#pragma once

/// @file component.h
/// @brief ns::scene::Component — 振る舞いを表現する再利用ブロック (, )。
///
/// GameObject 派生 (Player/Block/Camera 等) に固定スロット (named members) として
/// 値型 or std::unique_ptr で保有される。Component 自身は所有者 GameObject を raw 参照する。
/// Component 間 / cross-GameObject アクセスは ctor 経由の明示的 raw pointer 注入のみ許可
/// (GetComponent<T>() 動的検索 API は提供しない)。
///
/// Lifecycle ():
///   - OnStart() — Scene attach 直後に 1 回
///   - OnUpdate(float dt) — fixed step 内で毎回 (`IsActive()==false` で skip)
///   - OnEndPlay() — Scene 破棄 / Component 廃棄前に 1 回

namespace ns::scene
{
    class GameObject;
    class Transform;

    /// 全 Component の基底。pure virtual を持たないため直接 instance も可能だが
    /// 通常は派生して使う。
    class Component
    {
    public:
        Component() noexcept = default;
        virtual ~Component() noexcept = default;

        Component(const Component&) = delete;
        Component& operator=(const Component&) = delete;
        Component(Component&&) = delete;
        Component& operator=(Component&&) = delete;

        /// 所有 GameObject。Scene attach 後は non-null。
        [[nodiscard]] GameObject* Owner() noexcept { return m_owner; }
        [[nodiscard]] const GameObject* Owner() const noexcept { return m_owner; }

        /// 所有 GameObject の root Transform への short-cut。
        /// 関数名が型名と衝突しないよう RootTransform 命名 (C++ の hidden type rule 回避)。
        [[nodiscard]] Transform& RootTransform() noexcept;
        [[nodiscard]] const Transform& RootTransform() const noexcept;

        [[nodiscard]] bool IsActive() const noexcept { return m_active; }
        void SetActive(bool active) noexcept { m_active = active; }

        virtual void OnStart() {}
        virtual void OnUpdate(float dt) { (void)dt; }
        virtual void OnEndPlay() {}

    private:
        friend class GameObject;
        void AttachOwner(GameObject* owner) noexcept { m_owner = owner; }

        GameObject* m_owner = nullptr;
        bool m_active = true;
    };

} // namespace ns::scene
