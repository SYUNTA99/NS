#pragma once

/// @file game_object.h
/// @brief NS::Scene::GameObject — Transform を持つ継承可能基底 (, , )。
///
/// UE5 の AActor 相当だが命名は GameObject。Component を named members として固定スロットで
/// 保有する派生クラス (Player / Block / Enemy 等) の共通基底。Component 自体は派生クラス側が
/// 値型 or unique_ptr で所有し、GameObject は Tick / OnEndPlay 伝播用に raw 参照を `m_components`
/// に保持する。
///
/// Lifecycle ():
///   - OnStart()       — Scene attach 直後に 1 回、配下 Component の OnStart を伝播
///   - OnUpdate(dt)    — fixed step 毎回、IsActive==true の Component に伝播
///   - OnEndPlay()     — Scene 破棄前に 1 回、登録逆順で Component::OnEndPlay を呼ぶ
///
/// Cross-GameObject アクセスは ctor 経由の明示 raw pointer 注入のみ (GetComponent<T>() なし)。

#include "Framework/Scene/Transform.h"

#include <vector>

namespace NS::App
{
    class Scene;
}

namespace NS::Scene
{
    class Component;

    /// 全 GameObject 派生の基底。
    class GameObject
    {
    public:
        GameObject() noexcept = default;
        virtual ~GameObject() noexcept;

        GameObject(const GameObject&) = delete;
        GameObject& operator=(const GameObject&) = delete;
        GameObject(GameObject&&) = delete;
        GameObject& operator=(GameObject&&) = delete;

        /// Root Transform (値型 member)。階層構築は SetParent で。
        [[nodiscard]] Transform& Root() noexcept { return m_root; }
        [[nodiscard]] const Transform& Root() const noexcept { return m_root; }

        [[nodiscard]] GameObject* Parent() const noexcept { return m_parent; }
        /// parent==nullptr で root 化。Transform の親子関係も同期更新する。
        void SetParent(GameObject* parent) noexcept;
        [[nodiscard]] const std::vector<GameObject*>& Children() const noexcept { return m_children; }

        /// Component を Tick/OnEndPlay 伝播リストに登録、`Component::m_owner` も注入する。
        /// 所有は派生クラスが行うため、本関数は raw 参照のみを保存する。
        void RegisterComponent(Component* comp) noexcept;
        /// Component の破棄前に呼ぶ。`m_components` から該当 raw pointer を除去し、
        /// `Component::m_owner` を nullptr に戻す。未登録 / null は no-op。
        void UnregisterComponent(Component* comp) noexcept;
        [[nodiscard]] const std::vector<Component*>& Components() const noexcept { return m_components; }

        /// 所有 Scene。Scene attach 前 / 破棄後は nullptr。
        [[nodiscard]] NS::App::Scene* OwningScene() const noexcept { return m_scene; }
        /// Scene 側が attach 時に呼ぶ。GameObject 派生から手動で呼ばない。
        void AttachScene(NS::App::Scene* scene) noexcept { m_scene = scene; }

        /// 配下 Component の OnStart を伝播。派生クラスは override で固有処理を足す前後に
        /// `GameObject::OnStart()` を呼ぶこと。
        virtual void OnStart();
        /// IsActive==true の Component にだけ OnUpdate(dt) を伝播。
        virtual void OnUpdate(float dt);
        /// 登録逆順で Component::OnEndPlay を呼ぶ。
        virtual void OnEndPlay();

        [[nodiscard]] bool IsAlive() const noexcept { return m_alive; }
        /// 次フレーム以降 Scene 側で安全に reap される予定の印。
        void MarkPendingKill() noexcept { m_alive = false; }

    private:
        Transform m_root;
        std::vector<Component*> m_components;
        std::vector<GameObject*> m_children;
        GameObject* m_parent = nullptr;
        NS::App::Scene* m_scene = nullptr;
        bool m_alive = true;

        void DetachFromParent() noexcept;
    };

} // namespace NS::Scene
