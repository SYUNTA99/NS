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
} // namespace NS::Game::Level

namespace NS::Game::Blocks
{
    /// ObjectInstance から配置物を組み立てて返す。 mesh / material は assets から借り、 free 配置物の .mat は
    /// materialPaths を介して解決する。 未対応の grid kind は nullptr を返す (呼出側が読み飛ばす)
    /// assets が deviceless (Builtin / SharedMaterial が nullptr) でも落ちない
    [[nodiscard]] std::unique_ptr<NS::Scene::GameObject> BuildPlacedObject(
        const NS::Game::Level::ObjectInstance& object,
        NS::Scene::AssetManager& assets,
        const std::vector<std::string>& materialPaths);

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
