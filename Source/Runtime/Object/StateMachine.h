#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace NS::Object
{
    //! @brief 状態機械の 1 状態。素のクラスで、所有者の能力を呼ぶ判断と遷移だけを書く
    //! @details 調整値や続きのデータは所有者側に置き、状態は持たない。どの状態からでも同じ記録を読める
    //! 派生は登録名 static constexpr const char* k_Name を持ち、Name はそれを返す
    template <typename TOwner> class State
    {
    public:
        State() = default;
        virtual ~State() = default;

        State(const State&) = delete;
        State& operator=(const State&) = delete;
        State(State&&) = delete;
        State& operator=(State&&) = delete;

        //! 登録名。データ駆動の一覧と Change の指定に使う
        [[nodiscard]] virtual const char* Name() const noexcept = 0;

        virtual void OnEnter(TOwner&) {}
        virtual void OnStep(TOwner& owner, float dt) = 0;
        virtual void OnExit(TOwner&) {}
    };

    //! @brief 所有者型ごとの状態の登録一覧。名前 → 生成の対応を自己登録 (NS_STATE) で集める
    template <typename TOwner> class StateRegistry
    {
    public:
        using Factory = std::unique_ptr<State<TOwner>> (*)();

        static bool Register(std::string name, Factory factory)
        {
            Map().emplace(std::move(name), factory);
            return true;
        }

        //! 登録名から状態を作る。未登録は nullptr
        [[nodiscard]] static std::unique_ptr<State<TOwner>> Create(std::string_view name)
        {
            const auto& map = Map();
            const auto it = map.find(name);
            if (it == map.end())
                return nullptr;
            return it->second();
        }

    private:
        static std::map<std::string, Factory, std::less<>>& Map()
        {
            static std::map<std::string, Factory, std::less<>> s_map;
            return s_map;
        }
    };

    //! @brief 名前の並びから組む状態機械。先頭が初期状態で、Build 時に OnEnter する
    //! @details 遷移は Change が今の状態の OnExit → 次の OnEnter を即時に呼ぶ
    //! OnStep の中で Change した状態のコードへ戻らないよう、呼び出し側は遷移したら return すること
    template <typename TOwner> class StateMachine
    {
    public:
        //! 登録名の並びから状態列を組み、先頭へ入る。未登録名は飛ばし、1 つでも飛ばしたら false
        bool Build(TOwner& owner, const std::vector<std::string>& names)
        {
            m_states.clear();
            m_current = nullptr;
            bool all = true;
            for (const std::string& name : names)
            {
                std::unique_ptr<State<TOwner>> state = StateRegistry<TOwner>::Create(name);
                if (state == nullptr)
                {
                    all = false;
                    continue;
                }
                m_states.push_back(std::move(state));
            }
            if (!m_states.empty())
            {
                m_current = m_states.front().get();
                m_current->OnEnter(owner);
            }
            if (m_states.empty())
                all = false;
            return all;
        }

        //! OnExit / OnEnter を呼ばずに初期状態 (先頭) へ戻す。リスポーン等のハードリセット用
        void Reset() noexcept
        {
            if (m_states.empty())
                m_current = nullptr;
            else
                m_current = m_states.front().get();
        }

        [[nodiscard]] bool IsBuilt() const noexcept { return m_current != nullptr; }

        //! 現在状態の登録名。未組立は空文字
        [[nodiscard]] const char* CurrentName() const noexcept
        {
            if (m_current == nullptr)
                return "";
            return m_current->Name();
        }

        //! 現在状態で 1 歩進める。未組立は何もしない
        void Step(TOwner& owner, float dt)
        {
            if (m_current != nullptr)
                m_current->OnStep(owner, dt);
        }

        //! 名前の状態へ移る。今の OnExit → 次の OnEnter を即時に呼ぶ。同じ状態へは何もせず true、
        //! 未知名と未組立は何もせず false
        bool Change(TOwner& owner, std::string_view name)
        {
            State<TOwner>* next = Find(name);
            if (next == nullptr)
                return false;
            if (next == m_current)
                return true;
            if (m_current != nullptr)
                m_current->OnExit(owner);
            m_current = next;
            m_current->OnEnter(owner);
            return true;
        }

        template <typename TState> bool Change(TOwner& owner) { return Change(owner, TState::k_Name); }

    private:
        [[nodiscard]] State<TOwner>* Find(std::string_view name) noexcept
        {
            for (const std::unique_ptr<State<TOwner>>& state : m_states)
            {
                if (name == state->Name())
                    return state.get();
            }
            return nullptr;
        }

        std::vector<std::unique_ptr<State<TOwner>>> m_states; // 並びはデータの並びのまま。先頭が初期状態
        State<TOwner>* m_current = nullptr;                   // 現在状態。未組立は nullptr
    };

} // namespace NS::Object

//! 状態を一覧へ登録する。状態クラスを定義した cpp の namespace スコープに置く
#define NS_STATE(StateClass, OwnerClass)                                                                               \
    namespace                                                                                                          \
    {                                                                                                                  \
        [[maybe_unused]] const bool g_stateRegistered##StateClass = ::NS::Object::StateRegistry<OwnerClass>::Register( \
            StateClass::k_Name,                                                                                        \
            []() -> std::unique_ptr<::NS::Object::State<OwnerClass>> { return std::make_unique<StateClass>(); });      \
    }
