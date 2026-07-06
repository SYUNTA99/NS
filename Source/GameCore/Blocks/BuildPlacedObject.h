#pragma once

/// @file BuildPlacedObject.h
/// @brief ObjectInstance 1 件から配置物 GameObject を組み立てる単一ファクトリ
///
/// @details object.components を ComponentRegistry で生成し反射 set で値を入れて 1 箇所へ集約する
/// mesh / material は AssetManager から借り、 collider / 挙動 Component を合成する
/// 構築のみを担い、 SceneBase への attach / OnStart / 衝突世界への登録は呼出側が行う
/// 依存: NS::Scene::GameObject / AssetManager, NS::GameCore::Level::ObjectInstance

#include "Framework/Math/Math.h"
#include "Framework/Scene/Component.h"
#include "Framework/Scene/GameObject.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace NS::Graphics
{
    class Mesh;
} // namespace NS::Graphics

namespace NS::Scene
{
    class AssetManager;
} // namespace NS::Scene

namespace NS::GameCore::Level
{
    struct ObjectInstance;
    struct ComponentData;
    struct LevelData;
} // namespace NS::GameCore::Level

namespace NS::GameCore::Blocks
{
    /// ObjectInstance から配置物を組み立てて返す。 mesh / material は assets から借り、 free 配置物の .mat は
    /// materialPaths を介して解決する。 component を持たない object は nullptr を返し呼出側が読み飛ばす
    /// assets が deviceless つまり Builtin / SharedMaterial が nullptr でも落ちない
    [[nodiscard]] std::unique_ptr<NS::Scene::GameObject> BuildPlacedObject(
        const NS::GameCore::Level::ObjectInstance& object,
        NS::Scene::AssetManager& assets,
        const std::vector<std::string>& materialPaths);

    /// テクスチャが揃うまで cube 配置物に与える基準色
    inline constexpr NS::Math::Vector3 kSolidBaseColor{0.70f, 0.70f, 0.75f};

    /// 実プレイヤーの mesh に与える基準色。 データ既定と scene 側の直組みが同じ赤を共有する
    inline constexpr NS::Math::Vector3 kPlayerBaseColor{0.85f, 0.20f, 0.20f};

    /// grid セルに置く cube 1 個分の component 一覧を組む。 cube メッシュ + block 材質 + 半径 0.5 の Box 当たり
    /// MakeGridObject と editor の grid 配置が同じ cube を起こす窓口
    [[nodiscard]] std::vector<NS::GameCore::Level::ComponentData> MakeGridCubeComponents();

    /// grid セルに置く楔スロープ 1 個分の component 一覧を組む。 角度に対応する wedge メッシュ + SlopeCollider
    /// angleDegrees は 45 / 30 / 22.5 / 15 度を想定し、 メッシュと当たりの傾斜を一致させる
    [[nodiscard]] std::vector<NS::GameCore::Level::ComponentData> MakeGridSlopeComponents(float angleDegrees);

    /// 接触でレベルクリアになるゴール 1 個分の component 一覧を組む。 視覚を持たない goal pickup に
    /// editor で見える金色 cube を載せる。 PlayMode が PickupComponent の種別を読んでクリアを判定する
    [[nodiscard]] std::vector<NS::GameCore::Level::ComponentData> MakeGoalComponents();

    /// プレイヤー実体の既定 component 一覧を組む。 実プレイヤーの直組み構成と同じ
    /// mesh 描画 + 移動 + 入力 + 接地影の 4 点。 値の細部は component のコード既定に任せる
    [[nodiscard]] std::vector<NS::GameCore::Level::ComponentData> MakeDefaultPlayerComponents();

    /// 追従カメラ実体の component 一覧を組む。 ThirdPersonFollowComponent 1 点で、 追従先の
    /// 永続 id を Target 参照へ、 プレイの遠景 100 を Far Plane へ焼く。 感触値はコード既定に任せる
    [[nodiscard]] std::vector<NS::GameCore::Level::ComponentData> MakeFollowCameraComponents(
        std::uint32_t targetObjectId);

    /// 自由配置の cube 1 個分の component 一覧を組む。 cube メッシュ + shapeCollider に応じた Box/Sphere/Capsule 当たり
    /// 当たり寸法 / offset / 回転は object の collider フィールドから読む
    [[nodiscard]] std::vector<NS::GameCore::Level::ComponentData> MakeFreeCubeComponents(
        const NS::GameCore::Level::ObjectInstance& object);

    /// 固形 block か。 BoxCollider を持ち slope / hazard / 拾得を持たないことを components から判定する
    /// 当たり可視化 / コヨーテ縁 / R 回転対象の「固形」判定窓口
    [[nodiscard]] bool IsGridSolidObject(const NS::GameCore::Level::ObjectInstance& object);

    /// R で 90° 回す対象か。 SlopeCollider を持つか grid 固形なら true。 水 / 装飾は false
    [[nodiscard]] bool IsRotatableObject(const NS::GameCore::Level::ObjectInstance& object);

    /// components から種別の表示名を導く ASCII 固定文字列。 Player / Camera / Solid / Coin / Goal /
    /// Slope NN / Hazard / Water / Decoration のいずれか。 Hierarchy / Inspector の見出しに使う。 未知構成は "?"
    [[nodiscard]] const char* ObjectDisplayName(const NS::GameCore::Level::ObjectInstance& object);

    /// asset 相対パスを ContentRoot 配下へ正規化して返す。 `..` で外へ出るパスは nullopt にし任意ファイル読込を防ぐ
    /// path 型メソッドのみで判定し、 実在確認の filesystem 操作系は呼ばない
    [[nodiscard]] std::optional<std::filesystem::path> ResolveContentPath(const std::string& relative);

    /// メッシュ参照文字列からメッシュを解決する。 builtin 名を先引きし、 外れたら ContentRoot 配下の相対パスとして
    /// glTF を読む。 空 / traversal / 未解決は nullptr を返す。 traversal は ResolveContentPath が弾く
    [[nodiscard]] NS::Graphics::Mesh* ResolveMeshFromRef(NS::Scene::AssetManager& assets, const std::string& meshRef);

    /// obj の collider を Box / Sphere / Capsule / Slope の順に見て最初に見つかった世界 AABB を返す
    /// 影の受け皿と当たり可視化が collider 種別に依存せず世界境界を 1 つ取る窓口。 collider が無ければ nullopt
    [[nodiscard]] std::optional<NS::Math::AABB> ColliderWorldAABB(NS::Scene::GameObject& obj) noexcept;

    /// obj の Component 列から型 T の最初の一致を返す。 無ければ nullptr
    /// 描画 / 衝突 / editor が具象型を知らずに Component を取り出す共通窓口
    template <class T> [[nodiscard]] T* FindComponent(NS::Scene::GameObject& obj) noexcept
    {
        return obj.FindComponent<T>();
    }
} // namespace NS::GameCore::Blocks
