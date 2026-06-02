#pragma once

/// @file Component.h
/// @brief NS::Scene::Component — 振る舞いを表現する再利用ブロック
///
/// GameObject 派生 (Player/Block/Camera 等) に固定スロット (named members) として
/// 値型 or std::unique_ptr で保有される。Component 自身は所有者 GameObject を raw 参照する
/// Component 間 / cross-GameObject アクセスはコンストラクタ経由の明示的 raw pointer 注入のみ許可
/// (GetComponent<T>() 動的検索 API は提供しない)
///
/// Lifecycle:
///   - OnStart() — Scene attach 直後に 1 回
///   - OnUpdate() — fixed step 内で毎回 (`IsActive()==false` で skip)
///     dt は `NS::Core::FrameTimer::FixedDelta()` で取得 (all-static、 Application 不要)
///   - OnEndPlay() — Scene 破棄 / Component 廃棄前に 1 回

namespace NS::Scene
{
    class GameObject;
    class Transform;

    /// Component の OnUpdate 実行順序を制御する priority 帯
    /// 値が小さいほど先に呼ばれる。 同 priority 内は登録順 (stable_sort)
    /// 帯は意味的にグルーピング (Input 系 / Physics 系等)、 中間値で挟み込み可
    enum class TickPriority : int
    {
        Input = 0,       ///< 入力読取 (PlayerInputComponent 等)
        AI = 100,        ///< AI / state machine (将来 Enemy 用)
        Physics = 200,   ///< 物理 / movement (CharacterMovementComponent 等) — Component default
        Animation = 300, ///< animation / 補間 (将来 SkeletalAnim 用)
        Camera = 400,    ///< Camera follow / transform (ThirdPersonFollowComponent 等)
    };

    /// 全 Component の基底。pure virtual を持たないため直接 instance も可能だが
    /// 通常は派生して使う
    class Component
    {
    public:
        /// 所有 GameObject を受け取って auto-register するコンストラクタ
        /// `owner == nullptr` でも null-safe (登録なし、 後で AttachOwner で手動 attach 可)
        /// 通常は `Component(this, static_cast<int>(TickPriority::X))` のように派生クラスの
        /// コンストラクタから呼ぶ。 priority は data としてコンストラクタで確定するため、 base コンストラクタ内 sort
        /// 時の virtual dispatch 問題 (C++ vtable がまだ derived を指していない) を回避する
        explicit Component(GameObject* owner, int priority = static_cast<int>(TickPriority::Physics)) noexcept;

        virtual ~Component() noexcept;

        Component(const Component&) = delete;
        Component& operator=(const Component&) = delete;
        Component(Component&&) = delete;
        Component& operator=(Component&&) = delete;

        /// OnUpdate iteration 順を決める priority 帯。 値小→先呼出、 stable sort で同値保持
        /// 既定 `TickPriority::Physics` (200) — 物理 / movement 帯
        /// 派生はコンストラクタの base init で `Component(owner, (int)TickPriority::X)` を渡す
        /// (override ではなく data 注入)
        [[nodiscard]] int Priority() const noexcept { return m_priority; }

        /// 所有 GameObject。Scene attach 後は non-null
        [[nodiscard]] GameObject* Owner() noexcept { return m_owner; }
        [[nodiscard]] const GameObject* Owner() const noexcept { return m_owner; }

        /// 所有 GameObject の root Transform への short-cut
        /// 関数名が型名と衝突しないよう RootTransform 命名 (C++ の hidden type rule 回避)
        [[nodiscard]] Transform& RootTransform() noexcept;
        [[nodiscard]] const Transform& RootTransform() const noexcept;

        [[nodiscard]] bool IsActive() const noexcept { return m_active; }
        void SetActive(bool active) noexcept { m_active = active; }

        virtual void OnStart() {}
        virtual void OnUpdate() {}
        virtual void OnEndPlay() {}

    private:
        friend class GameObject;
        void AttachOwner(GameObject* owner) noexcept { m_owner = owner; }

        GameObject* m_owner = nullptr;
        int m_priority = static_cast<int>(TickPriority::Physics);
        bool m_active = true;
    };

} // namespace NS::Scene
