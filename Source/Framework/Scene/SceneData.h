#pragma once

/// @file SceneData.h
/// @brief SceneData — `.scene` に書く永続シーン。 配置物一覧とシーン付随プロパティの器
///
/// @details 配置物は transform + 反射コンポーネント一覧の ObjectData に統一し、 当たりも
/// 見た目も components が唯一の出所になる。 実行時に書き換えない読み手へは const 参照で渡す
/// `ComputeCrc32()` は field 単位の明示 update なので vector capacity 等の内部 padding
/// に依存せず、 同一データに対して常に同じ値を返す
/// 依存: NS::Math, NS::Scene::ObjectRef

#include "Framework/Math/Math.h"
#include "Framework/Scene/ObjectRef.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace NS::Scene
{

    /// 反射 1 フィールドの永続値。 name は反射フィールド名、 value の代替型は FieldType と 1:1
    struct FieldValue
    {
        std::string name;
        /// 変種の宣言順に ComputeCrc32 と operator== の switch が依存する。 増減・並べ替え時は両方を直す
        std::variant<float, int, bool, NS::Math::Vector3, std::string, ObjectRef> value;

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

    /// 配置物の永続表現。 コンポーネント一覧を内包し、 当たりも見た目も components が唯一の出所
    /// 配置物は種別を問わず同じ型で 1 リストに格納する
    /// position / rotation すなわち quaternion / scale をフル保持し、 種別は components が表す
    /// materialIndex は `SceneData::materialPaths` への添字、 -1 は既定マテリアルを表す
    /// objectId はシーン内で一意な永続 id で 0 は未割当。並べ替えや改名に耐えるオブジェクト参照のキーになる
    struct ObjectData
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

        /// このオブジェクトが持つコンポーネント一覧
        std::vector<ComponentData> components;

        [[nodiscard]] bool operator==(const ObjectData& other) const = default;
    };

    /// シーンが所有する環境値。 保存形式に入る永続データで、 描画は毎フレームこれを EnvironmentSubsystem へ写す
    /// 既定値は中立の絵。 適用側がこの欄へ値を写し込む雛形で、 以降はシーンの値が正になる
    struct SceneEnvironment
    {
        /// 平行光の向き。 正規化前でよく、 シェーダ側で normalize する
        NS::Math::Vector3 lightDirection{-0.3f, -1.0f, -0.2f};
        /// 平行光の色。 HDR 込みで 1.3 等を許容する
        NS::Math::Vector3 lightColor{1.0f, 1.0f, 1.0f};
        /// 環境光の色。 N.L = 0 の影側ベース色になる
        NS::Math::Vector3 ambientColor{0.2f, 0.2f, 0.2f};
        /// skybox cubemap のディレクトリまたは .dds の ContentRoot 配下相対パス。 空文字なら skybox を描かない
        std::string skyboxCubemapPath{};
    };

    /// `.scene` に書かれる永続シーン。 プレイ経路には const 参照でしか渡さない
    struct SceneData
    {
        /// 全配置物の唯一のリスト
        std::vector<ObjectData> objects;

        /// 次に割り当てる永続 object id。単調増加で欠番は再利用せず、削除済み id が別物を指す事故を防ぐ
        std::uint32_t nextObjectId = 1;

        /// objects の materialIndex が参照する .mat 相対パス表
        std::vector<std::string> materialPaths;

        /// シーンの見た目を確定する環境値。 lighting と skybox
        SceneEnvironment environment{};

        /// field 単位の明示 update で計算。 vector は `data()+size()*sizeof(element)` のみ対象で capacity は除外
        [[nodiscard]] std::uint32_t ComputeCrc32() const noexcept;
    };

    /// objects 配列で「該当無し」を表す添字
    inline constexpr std::size_t kNoObjectIndex = static_cast<std::size_t>(-1);

    /// 「object 無し」を表す永続 id の番兵。実 id は採番が 1 始まりで 0 を取らない
    /// ObjectRef フィールドの「0 は未設定」と同じ約束
    inline constexpr std::uint32_t kNoObjectId = 0;

    /// 永続 id が `id` の object の添字。無ければ kNoObjectIndex、kNoObjectId は常に該当無し
    [[nodiscard]] std::size_t FindObjectIndexById(const SceneData& scene, std::uint32_t id) noexcept;

    /// 永続 object id を 1 個割り当ててカウンタを進める。生成経路が新規 object に振るのに使う
    [[nodiscard]] std::uint32_t AllocateObjectId(SceneData& scene) noexcept;

    /// 全 object の永続 id を「非 0 かつ一意」へ整える。未割当と重複には新 id を振り、
    /// nextObjectId を既存最大 id より先へ進める。手編集のファイルを読込直後に通す整合の門
    void EnsureUniqueObjectIds(SceneData& scene);

    /// 存在しない object を指す ObjectRef フィールドを未設定 0 へ戻し、直した件数を返す
    /// 手編集や参照先削除で宙に浮いた参照を読込直後に浄化し、実行時の照合失敗を入口で断つ
    [[nodiscard]] std::size_t PruneDanglingObjectRefs(SceneData& scene);

    /// undo の概算メモリに使う sizeof 外の heap 量。 反射値の文字列ヒープは概算に含めない
    /// component vector / typeName / field 名の確保分を数える。 配置・変形系 Command の EstimatedBytes が使う
    [[nodiscard]] std::size_t EstimatedHeapBytes(const ComponentData& component) noexcept;
    [[nodiscard]] std::size_t EstimatedHeapBytes(const ObjectData& object) noexcept;

    /// object.components から typeName 一致の最初の 1 件を返す。 無ければ nullptr
    /// component / field 走査の唯一の窓口。 各 consumer が同じループを手書きするのを防ぐ
    [[nodiscard]] const ComponentData* FindComponentData(const ObjectData& object, std::string_view typeName) noexcept;
    /// component.fields から name 一致の最初の 1 件を返す。 無ければ nullptr
    [[nodiscard]] const FieldValue* FindField(const ComponentData& component, std::string_view name) noexcept;

} // namespace NS::Scene
