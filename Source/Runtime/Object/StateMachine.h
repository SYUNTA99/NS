#pragma once

#include <memory>
#include <type_traits>
#include <vector>

namespace NS::Object
{
    //! 状態の型を指す印。型ごとに 1 つだけある静的な変数の番地で、実行時型情報を使わずに型を見分ける
    using StateId = const void*;

    namespace detail
    {
        //! 型ごとに番地を 1 つ作るための入れ物。値そのものは読まない
        template <typename TState> struct StateTag
        {
            static constexpr char k_Marker = 0;
        };
    } // namespace detail

    //! 状態の型から印を得る
    template <typename TState> [[nodiscard]] constexpr StateId StateIdOf() noexcept
    {
        return &detail::StateTag<TState>::k_Marker;
    }

    //! @brief 状態機械の 1 状態。素のクラスで、所有者の能力を呼ぶ判断と遷移だけを書く
    //! @details 調整値や続きのデータは所有者側に置き、状態は持たない。どの状態からでも同じ記録を読める
    //! 直接派生させず StateOf を挟む。Id と Name はそちらが埋める
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

        //! ログと試験の失敗文に出す表示名。遷移の指定には使わない
        [[nodiscard]] virtual const char* Name() const noexcept = 0;

        virtual void OnEnter(TOwner&) {}
        virtual void OnStep(TOwner& owner, float dt) = 0;
        virtual void OnExit(TOwner&) {}
    };

    //! @brief 状態の印と表示名を埋める中間クラス。状態は StateOf<自分の型, 所有者> から派生する
    //! @details 派生は表示名 static constexpr const char* k_Name を持つ
    //! Id と Name を派生に書かせると、状態を複製した時に型名を直し忘れてもビルドが通り、印が元の状態と重なる
    template <typename TState, typename TOwner> class StateOf : public State<TOwner>
    {
    public:
        [[nodiscard]] StateId Id() const noexcept final { return StateIdOf<TState>(); }
        [[nodiscard]] const char* Name() const noexcept final { return TState::k_Name; }
    };

    //! @brief 型の並びから組む状態機械。先頭が初期状態で、Build 時に OnEnter する
    //! @details 遷移は Change が今の状態の OnExit → 次の OnEnter を即時に呼ぶ
    //! OnStep の中で Change しても呼び出し元の OnStep は続くので、遷移したら即 return する
    template <typename TOwner> class StateMachine
    {
    public:
        //! @brief 状態の型の並びから状態列を組み、先頭へ入る
        //! @details 並びに書いた型が、この機械の持てる状態の全部になる
        //! @param[in] owner 先頭の OnEnter へ渡す所有者
        template <typename... TStates> void Build(TOwner& owner)
        {
            static_assert(sizeof...(TStates) > 0, "状態を 1 つ以上並べる");
            static_assert((std::is_base_of_v<StateOf<TStates, TOwner>, TStates> && ...),
                          "状態は StateOf<自分の型, 所有者> から派生する");

            m_states.clear();
            m_states.reserve(sizeof...(TStates));
            (m_states.push_back(std::make_unique<TStates>()), ...);
            m_current = m_states.front().get();
            m_current->OnEnter(owner);
        }

        //! OnExit / OnEnter を呼ばずに先頭の状態へ戻す。やり直しで使う
        void Reset() noexcept
        {
            if (m_states.empty())
            {
                m_current = nullptr;
            }
            else
            {
                m_current = m_states.front().get();
            }
        }

        //! 状態を 1 つでも組めているか
        [[nodiscard]] bool IsBuilt() const noexcept { return m_current != nullptr; }

        //! 現在状態の表示名。未組立は空文字
        [[nodiscard]] const char* CurrentName() const noexcept
        {
            if (m_current == nullptr)
            {
                return "";
            }
            return m_current->Name();
        }

        //! 現在状態の印。未組立は nullptr
        [[nodiscard]] StateId CurrentId() const noexcept
        {
            if (m_current == nullptr)
            {
                return nullptr;
            }
            return m_current->Id();
        }

        //! 現在状態が TState の場合 true、それ以外の場合は false。未組立はどの型とも一致しない
        template <typename TState> [[nodiscard]] bool IsCurrent() const noexcept
        {
            return CurrentId() == StateIdOf<TState>();
        }

        //! 現在状態の OnStep を 1 回呼ぶ。未組立は何もしない
        void Step(TOwner& owner, float dt)
        {
            if (m_current != nullptr)
            {
                m_current->OnStep(owner, dt);
            }
        }

        //! 印の状態へ移る。今の OnExit → 次の OnEnter を即時に呼ぶ。同じ状態へは何もせず true、
        //! 並びに無い状態と未組立は何もせず false
        bool Change(TOwner& owner, StateId id)
        {
            State<TOwner>* next = Find(id);
            if (next == nullptr)
            {
                return false;
            }
            if (next == m_current)
            {
                return true;
            }

            if (m_current != nullptr)
            {
                m_current->OnExit(owner);
            }

            m_current = next;
            m_current->OnEnter(owner);
            return true;
        }

        //! 状態 TState へ移る
        template <typename TState> bool Change(TOwner& owner) { return Change(owner, StateIdOf<TState>()); }

    private:
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
        State<TOwner>* m_current = nullptr;                   // 現在状態。未組立は nullptr
    };

} // namespace NS::Object
