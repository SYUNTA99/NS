#pragma once

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Coroutine.h"
#include "NSlib/Object/IUse/IUseState.h"

#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace NS::Obj
{
    //! @brief 状態機械の 1 状態。素のクラスで、所有者の能力を呼ぶ判断と遷移だけを書く
    //! @details 調整値や続きのデータは所有者側に置き、状態は持たない。どの状態からでも同じ記録を読める
    //! 直接派生させず StateOf を挟む。Id はそちらが埋める
    template <typename TOwner> class State
    {
    public:
        State() = default;
        virtual ~State() = default;

        State(const State&) = delete;
        State& operator=(const State&) = delete;
        State(State&&) = delete;
        State& operator=(State&&) = delete;

        //! 自分の型を指す印。Change の突き合わせに使う
        [[nodiscard]] virtual StateId Id() const noexcept = 0;

        //! 状態に入った時に 1 回呼ばれる。Build の先頭と、予約の確定の時
        virtual void OnEnter(TOwner&) {}
        //! @brief 状態の中で 1 フレーム進める
        //! @details 同じフレームのコルーチンの後に呼ばれる。コルーチンが移る予約をしたフレームは呼ばれない
        virtual void OnStep(TOwner& owner, float dt) = 0;
        //! 他の状態へ移る予約が入った時に 1 回呼ばれる
        virtual void OnExit(TOwner&) {}

    protected:
        void StartCoroutine(NS::Coroutine&& coroutine) { m_coroutines.Start(std::move(coroutine)); }
        [[nodiscard]] float StepDelta() const noexcept { return m_stepDelta; }

    private:
        template <typename> friend class StateMachine;
        void TickCoroutines(float dt)
        {
            m_stepDelta = dt;
            m_coroutines.Tick(dt);
        }
        void CancelCoroutines() noexcept { m_coroutines.CancelAll(); }

        NS::CoroutineRunner m_coroutines;
        float m_stepDelta = 0.0f;
    };

    //! @brief 状態の印を埋める中間クラス。状態は StateOf<自分の型, 所有者> から派生する
    //! @details Id を派生に書かせると、状態を複製した時に型名を直し忘れてもビルドが通り、印が元の状態と重なる
    template <typename TState, typename TOwner> class StateOf : public State<TOwner>
    {
    public:
        [[nodiscard]] StateId Id() const noexcept final { return StateIdOf<TState>(); }
    };

    //! @brief 型の並びから組む状態機械。先頭が初期状態で、Build 時に OnEnter する
    //! @details 所有者は Build で 1 回だけ受けて控え、Step と Change はその控えを状態へ渡す。
    //! OnStep の中で Change しても呼び出し元の OnStep は続くので、遷移したら即 return する
    template <typename TOwner> class StateMachine : public IStateMachine
    {
    public:
        //! @brief 状態の型の並びから状態列を組み、先頭へ入る
        //! @details 並びに書いた型が、この機械の持てる状態の全部になる
        //! @param[in] owner 状態へ渡す所有者。控えて Step と Change でも使う
        template <typename... TStates> void Build(TOwner& owner)
        {
            static_assert(sizeof...(TStates) > 0, "状態を 1 つ以上並べる");
            static_assert((std::is_base_of_v<StateOf<TStates, TOwner>, TStates> && ...),
                          "状態は StateOf<自分の型, 所有者> から派生する");

            if (m_current != nullptr)
            {
                m_current->CancelCoroutines();
            }
            m_next = nullptr;
            m_states.clear();
            m_states.reserve(sizeof...(TStates));
            (m_states.push_back(std::make_unique<TStates>()), ...);
            m_current = m_states.front().get();
            m_owner = &owner;
            m_step = 0;
            m_current->OnEnter(owner);
        }

        //! OnExit / OnEnter を呼ばずに先頭の状態へ戻す。やり直しで使う
        void Reset() noexcept
        {
            m_next = nullptr;
            if (m_current != nullptr)
            {
                m_current->CancelCoroutines();
            }
            if (m_states.empty())
            {
                m_current = nullptr;
            }
            else
            {
                m_current = m_states.front().get();
            }
            m_step = 0;
        }

        //! 状態を 1 つでも組めているか
        [[nodiscard]] bool IsBuilt() const noexcept { return m_current != nullptr; }

        //! 現在状態の印。未組立は nullptr
        [[nodiscard]] StateId CurrentId() const noexcept override
        {
            if (m_next != nullptr)
            {
                return m_next->Id();
            }
            if (m_current == nullptr)
            {
                return nullptr;
            }
            return m_current->Id();
        }

        //! @brief 今の状態に入ってから進めたフレーム数を返す
        //! @return 入った最初のフレームは 0。移る予約があれば std::uint32_t の最大値
        [[nodiscard]] std::uint32_t StepsInState() const noexcept override
        {
            if (m_next != nullptr)
            {
                return static_cast<std::uint32_t>(-1);
            }
            return m_step;
        }
        //! 組めていて移る予約が無く、今の状態に入った最初のフレームの場合 true、それ以外の場合は false
        [[nodiscard]] bool IsFirstStep() const noexcept
        {
            return m_current != nullptr && m_next == nullptr && m_step == 0;
        }

        //! @brief 今の状態のコルーチンを捨て、移る予約を取り消す
        //! @details 走っている Step の中で呼ぶと、その Step は残りの OnStep と予約の確定を飛ばす
        void CancelCurrentCoroutines() noexcept
        {
            m_stepCancelled = true;
            m_next = nullptr;
            if (m_current != nullptr)
            {
                m_current->CancelCoroutines();
            }
        }

        //! 現在状態が TState の場合 true、それ以外の場合は false。未組立はどの型とも一致しない
        template <typename TState> [[nodiscard]] bool IsCurrent() const noexcept
        {
            return CurrentId() == StateIdOf<TState>();
        }

        //! @brief Build で控えた所有者で、現在状態の OnStep を 1 回呼ぶ
        //! @details 未組立は何もしない
        void Step(float dt) override
        {
            if (m_owner == nullptr)
            {
                return;
            }
            m_stepCancelled = false;
            CommitChange();
            if (m_current != nullptr && m_next == nullptr && !m_stepCancelled)
            {
                State<TOwner>* stepped = m_current;
                stepped->TickCoroutines(dt);
                if (m_current == stepped && m_next == nullptr && !m_stepCancelled)
                {
                    stepped->OnStep(*m_owner, dt);
                    if (m_current == stepped && m_next == nullptr && !m_stepCancelled)
                    {
                        ++m_step;
                    }
                }
            }
            if (!m_stepCancelled)
            {
                CommitChange();
            }
        }

        //! @brief 印 id の状態へ移る予約をする。確定は次の Step
        //! @details 予約を入れた時に今の状態の OnExit を呼び、コルーチンを捨てる。予約の上書きでは呼ばない。
        //! OnExit と確定の時の OnEnter へは Build で控えた所有者を渡す
        //! 組み立て済みで Build に並べていない状態はアサートで止める
        //! @param[in] id 移る先の状態の印
        //! @return 予約できたか既にその状態の場合 true、未組立か Build に並べていない状態の場合 false
        bool Change(StateId id) override
        {
            State<TOwner>* next = Find(id);
            // 組み立て済みで見つからないのは Build への並べ忘れ。黙って断ると遷移しないまま動き続ける
            NS_ASSERT(
                Scene, m_owner == nullptr || next != nullptr, "StateMachine: Build に並べていない状態へ移ろうとした");
            if (next == nullptr)
            {
                return false;
            }
            if (next == m_next || (m_next == nullptr && next == m_current))
            {
                return true;
            }

            const bool alreadyPending = m_next != nullptr;
            m_next = next;
            if (!alreadyPending && m_current != nullptr)
            {
                m_current->OnExit(*m_owner);
                m_current->CancelCoroutines();
            }
            return true;
        }

        //! 状態 TState へ移る予約をする。返す値は Change(StateId) と同じ
        template <typename TState> bool Change() { return Change(StateIdOf<TState>()); }

    private:
        void CommitChange()
        {
            if (m_next == nullptr)
            {
                return;
            }
            m_current = m_next;
            m_next = nullptr;
            m_step = 0;
            m_current->OnEnter(*m_owner);
        }

        [[nodiscard]] State<TOwner>* Find(StateId id) noexcept
        {
            for (const std::unique_ptr<State<TOwner>>& state : m_states)
            {
                if (state->Id() == id)
                {
                    return state.get();
                }
            }
            return nullptr;
        }

        std::vector<std::unique_ptr<State<TOwner>>> m_states; // 並びは Build に並べた順。先頭が初期状態
        State<TOwner>* m_current = nullptr;
        State<TOwner>* m_next = nullptr;
        TOwner* m_owner = nullptr; // 書くのは Build だけ。未組立は nullptr
        std::uint32_t m_step = 0;
        bool m_stepCancelled = false;
    };

    //! @brief 親の状態の中で進める子の状態機械
    //! @details 子の状態が Finish を呼んで終わり、親は IsDead を見て次へ移る
    template <typename TOwner> class SubStateMachine : public IUseState
    {
    public:
        //! @brief 状態の型の並びから組み直し、先頭へ入る。終わった印も下ろす
        template <typename... TStates> void Build(TOwner& owner)
        {
            m_dead = false;
            m_machine.template Build<TStates...>(owner);
        }

        //! 終わっていなければ、Build で控えた所有者と今の状態で 1 フレーム進める
        void Step(float dt)
        {
            if (!m_dead)
            {
                m_machine.Step(dt);
            }
        }

        //! 終わった印を立て、今の状態のコルーチンと移る予約を捨てる
        void Finish() noexcept
        {
            m_dead = true;
            m_machine.CancelCurrentCoroutines();
        }
        //! Finish の後、次の Build までの場合 true、それ以外の場合は false
        [[nodiscard]] bool IsDead() const noexcept { return m_dead; }
        [[nodiscard]] StateMachine<TOwner>& Machine() noexcept { return m_machine; }
        [[nodiscard]] const StateMachine<TOwner>& Machine() const noexcept { return m_machine; }
        [[nodiscard]] IStateMachine* GetStateMachine() noexcept override { return &m_machine; }
        [[nodiscard]] const IStateMachine* GetStateMachine() const noexcept override { return &m_machine; }

    private:
        StateMachine<TOwner> m_machine;
        bool m_dead = false;
    };

} // namespace NS::Obj
