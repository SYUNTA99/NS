#pragma once

/// @file GameObject.h
/// @brief NS::Scene::GameObject — Transform を持つ継承可能基底
///
/// 派生クラス (Player / Block / Enemy 等) の共通基底。 配下 Component は AddComponent<T>() で
/// 生成し、 GameObject が unique_ptr で寿命を所有する。 Tick / OnEndPlay 伝播用の priority 昇順
/// 生ポインタ列を `m_components` に併せて保持する
///
/// Lifecycle:
///   - OnStart()    — SceneBase attach 直後に 1 回、配下 Component の OnStart を伝播
///   - OnUpdate()   — fixed step 毎回、IsActive==true の Component に伝播
///                    dt は `NS::Core::FrameTimer::FixedDelta()` で取得
///   - OnEndPlay()  — SceneBase 破棄前に 1 回、登録逆順で Component::OnEndPlay を呼ぶ
///
/// 他 GameObject へのアクセスはコンストラクタ経由の明示的な生ポインタ注入のみ (GetComponent<T>() なし)

#include "Framework/Scene/Transform.h"

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace NS::Scene
{
    class Component;
    class SceneBase;

    /// 全 GameObject 派生の基底
    class GameObject
    {
    public:
        GameObject() noexcept = default;
        virtual ~GameObject() noexcept;

        GameObject(const GameObject&) = delete;
        GameObject& operator=(const GameObject&) = delete;
        GameObject(GameObject&&) = delete;
        GameObject& operator=(GameObject&&) = delete;

        /// Root Transform (値型 member)。階層構築は SetParent で
        [[nodiscard]] Transform& Root() noexcept { return m_root; }
        [[nodiscard]] const Transform& Root() const noexcept { return m_root; }

        [[nodiscard]] GameObject* Parent() const noexcept { return m_parent; }
        /// parent==nullptr で root 化。Transform の親子関係も同期更新する
        void SetParent(GameObject* parent) noexcept;
        [[nodiscard]] const std::vector<GameObject*>& Children() const noexcept { return m_children; }

        [[nodiscard]] const std::vector<Component*>& Components() const noexcept { return m_components; }

        /// Component を生成して寿命を所有し priority 昇順の tick 列へ登録する。戻り値は非所有の生ポインタ
        template <class T, class... Args> T* AddComponent(Args&&... args)
        {
            static_assert(std::is_base_of_v<Component, T>, "T は Component 派生でなければならない");
            auto owned = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = owned.get();
            m_ownedComponents.push_back(std::move(owned));
            AttachOwnedComponent(raw);
            return raw;
        }

        /// 所有 SceneBase。SceneBase attach 前 / 破棄後は nullptr
        [[nodiscard]] SceneBase* OwningScene() const noexcept { return m_scene; }
        /// SceneBase 側が attach 時に呼ぶ。GameObject 派生から手動で呼ばない
        void AttachScene(SceneBase* scene) noexcept { m_scene = scene; }

        /// 配下 Component の OnStart を伝播。override 時は前後に `GameObject::OnStart()` を呼ぶこと
        virtual void OnStart();
        /// IsActive==true の Component にだけ OnUpdate() を伝播
        virtual void OnUpdate();
        /// 登録逆順で Component::OnEndPlay を呼ぶ
        virtual void OnEndPlay();

        [[nodiscard]] bool IsAlive() const noexcept { return m_alive; }
        /// 次フレーム以降 SceneBase 側で安全に reap される予定の印
        void MarkPendingKill() noexcept { m_alive = false; }

    private:
        /// Component に owner を注入し tick 列へ priority 昇順で挿入する
        void AttachOwnedComponent(Component* comp) noexcept;

        Transform m_root;
        std::vector<Component*> m_components;
        std::vector<std::unique_ptr<Component>> m_ownedComponents; // 所有権保持用 (tick 順序は m_components が担う)
        std::vector<GameObject*> m_children;
        GameObject* m_parent = nullptr;
        SceneBase* m_scene = nullptr;
        bool m_alive = true;

        void DetachFromParent() noexcept;
    };

} // namespace NS::Scene
