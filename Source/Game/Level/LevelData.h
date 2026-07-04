#pragma once

/// @file LevelData.h
/// @brief LevelData — `.nslvl` に書く永続データ。 PlayMode は const 参照のみで受ける
///
/// @details Strict 分離: 永続フィールドはここに、 runtime mutable な playerPosition /
/// coinCount 等は `PlayState` に置く。 `PlayMode` 側で `const LevelData&` を要求する
/// ことで「PlayMode は LevelData を書き換えない」 を compile-time に保証する
/// `ComputeCrc32()` は field 単位の明示 update なので vector capacity 等の内部 padding
/// に依存せず、 同一データに対して常に同じ値を返す

#include "Framework/Math/Math.h"
#include "Framework/Scene/ObjectRef.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace NS::Game::Level
{

    /// `ObjectInstance::flags` の bit。 グリッド配置物は bit0 を立て instancing / オートタイル対象にする
    inline constexpr std::uint8_t kObjectFlagGridAligned = 0x01;

    /// 当たり判定の形状種別。 `ObjectInstance::shapeCollider` に格納する。 既定 / 旧データは Box
    enum class ShapeCollider : std::uint8_t
    {
        Box = 0,
        Sphere = 1,
        Capsule = 2,
        Mesh = 3,
    };

    /// 反射 1 フィールドの永続値。 name は反射フィールド名、 value の代替型は FieldType と 1:1
    struct FieldValue
    {
        std::string name;
        /// 変種の宣言順に ComputeCrc32 と operator== の switch が依存する。 増減・並べ替え時は両方を直す
        std::variant<float, int, bool, NS::Math::Vector3, std::string, NS::Scene::ObjectRef> value;

        /// variant の Vector3 代替が operator== を持たないため代替ごとに明示比較する
        [[nodiscard]] bool operator==(const FieldValue& other) const noexcept;
    };

    /// 1 コンポーネントの永続表現。 型名 + 反射フィールド値一覧
    struct ComponentData
    {
        std::string typeName;
        std::vector<FieldValue> fields;

        /// fields 比較は FieldValue::operator== に委譲される
        [[nodiscard]] bool operator==(const ComponentData& other) const = default;
    };

    /// 配置物の永続表現。 コンポーネント一覧を内包する full SSOT 表現
    /// grid block も自由配置物も同じ型で 1 リストに格納する。 grid かどうかは flags の bit0 で区別する
    /// position / rotation すなわち quaternion / scale をフル保持し、 種別は components が表す
    /// materialIndex は `LevelData::materialPaths` への添字、 -1 は既定マテリアルを表す
    /// colliderHalfExtents は Transform と独立した当たり箱の local 半径で既定 0.5。 world では Transform.scale が乗る
    /// colliderOffset / colliderRotation は当たり箱を視覚と独立に owner local 空間でずらす / 回す。 既定は 0 / 単位
    /// shapeCollider は当たり判定形状 Box/Sphere/Capsule/Mesh で既定 / 旧データは Box
    /// colliderHalfExtents は形状で解釈が変わる: Box=各半径 / Sphere=x が半径 / Capsule=x 半径・y 半高 / Mesh=未使用
    /// objectId はレベル内で一意な永続 id で 0 は未割当。並べ替えや改名に耐えるオブジェクト参照のキーになる
    struct ObjectInstance
    {
        std::uint32_t objectId = 0;
        float positionX = 0.0f;
        float positionY = 0.0f;
        float positionZ = 0.0f;
        float rotationX = 0.0f;
        float rotationY = 0.0f;
        float rotationZ = 0.0f;
        float rotationW = 1.0f;
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        float scaleZ = 1.0f;
        std::int16_t materialIndex = -1;
        std::uint8_t flags = 0;
        std::uint8_t shapeCollider = 0;
        std::uint16_t reserved1 = 0;
        float colliderHalfExtentsX = 0.5f;
        float colliderHalfExtentsY = 0.5f;
        float colliderHalfExtentsZ = 0.5f;
        float colliderOffsetX = 0.0f;
        float colliderOffsetY = 0.0f;
        float colliderOffsetZ = 0.0f;
        float colliderRotationX = 0.0f;
        float colliderRotationY = 0.0f;
        float colliderRotationZ = 0.0f;
        float colliderRotationW = 1.0f;

        /// このオブジェクトが持つコンポーネント一覧。 full SSOT のコンポ構成
        std::vector<ComponentData> components;

        [[nodiscard]] bool operator==(const ObjectInstance& other) const = default;
    };

    /// シーンが所有する環境値。 保存形式に入る永続データで、 描画は毎フレームこれを EnvironmentSubsystem へ写す
    /// 既定値は中立の絵。 テーマは適用時にこの欄へ値を写し込む雛形で、 以降はシーンの値が正になる
    struct LevelEnvironment
    {
        /// 平行光の向き。 正規化前でよく、 シェーダ側で normalize する
        NS::Math::Vector3 lightDirection{-0.3f, -1.0f, -0.2f};
        /// 平行光の色。 HDR 込みで 1.3 等を許容する
        NS::Math::Vector3 lightColor{1.0f, 1.0f, 1.0f};
        /// 環境光の色。 N.L = 0 の影側ベース色になる
        NS::Math::Vector3 ambientColor{0.2f, 0.2f, 0.2f};
        /// skybox cubemap のディレクトリまたは .dds の ContentRoot 配下相対パス。 空文字なら skybox を描かない
        std::string skyboxCubemapPath{};
        /// block texture 配列の先頭 slice。 Rebuild 時の焼き込みが読む
        std::uint16_t blockTextureBaseSlice = 0;
    };

    /// `.nslvl` に書かれる永続データ。 PlayMode 中は const 参照でしか触らせない
    struct LevelData
    {
        /// grid block も自由配置物も含む唯一の配置物リスト。 grid かどうかは各要素の flags で判別する
        std::vector<ObjectInstance> objects;

        /// 次に割り当てる永続 object id。単調増加で欠番は再利用せず、削除済み id が別物を指す事故を防ぐ
        std::uint32_t nextObjectId = 1;

        /// objects の materialIndex が参照する .mat 相対パス表
        std::vector<std::string> materialPaths;

        /// シーンの見た目を確定する環境値。 lighting と skybox と block の slice 帯
        LevelEnvironment environment{};

        std::uint16_t bgmId = 0;
        std::uint16_t coinThreshold = 0;
        std::uint16_t timeLimitSeconds = 0;

        /// field 単位の明示 update で計算。 vector は `data()+size()*sizeof(element)` のみ対象で capacity は除外
        [[nodiscard]] std::uint32_t ComputeCrc32() const noexcept;
    };

    /// objects 配列で「該当無し」を表す添字
    inline constexpr std::size_t kNoObjectIndex = static_cast<std::size_t>(-1);

    /// 「object 無し」を表す永続 id の番兵。実 id は採番が 1 始まりで 0 を取らない
    /// ObjectRef フィールドの「0 は未設定」と同じ約束
    inline constexpr std::uint32_t kNoObjectId = 0;

    /// 新規レベルでプレイヤーを置く既定の capsule 中心高さ。 床 block 上面 0.5 + capsule 半高 0.9 + 1cm
    inline constexpr float kDefaultPlayerSpawnY = 1.41f;

    /// プレイヤー実体か。 入力で動く能力そのものが種別の印なので、 専用マーカーを増やさず
    /// PlayerInputComponent の有無で判定する
    [[nodiscard]] bool IsPlayerObject(const ObjectInstance& object) noexcept;

    /// objects からプレイヤー実体を探す。 最初の 1 件の添字、 無ければ kNoObjectIndex
    /// 複数居ても先頭を正とする。 読込の門が 1 体を保証し、 余分は読込時に警告済み
    [[nodiscard]] std::size_t FindPlayerObjectIndex(const LevelData& level) noexcept;

    /// 指定 pose のプレイヤー実体 ObjectInstance を作る。 components は既定構成一式で、
    /// scale は capsule 当たり 0.4/0.9/0.4 に cube mesh の見た目を合わせる値
    [[nodiscard]] ObjectInstance MakePlayerObject(const NS::Math::Vector3& position,
                                                  const NS::Math::Quaternion& rotation);

    /// 追従カメラ実体か。 ThirdPersonFollowComponent の有無で判定する
    [[nodiscard]] bool IsFollowCameraObject(const ObjectInstance& object) noexcept;

    /// objects から追従カメラ実体を探す。 最初の 1 件の添字、 無ければ kNoObjectIndex
    [[nodiscard]] std::size_t FindFollowCameraObjectIndex(const LevelData& level) noexcept;

    /// 追従カメラ実体 ObjectInstance を作る。 追従先の永続 id を Target 参照へ焼く。 0 は未設定
    /// pose は追従で毎フレーム決まるため Transform は既定のまま。 視覚と当たりは持たない
    [[nodiscard]] ObjectInstance MakeFollowCameraObject(std::uint32_t targetObjectId);

    /// 永続 id が `id` の object の添字。無ければ kNoObjectIndex、kNoObjectId は常に該当無し
    [[nodiscard]] std::size_t FindObjectIndexById(const LevelData& level, std::uint32_t id) noexcept;

    /// 永続 object id を 1 個割り当ててカウンタを進める。生成経路が新規 object に振るのに使う
    [[nodiscard]] std::uint32_t AllocateObjectId(LevelData& level) noexcept;

    /// 全 object の永続 id を「非 0 かつ一意」へ整える。未割当と重複には新 id を振り、
    /// nextObjectId を既存最大 id より先へ進める。旧版や手編集のファイルを読込直後に通す移行の門
    void EnsureUniqueObjectIds(LevelData& level);

    /// 存在しない object を指す ObjectRef フィールドを未設定 0 へ戻し、直した件数を返す
    /// 手編集や参照先削除で宙に浮いた参照を読込直後に浄化し、実行時の照合失敗を入口で断つ
    [[nodiscard]] std::size_t PruneDanglingObjectRefs(LevelData& level);

    /// object の collider 形状を返す。 未知値は安全側で Box に倒す
    [[nodiscard]] ShapeCollider ObjectShapeCollider(const ObjectInstance& object) noexcept;
    /// object の collider 形状を設定する
    void SetObjectShapeCollider(ObjectInstance& object, ShapeCollider shape) noexcept;

    /// gridAligned object の cell 座標 = position を最近接整数へ丸めた値
    [[nodiscard]] std::int16_t ObjectCellX(const ObjectInstance& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellY(const ObjectInstance& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellZ(const ObjectInstance& object) noexcept;

    /// gridAligned かつ cell の x, y, z に一致する最初の object の添字。 無ければ kNoObjectIndex
    [[nodiscard]] std::size_t FindGridObjectAtCell(const LevelData& level,
                                                   std::int16_t x,
                                                   std::int16_t y,
                                                   std::int16_t z) noexcept;

    /// cell の x, y, z と rotationStep 0..3 から gridAligned な既定 solid の ObjectInstance を作る
    /// 既定 solid 一式すなわち cube 描画 + Box 当たりを component として積む
    [[nodiscard]] ObjectInstance MakeGridObject(std::int16_t x,
                                                std::int16_t y,
                                                std::int16_t z,
                                                std::uint8_t rotationStep);

    /// gridAligned object の現在の 90° 回転 step を quaternion から最近接で復元する
    [[nodiscard]] std::uint8_t GridRotationStep(const ObjectInstance& object) noexcept;

    /// gridAligned object の回転を rotationStep に対応する Y 軸 yaw quaternion に設定する
    void SetGridRotationStep(ObjectInstance& object, std::uint8_t rotationStep) noexcept;

    /// undo の概算メモリに使う sizeof 外の heap 量。 反射値の文字列ヒープは概算に含めない
    /// component vector / typeName / field 名の確保分を数える。 配置・変形系 Command の EstimatedBytes が使う
    [[nodiscard]] std::size_t EstimatedHeapBytes(const ComponentData& component) noexcept;
    [[nodiscard]] std::size_t EstimatedHeapBytes(const ObjectInstance& object) noexcept;

    /// object.components から typeName 一致の最初の 1 件を返す。 無ければ nullptr
    /// component / field 走査の唯一の窓口。 各 consumer が同じループを手書きするのを防ぐ
    [[nodiscard]] const ComponentData* FindComponentData(const ObjectInstance& object,
                                                         std::string_view typeName) noexcept;
    /// component.fields から name 一致の最初の 1 件を返す。 無ければ nullptr
    [[nodiscard]] const FieldValue* FindField(const ComponentData& component, std::string_view name) noexcept;

    /// 拾得種別を返す。 PickupComponent が無ければ -1、 "Pickup Kind" 欠損は 0 でコイン既定
    /// 0=コイン / 1=ゴール。 Blocks の配置物の表示・固形判定と PlayMode のプレイ拾得判定が同じ契約を読む
    [[nodiscard]] int PickupKindOf(const ObjectInstance& object) noexcept;

} // namespace NS::Game::Level
