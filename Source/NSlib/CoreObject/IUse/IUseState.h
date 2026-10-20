#pragma once

#include "NSlib/Core/Assert.h"

#include "NSlib/Core/Types.h"

namespace NS
{
    //! 状態の型ごとに 1 つの印。中身は型ごとの静的変数の番地で、nullptr は状態が無い印
    using StateId = const void*;

    template <typename TState> struct StateTag
    {
        static constexpr char k_Marker = 0;
    };

    //! @brief 状態の型 TState の印を返す
    template <typename TState> [[nodiscard]] constexpr StateId StateIdOf() noexcept
    {
        return &StateTag<TState>::k_Marker;
    }

    //! @brief 所有者の型を問わずに状態機械を問い、移す口
    class IStateMachine
    {
    public:
        virtual ~IStateMachine() = default;
        //! @brief 今の状態の印を返す
        //! @return 移る予約があれば移る先の印。組んでいなければ nullptr
        [[nodiscard]] virtual StateId CurrentId() const noexcept = 0;
        //! @brief 今の状態に入ってから進めたフレーム数を返す
        //! @return 入った最初のフレームは 0
        [[nodiscard]] virtual uint32 StepsInState() const noexcept = 0;
        //! 状態遷移を予約する。予約できたか同じ状態なら true
        virtual bool Change(StateId id) = 0;
        //! 組み立て時の持ち主で dt 秒進める。未組立なら何もしない
        virtual void Step(float dt) = 0;
    };

    //! @brief 自分の状態機械を返す口。状態機械を持つ物が実装する
    //! @details SetState / IsState / IsFirstStep / GetStateStep は IUseState だけを受け取る
    class IUseState
    {
    public:
        //! @brief 自分の状態機械を返す
        //! @return 状態機械を持たなければ nullptr
        [[nodiscard]] virtual IStateMachine* GetStateMachine() noexcept = 0;
        //! @brief 自分の状態機械を返す
        //! @return 状態機械を持たなければ nullptr
        [[nodiscard]] virtual const IStateMachine* GetStateMachine() const noexcept = 0;

    protected:
        ~IUseState() = default;
    };

    //! @brief user の状態機械を TState へ移す予約をする
    //! @return 予約できたか既に TState の場合 true、それ以外の場合は false
    template <typename TState> bool SetState(IUseState* user)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        IStateMachine* machine = user->GetStateMachine();
        return machine != nullptr && machine->Change(StateIdOf<TState>());
    }

    //! user の今の状態が TState の場合 true、それ以外の場合は false
    template <typename TState> [[nodiscard]] bool IsState(const IUseState* user) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const IStateMachine* machine = user->GetStateMachine();
        return machine != nullptr && machine->CurrentId() == StateIdOf<TState>();
    }

    //! user が今の状態に入った最初のフレームの場合 true、それ以外の場合は false
    [[nodiscard]] inline bool IsFirstStep(const IUseState* user) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const IStateMachine* machine = user->GetStateMachine();
        return machine != nullptr && machine->CurrentId() != nullptr && machine->StepsInState() == 0;
    }

    //! @brief user が今の状態に入ってから進めたフレーム数を返す
    //! @return 状態機械を持たなければ 0
    [[nodiscard]] inline uint32 GetStateStep(const IUseState* user) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        const IStateMachine* machine = user->GetStateMachine();
        if (machine == nullptr)
        {
            return 0;
        }
        return machine->StepsInState();
    }
} // namespace NS
