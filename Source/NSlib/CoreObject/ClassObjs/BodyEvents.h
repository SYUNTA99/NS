#pragma once

#include <functional>
#include <utility>
#include <vector>

namespace NS
{
    //! 引数のない通知。登録順に複数の購読先へ送る
    class BodyEvent
    {
    public:
        //! 空の関数は無視する
        void Subscribe(std::function<void()> callback)
        {
            if (!callback)
            {
                return;
            }
            m_callbacks.push_back(std::move(callback));
        }

        // TODO: 呼んでいる最中に Subscribe されると反復子が外れる。購読者が出た時に控えを取るか禁じるかを決める
        void Invoke() const noexcept
        {
            for (const std::function<void()>& callback : m_callbacks)
            {
                callback();
            }
        }

    private:
        std::vector<std::function<void()>> m_callbacks;
    };

    struct BodyEvents
    {
        BodyEvent onGroundEnter;
        BodyEvent onGroundExit;
    };
} // namespace NS
