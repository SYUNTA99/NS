#pragma once

/// @file BuildPlacedObject.h
/// @brief ObjectInstance 1 件から配置物 GameObject を組み立てる単一ファクトリ
///
/// @details 旧ブロックサブクラス群の prefab レシピ (どの Component をどう合成するか) を 1 箇所へ集約する
/// kind を BlockRegistry で引き、 mesh / material は AssetManager から借り、 collider / 挙動 Component を合成する
/// 構築のみを担い、 SceneBase への attach / OnStart / 衝突世界への登録は呼出側が行う
/// 依存: NS::Scene::GameObject / AssetManager, NS::Game::Level::ObjectInstance, NS::Game::Blocks::BlockRegistry

#include "Framework/Scene/Component.h"
#include "Framework/Scene/GameObject.h"

#include <cstdint>
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

namespace NS::Game::Level
{
    struct ObjectInstance;
    struct ComponentData;
    struct LevelData;
} // namespace NS::Game::Level

namespace NS::Game::Blocks
{
    /// ObjectInstance から配置物を組み立てて返す。 mesh / material は assets から借り、 free 配置物の .mat は
    /// materialPaths を介して解決する。 component を持たない object は nullptr を返す (呼出側が読み飛ばす)
    /// assets が deviceless (Builtin / SharedMaterial が nullptr) でも落ちない
    [[nodiscard]] std::unique_ptr<NS::Scene::GameObject> BuildPlacedObject(
        const NS::Game::Level::ObjectInstance& object,
        NS::Scene::AssetManager& assets,
        const std::vector<std::string>& materialPaths);

    /// kind が決めていた mesh / material / 色 / 当たり / 拾得を ComponentData 一覧へ展開する
    /// kind は引数で受け取り、 collider 寸法 / flags など kind 以外の属性は object から読む
    /// コイン / スターは視覚を持たないため PickupComponent のみを返す。 未対応 kind は空一覧を返す
    /// 積む typeName は ComponentRegistry の curated 名のみで、 任意 type は生成しない
    [[nodiscard]] std::vector<NS::Game::Level::ComponentData> MaterializeLegacyKind(
        std::uint16_t kind, const NS::Game::Level::ObjectInstance& object);

    /// 旧 kind 付きレベルを読込時に一度だけ実 component 一覧へ移行する
    /// LegacyKind placeholder を持つ object はその id を MaterializeLegacyKind で展開し、 placeholder ごと置換する
    /// 既に実 component を持つ object は変えない。 二重に呼んでも構成は変わらない (冪等)
    void MigrateLegacyLevel(NS::Game::Level::LevelData& level);

    /// grid 配置された固形 block か。 components から判定する (gridAligned かつ BoxCollider 持ちで
    /// slope / pole / hazard / 拾得を持たない)。 instancing / 昇格 / 当たり可視化の「固形」判定窓口
    [[nodiscard]] bool IsGridSolidObject(const NS::Game::Level::ObjectInstance& object);

    /// R で 90° 回す対象か。 SlopeCollider を持つか grid 固形なら true (掴み pole / 水 / 装飾は false)
    [[nodiscard]] bool IsRotatableObject(const NS::Game::Level::ObjectInstance& object);

    /// components から種別の表示名を導く ASCII 固定文字列 (Solid / Coin / Star / Slope NN / Pole / Hazard /
    /// Water / Decoration)。 Hierarchy / Inspector の見出しに使う。 未知構成は "?"
    [[nodiscard]] const char* ObjectDisplayName(const NS::Game::Level::ObjectInstance& object);

    /// asset 相対パスを ContentRoot 配下へ正規化して返す。 `..` で外へ出るパスは nullopt にし任意ファイル読込を防ぐ
    /// path 型メソッドのみで判定し、 実在確認の filesystem 操作系は呼ばない
    [[nodiscard]] std::optional<std::filesystem::path> ResolveContentPath(const std::string& relative);

    /// メッシュ参照文字列からメッシュを解決する。 builtin 名を先引きし、 外れたら ContentRoot 配下の相対パスとして
    /// glTF を読む。 空 / traversal / 未解決は nullptr を返す。 traversal は ResolveContentPath が弾く
    [[nodiscard]] NS::Graphics::Mesh* ResolveMeshFromRef(NS::Scene::AssetManager& assets, const std::string& meshRef);

    /// obj の Component 列から型 T の最初の一致を返す (無ければ nullptr)
    /// 描画 / 衝突 / editor が具象型を知らずに Component を取り出す共通窓口
    template <class T> [[nodiscard]] T* FindComponent(NS::Scene::GameObject& obj) noexcept
    {
        for (NS::Scene::Component* comp : obj.Components())
        {
            if (T* typed = dynamic_cast<T*>(comp))
                return typed;
        }
        return nullptr;
    }
} // namespace NS::Game::Blocks
