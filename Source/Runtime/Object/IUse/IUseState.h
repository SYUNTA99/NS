#pragma once

#include <cstdint>

namespace NS::Obj
{
    //! 状態の型ごとに 1 つの印。中身は型ごとの静的変数の番地で、nullptr は状態が無い印
    using StateId = const void*;

    namespace detail
    {
        template <typename TState> struct StateTag
        {
            static constexpr char k_Marker = 0;
        };
    } // namespace detail

    //! @brief 状態の型 TState の印を返す
    template <typename TState> [[nodiscard]] constexpr StateId StateIdOf() noexcept
    {
        return &detail::StateTag<TState>::k_Marker;
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
        [[nodiscard]] virtual std::uint32_t StateStep() const noexcept = 0;
        //! @brief 印 id の状態へ移る予約をする
        //! @param[in] id 移る先の状態の印
        //! @return 予約できたか既にその状態の場合 true、それ以外の場合は false
        virtual bool Change(StateId id) = 0;
        //! @brief 今の状態を 1 フレーム進める
        //! @details 組む時に控えた所有者で進めるので、所有者の型を知らない基底から呼べる。組んでいなければ何もしない
        //! @param[in] dt 進める秒数
        virtual void Step(float dt) = 0;
    };

    //! @brief 自分の状態機械を返す口。状態機械を持つ物が実装する
    //! @details SetState / IsState / IsFirstStep / StateStep は IUseState だけを受け取る
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
    template <typename TState> bool SetState(IUseState& user)
    {
        IStateMachine* machine = user.GetStateMachine();
        return machine != nullptr && machine->Change(StateIdOf<TState>());
    }

    //! user の今の状態が TState の場合 true、それ以外の場合は false
    template <typename TState> [[nodiscard]] bool IsState(const IUseState& user) noexcept
    {
        const IStateMachine* machine = user.GetStateMachine();
        return machine != nullptr && machine->CurrentId() == StateIdOf<TState>();
    }

    //! user が今の状態に入った最初のフレームの場合 true、それ以外の場合は false
    [[nodiscard]] inline bool IsFirstStep(const IUseState& user) noexcept
    {
        const IStateMachine* machine = user.GetStateMachine();
        return machine != nullptr && machine->CurrentId() != nullptr && machine->StateStep() == 0;
    }

    //! @brief user が今の状態に入ってから進めたフレーム数を返す
    //! @return 状態機械を持たなければ 0
    [[nodiscard]] inline std::uint32_t StateStep(const IUseState& user) noexcept
    {
        const IStateMachine* machine = user.GetStateMachine();
        if (machine == nullptr)
        {
            return 0;
        }
        return machine->StateStep();
    }
} // namespace NS::Obj
