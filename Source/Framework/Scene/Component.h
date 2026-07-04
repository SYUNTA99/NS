#pragma once

/// @file Component.h
/// @brief NS::Scene::Component — 振る舞いを表現する再利用ブロック
///
/// GameObject::AddComponent<T>() で生成され、 GameObject が unique_ptr で寿命を所有する
/// Component 自身は所有者 GameObject を生参照する。 owner は生成後に GameObject が注入する
/// 兄弟 Component への参照は OnStart で Owner()->FindComponent<T>() により解決し、
/// scene の service は OnStart で Owner()->OwningScene() 経由で借用する
///
/// ライフサイクル:
///   - OnStart() — Scene attach 直後に 1 回
///   - OnUpdate() — fixed step 内で毎回。 `IsActive()==false` なら skip する
///     dt は `NS::Core::FrameTimer::FixedDelta()` で取得する。 全て static なので Application 不要
///   - OnEndPlay() — Scene 破棄 / Component 廃棄前に 1 回

#include "Framework/Scene/Reflection.h"

namespace NS::Scene
{
    class GameObject;
    class Transform;

    /// OnUpdate 実行順を制御する priority 帯。値が小さいほど先、同 priority 内は登録順
    enum class TickPriority : int
    {
        /// 入力読取。PlayerInputComponent 等が使う
        Input = 0,
        /// AI / state machine。将来 Enemy 用
        AI = 100,
        /// 物理 / movement。Component の既定で CharacterMovementComponent 等が使う
        Physics = 200,
        /// animation / 補間。将来 SkeletalAnim 用
        Animation = 300,
        /// Camera follow / transform。ThirdPersonFollowComponent 等が使う
        Camera = 400,
    };

    /// 全 Component の基底。通常は派生して使う
    class Component
    {
    public:
        /// priority をコンストラクタ引数で確定する。基底コンストラクタ内は仮想関数テーブルが未確定なので
        /// 仮想呼び出しを避ける
        explicit Component(int priority = static_cast<int>(TickPriority::Physics)) noexcept;

        virtual ~Component() noexcept;

        Component(const Component&) = delete;
        Component& operator=(const Component&) = delete;
        Component(Component&&) = delete;
        Component& operator=(Component&&) = delete;

        /// OnUpdate 実行順の priority。既定は `TickPriority::Physics` の 200
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

        /// 自分の反射鎖に target が現れるか。反射照合による is-a 判定。target が nullptr なら常に false
        [[nodiscard]] bool IsA(const ReflectionInfo* target) const noexcept;

    private:
        // owner 注入は AddComponent 経由のみ。Component から GameObject の非公開メンバへはアクセスしない
        friend class GameObject;
        void AttachOwner(GameObject* owner) noexcept { m_owner = owner; }

        GameObject* m_owner = nullptr;
        int m_priority = static_cast<int>(TickPriority::Physics);
        bool m_active = true;
    };

    /// 反射照合で通れば static_cast、外れれば nullptr を返す型分岐の窓口。comp が nullptr でも安全
    template <class T> [[nodiscard]] T* ComponentCast(Component* comp) noexcept
    {
        if (comp != nullptr && comp->IsA(T::StaticReflection()))
            return static_cast<T*>(comp);
        return nullptr;
    }

    /// const 版。反射照合で通れば static_cast、外れれば nullptr
    template <class T> [[nodiscard]] const T* ComponentCast(const Component* comp) noexcept
    {
        if (comp != nullptr && comp->IsA(T::StaticReflection()))
            return static_cast<const T*>(comp);
        return nullptr;
    }

} // namespace NS::Scene
