#pragma once

/// @file Component.h
/// @brief NS::Scene::Component — 振る舞いを表現する再利用ブロック
///
/// GameObject::AddComponent<T>() で生成され、 GameObject が unique_ptr で寿命を所有する
/// Component 自身は所有者 GameObject を生参照する (owner は生成後に GameObject が注入)
/// Component 間 / 他 GameObject へのアクセスはコンストラクタ経由の明示的な生ポインタ注入のみ許可
/// (GetComponent<T>() 動的検索 API は提供しない)
///
/// Lifecycle:
///   - OnStart() — Scene attach 直後に 1 回
///   - OnUpdate() — fixed step 内で毎回 (`IsActive()==false` で skip)
///     dt は `NS::Core::FrameTimer::FixedDelta()` で取得 (all-static、 Application 不要)
///   - OnEndPlay() — Scene 破棄 / Component 廃棄前に 1 回

#include "Framework/Scene/Reflection.h"

namespace NS::Scene
{
    class GameObject;
    class Transform;

    /// OnUpdate 実行順を制御する priority 帯。値が小さいほど先、同 priority 内は登録順
    enum class TickPriority : int
    {
        Input = 0,       ///< 入力読取 (PlayerInputComponent 等)
        AI = 100,        ///< AI / state machine (将来 Enemy 用)
        Physics = 200,   ///< 物理 / movement (CharacterMovementComponent 等) — Component default
        Animation = 300, ///< animation / 補間 (将来 SkeletalAnim 用)
        Camera = 400,    ///< Camera follow / transform (ThirdPersonFollowComponent 等)
    };

    /// 全 Component の基底。通常は派生して使う
    class Component
    {
    public:
        /// priority を ctor 引数で確定する。base ctor 内の virtual dispatch を避けるため (vtable 未確定)
        explicit Component(int priority = static_cast<int>(TickPriority::Physics)) noexcept;

        virtual ~Component() noexcept;

        Component(const Component&) = delete;
        Component& operator=(const Component&) = delete;
        Component(Component&&) = delete;
        Component& operator=(Component&&) = delete;

        /// OnUpdate 実行順の priority。既定 `TickPriority::Physics` (200)
        [[nodiscard]] int Priority() const noexcept { return m_priority; }

        /// 所有 GameObject。Scene attach 後は non-null
        [[nodiscard]] GameObject* Owner() noexcept { return m_owner; }
        [[nodiscard]] const GameObject* Owner() const noexcept { return m_owner; }

        /// 所有 GameObject の root Transform への short-cut。型名衝突回避のため RootTransform 命名
        [[nodiscard]] Transform& RootTransform() noexcept;
        [[nodiscard]] const Transform& RootTransform() const noexcept;

        [[nodiscard]] bool IsActive() const noexcept { return m_active; }
        void SetActive(bool active) noexcept { m_active = active; }

        virtual void OnStart() {}
        virtual void OnUpdate() {}
        virtual void OnEndPlay() {}

        /// このコンポーネント型の反射情報。未反射型は nullptr。エディタが Component* 越しに field を列挙する
        [[nodiscard]] virtual const ReflectionInfo* GetReflection() const noexcept { return nullptr; }

    private:
        // owner 注入は AddComponent 経由のみ。Component から GameObject の非公開メンバへはアクセスしない
        friend class GameObject;
        void AttachOwner(GameObject* owner) noexcept { m_owner = owner; }

        GameObject* m_owner = nullptr;
        int m_priority = static_cast<int>(TickPriority::Physics);
        bool m_active = true;
    };

} // namespace NS::Scene
