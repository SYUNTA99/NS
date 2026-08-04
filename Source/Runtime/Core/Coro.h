#pragma once

#include <coroutine>
#include <cstddef>
#include <exception>
#include <functional>
#include <utility>
#include <vector>

namespace NS::Core
{
    /// @brief 途中で止まり、次のフレームに続きから再開できる関数。演出手順を co_await の待ちを挟んで上から下へ書く
    /// @details CoroRunner が所有と駆動を持つ。待ちをまたいで生ポインタを持たないこと
    /// (待っている間に持ち主が破棄され得る)。破棄はフレームごと捨てる方式で、途中再開はしない
    class Coro
    {
    public:
        struct promise_type
        {
            float waitSeconds = 0.0f;            // 残り待ち秒。正の間は眠る
            std::function<bool()> waitPredicate; // 条件待ち。真を返したら起きる
            bool waitNextFrame = false;          // 次の Tick まで眠る印

            Coro get_return_object() { return Coro{std::coroutine_handle<promise_type>::from_promise(*this)}; }
            std::suspend_always initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }
            void return_void() noexcept {}
            void unhandled_exception() { std::terminate(); }
        };

        using Handle = std::coroutine_handle<promise_type>;

        Coro() = default;
        explicit Coro(Handle handle) noexcept : m_handle(handle) {}
        ~Coro()
        {
            if (m_handle)
                m_handle.destroy();
        }

        Coro(const Coro&) = delete;
        Coro& operator=(const Coro&) = delete;
        Coro(Coro&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
        Coro& operator=(Coro&& other) noexcept
        {
            if (this != &other)
            {
                if (m_handle)
                    m_handle.destroy();
                m_handle = std::exchange(other.m_handle, {});
            }
            return *this;
        }

        /// 所有を手放して生ハンドルを返す。CoroRunner::Start だけが使う
        [[nodiscard]] Handle Release() noexcept { return std::exchange(m_handle, {}); }

    private:
        Handle m_handle{};
    };

    /// @brief 指定秒だけ眠る待ち。時間は駆動側が Tick へ渡す dt で進む
    struct WaitSeconds
    {
        float seconds = 0.0f;

        [[nodiscard]] bool await_ready() const noexcept { return seconds <= 0.0f; }
        void await_suspend(std::coroutine_handle<Coro::promise_type> handle) const
        {
            handle.promise().waitSeconds = seconds;
        }
        void await_resume() const noexcept {}
    };

    /// @brief 条件が真になるまで眠る待ち。条件は毎 Tick 評価される
    struct WaitUntil
    {
        std::function<bool()> condition;

        [[nodiscard]] bool await_ready() const
        {
            // 条件が空か最初から真なら待たずに続ける
            return condition == nullptr || condition();
        }
        void await_suspend(std::coroutine_handle<Coro::promise_type> handle)
        {
            handle.promise().waitPredicate = std::move(condition);
        }
        void await_resume() const noexcept {}
    };

    /// @brief 次の Tick まで眠る待ち
    struct WaitNextFrame
    {
        [[nodiscard]] bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<Coro::promise_type> handle) const noexcept
        {
            handle.promise().waitNextFrame = true;
        }
        void await_resume() const noexcept {}
    };

    /// @brief 台本の実行器。所有する台本の待ちを進め、明けた物を再開する
    /// @details いつ Tick するかは持ち主が決める。止めている間は全台本が止まる (pause はこれで効く)
    /// CancelAll は台本を途中のまま破棄する (フレーム内の破棄処理は走る)。破棄後の再開は無い
    class CoroRunner
    {
    public:
        CoroRunner() = default;
        ~CoroRunner() { CancelAll(); }

        CoroRunner(const CoroRunner&) = delete;
        CoroRunner& operator=(const CoroRunner&) = delete;
        CoroRunner(CoroRunner&&) = delete;
        CoroRunner& operator=(CoroRunner&&) = delete;

        /// 台本を先頭から最初の待ちまで即時に走らせ、続きを預かる。待ち無しで終わればその場で捨てる
        void Start(Coro&& coro)
        {
            Coro::Handle handle = coro.Release();
            if (!handle)
                return;
            handle.resume();
            if (handle.done())
            {
                handle.destroy();
                return;
            }
            m_handles.push_back(handle);
        }

        /// 待ちを dt だけ進め、明けた台本を再開する。終わった台本はここで捨てる
        void Tick(float dt)
        {
            for (std::size_t i = 0; i < m_handles.size();)
            {
                Coro::Handle handle = m_handles[i];
                Coro::promise_type& promise = handle.promise();

                bool ready = false;
                if (promise.waitNextFrame)
                {
                    promise.waitNextFrame = false;
                    ready = true;
                }
                else if (promise.waitPredicate != nullptr)
                {
                    if (promise.waitPredicate())
                    {
                        promise.waitPredicate = nullptr;
                        ready = true;
                    }
                }
                else if (promise.waitSeconds > 0.0f)
                {
                    promise.waitSeconds -= dt;
                    ready = promise.waitSeconds <= 0.0f;
                }
                else
                {
                    ready = true;
                }

                if (!ready)
                {
                    ++i;
                    continue;
                }
                handle.resume();
                if (handle.done())
                {
                    handle.destroy();
                    m_handles.erase(m_handles.begin() + static_cast<std::ptrdiff_t>(i));
                    continue;
                }
                ++i;
            }
        }

        /// 全台本を途中のまま破棄する。モード離脱やレベル破棄の前に呼ぶ
        void CancelAll() noexcept
        {
            for (Coro::Handle handle : m_handles)
                handle.destroy();
            m_handles.clear();
        }

        [[nodiscard]] bool IsRunning() const noexcept { return !m_handles.empty(); }

    private:
        std::vector<Coro::Handle> m_handles; // 進行中の台本。done になったら即座に外す
    };

} // namespace NS::Core
