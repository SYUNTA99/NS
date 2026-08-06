#pragma once

#include "Runtime/Object/Component.h"
#include "Runtime/Object/Object.h"
#include "Runtime/Object/Transform.h"

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace NS::Object
{
    class Scene;
    class TransformComponent;

    /// @brief Transform を持つ継承可能基底。Player / Block / Enemy などの派生クラスの共通基底
    /// @details 配下 Component は AddComponent<T>() で生成し、 GameObject が unique_ptr で寿命を所有する
    /// Tick / OnEndPlay 伝播用の priority 昇順の生ポインタ列を `m_components` に併せて保持する
    /// OnStart / OnUpdate / OnEndPlay は配下 Component へ伝播するだけの補助で、派生の拡張点ではない
    /// 毎フレームの更新は World が持つ。全配置物の Component を priority 昇順に集めて直接回すため、
    /// ここの OnUpdate は通らない
    /// 他 GameObject へはコンストラクタで渡した生ポインタでしか触れない。 FindComponent<T>() は自分の Component
    /// 列しか見ない
    class GameObject : public Object
    {
    public:
        GameObject() noexcept;
        virtual ~GameObject() noexcept;

        /// TransformComponent が持つ Root Transform。階層構築は SetParent で
        [[nodiscard]] Transform& Root() noexcept { return *m_transform; }
        [[nodiscard]] const Transform& Root() const noexcept { return *m_transform; }

        /// 実行時にコードが足す一時オブジェクトか。 true は保存・凍結・作業データに写らず、
        /// データからの組み直し後も残る
        [[nodiscard]] bool IsTransient() const noexcept { return m_transient; }

        /// 一時オブジェクトの印。 Scene::SpawnTransient が立てる。 テストは直接立ててよい
        void SetTransient(bool transient) noexcept { m_transient = transient; }

        /// world の組み直し・当たりの張り直しの後に scene が一時オブジェクトへ知らせる。 データ由来の配置物には来ない
        virtual void OnWorldChanged() {}

        [[nodiscard]] GameObject* Parent() const noexcept { return m_parent; }
        /// parent==nullptr で root 化。Transform の親子関係も同期更新する
        void SetParent(GameObject* parent) noexcept;
        [[nodiscard]] const std::vector<GameObject*>& Children() const noexcept { return m_children; }

        [[nodiscard]] const std::vector<Component*>& Components() const noexcept { return m_components; }

        /// Component 列から型 T の最初の一致を返す。無ければ nullptr。具象型を知らずに同じ object の Component を引く読み取り経路
        /// リフレクション鎖の照合で一致を見るため T はリフレクション宣言を持つこと。未宣言型はここがコンパイルエラーになり、
        /// 実行時に静かに見つからない事故を防ぐ。派生型は基底型の検索にも一致する。読み取りのみで所有・順序には触れない
        template <class T> [[nodiscard]] T* FindComponent() noexcept
        {
            const auto* target = T::StaticReflection();
            for (Component* comp : m_components)
                if (comp != nullptr && comp->IsA(target))
                    return static_cast<T*>(comp);
            return nullptr;
        }
        template <class T> [[nodiscard]] const T* FindComponent() const noexcept
        {
            const auto* target = T::StaticReflection();
            for (const Component* comp : m_components)
                if (comp != nullptr && comp->IsA(target))
                    return static_cast<const T*>(comp);
            return nullptr;
        }

        /// Component を生成して寿命を所有し priority 昇順の tick 列へ登録する。戻り値は非所有の生ポインタ
        template <class T, class... Args> T* AddComponent(Args&&... args)
        {
            static_assert(!std::is_same_v<T, TransformComponent>,
                          "TransformComponent は器が必ず 1 つ持つ。Root() を使う");
            return AddComponentUnchecked<T>(std::forward<Args>(args)...);
        }

        /// 所有 Scene。Scene attach 前 / 破棄後は nullptr
        [[nodiscard]] Scene* OwningScene() const noexcept { return m_scene; }
        /// Scene 側が attach 時に呼ぶ。GameObject 派生から手動で呼ばない
        void AttachScene(Scene* scene) noexcept { m_scene = scene; }

        /// 配下 Component の OnStart を伝播
        void OnStart();
        /// IsActive==true の Component にだけ OnUpdate() を伝播
        void OnUpdate();
        /// 登録逆順で Component::OnEndPlay を呼ぶ
        void OnEndPlay();

        [[nodiscard]] bool IsAlive() const noexcept { return m_alive; }
        /// 即時破棄ではなく、次フレーム以降 Scene 側で安全に回収される
        void Destroy() noexcept { m_alive = false; }

        /// この配置物自身の active 値。親の状態は含まない
        [[nodiscard]] bool IsActiveSelf() const noexcept { return m_activeSelf; }

        /// @brief 自分と全ての祖先が有効か
        /// @details Component::IsActive がこれを見るので、偽の間は配下 Component が更新も描画も当たりも止まる
        [[nodiscard]] bool IsActiveInHierarchy() const noexcept;

        /// active を切り替える。子の値は触らないので、親を戻せば子も一緒に戻る
        void SetActive(bool active) noexcept { m_activeSelf = active; }

        /// 親の中での並び順。ヒエラルキーの表示順で、組み立てはこの順に並べる
        [[nodiscard]] std::uint32_t Order() const noexcept { return m_order; }

    private:
        // 並び順の書き込みは World::Rebuild の data 適用経路だけに絞る
        friend class World;
        void SetOrder(std::uint32_t order) noexcept { m_order = order; }

        /// Component に owner を注入し tick 列へ priority 昇順で挿入する
        void AttachOwnedComponent(Component* comp) noexcept;

        /// 型を問わず積む本体。TransformComponent を積めるのはコンストラクタだけ
        template <class T, class... Args> T* AddComponentUnchecked(Args&&... args)
        {
            static_assert(std::is_base_of_v<Component, T>, "T は Component 派生でなければならない");
            auto owned = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = owned.get();
            m_ownedComponents.push_back(std::move(owned));
            AttachOwnedComponent(raw);
            return raw;
        }

        Transform* m_transform = nullptr;                          // TransformComponent が持つ実体、GameObject が必ず 1 つ積む
        std::vector<Component*> m_components;                      // priority 昇順の tick 列、 非所有
        std::vector<std::unique_ptr<Component>> m_ownedComponents; // 所有権保持用。 tick 順序は m_components が担う
        std::vector<GameObject*> m_children;                       // 子 GameObject、 非所有
        GameObject* m_parent = nullptr;                            // 親 GameObject、 root なら nullptr
        Scene* m_scene = nullptr;                                  // 所有 Scene、 attach 前後は nullptr
        std::uint32_t m_order = 0;                                 // 親の中での並び順
        bool m_alive = true;                                       // false で次フレーム回収対象
        bool m_activeSelf = true;                                  // false で配下 Component が全て止まる
        bool m_transient = false;                                  // 一時オブジェクトの印。保存・凍結に写らない

        void DetachFromParent() noexcept;
    };

} // namespace NS::Object
