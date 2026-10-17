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
        //! 購読を足す。空の関数は捨てる
        void Subscribe(std::function<void()> callback)
        {
            if (!callback)
            {
                return;
            }
            m_callbacks.push_back(std::move(callback));
        }

        //! 購読を順に呼ぶ。購読が無ければ何もしない
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

    //! 身体の接地状態が切り替わったときの通知
    struct BodyEvents
    {
        BodyEvent onGroundEnter; //!< 着地したフレーム
        BodyEvent onGroundExit;  //!< 足場から離れたフレーム
    };
} // namespace NS
