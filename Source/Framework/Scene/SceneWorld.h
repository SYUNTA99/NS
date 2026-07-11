#pragma once

/// @file SceneWorld.h
/// @brief SceneWorld — SceneData から組む runtime world 表現の所有と構築
///
/// @details 配置物 GameObject と衝突プリミティブを SceneData から一括で組み直す
/// runtime も editor も同じ Rebuild 経路を通り、 scene は公開読み口からの観測と描画だけを行う
/// 配置物 1 件の組み立ては呼出側が渡すファクトリに委ね、 器の型選択や資産解決の知識を持たない
/// 機能別の型付き控えも持たず、 読み手は ForEachComponent で欲しい component 型を問い合わせる
/// 特定の 1 体が要る読み手は、 データ側で対象を決めて ObjectRefSubsystem の id 解決で実体を引く
/// 依存: NS::Scene::GameObject / ObjectData, NS::Physics::PhysicsWorld

#include "Framework/Scene/GameObject.h"

#include <functional>

namespace NS::Physics
{
    class PhysicsWorld;
}

namespace NS::Scene
{
    class SceneBase;
    struct ObjectData;
    struct SceneData;

    /// ObjectData 1 件から配置物を組むファクトリ。 組めない object は nullptr を返し読み飛ばされる
    using ObjectFactoryFn = std::function<std::unique_ptr<GameObject>(const ObjectData&)>;

    /// SceneData から組まれる runtime world。 所有と構築を一手に担う
    class SceneWorld
    {
    public:
        SceneWorld();
        ~SceneWorld();

        SceneWorld(const SceneWorld&) = delete;
        SceneWorld& operator=(const SceneWorld&) = delete;
        SceneWorld(SceneWorld&&) = delete;
        SceneWorld& operator=(SceneWorld&&) = delete;

        /// data の objects から全 runtime 表現を組み直す。 既存の配置物と衝突 world は必ず先に空へ戻す
        /// factory が空の起動前 / テストでは物を組まず、 衝突 world も空のまま返る
        void Rebuild(const SceneData& data,
                     SceneBase& scene,
                     NS::Physics::PhysicsWorld& physics,
                     const ObjectFactoryFn& factory);

        /// 配置物を逆順に畳んで所有物を空へ戻す。 scene の OnShutdown と Rebuild 冒頭が呼ぶ
        void Clear();

        /// 配置物の単一所有リスト。 種別を問わず全配置物を generic に持つ
        [[nodiscard]] std::vector<std::unique_ptr<GameObject>>& Objects() noexcept { return m_objects; }
        [[nodiscard]] const std::vector<std::unique_ptr<GameObject>>& Objects() const noexcept { return m_objects; }

        /// Objects()[i] に対応する data.objects の添字。 Objects() と同長・ 1:1
        [[nodiscard]] const std::vector<std::size_t>& SourceIndices() const noexcept { return m_objectSourceIndices; }

        /// 全配置物から型 T の component を訪ねる。 反射の is-a 照合なので抽象基底型でも派生を引ける
        /// 型付き控えの代わりの問い合わせ口で、 実体の寿命は Objects() が握ったまま
        template <class T, class Fn> void ForEachComponent(Fn&& fn) const
        {
            for (const auto& obj : m_objects)
                for (Component* comp : obj->Components())
                    if (auto* typed = ComponentCast<T>(comp))
                        fn(*typed);
        }

    private:
        std::vector<std::unique_ptr<GameObject>> m_objects;
        std::vector<std::size_t> m_objectSourceIndices;
    };

} // namespace NS::Scene
