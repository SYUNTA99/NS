#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace NS::Game::Entity
{
    //! @brief 引数の無い通知
    //! @details 購読は複数持てる。描画と音の部品が同じ通知を同時に受け取るため
    //! 呼ぶ順は購読を足した順
    class EntityEvent
    {
    public:
        //! 購読を足す。空の関数は捨てる
        void Subscribe(std::function<void()> callback)
        {
            if (!callback)
                return;
            m_callbacks.push_back(std::move(callback));
        }

        //! 購読を順に呼ぶ。購読が無ければ何もしない
        // TODO: 呼んでいる最中に Subscribe されると反復子が外れる。購読者が出た時に控えを取るか禁じるかを決める
        void Invoke() const noexcept
        {
            for (const std::function<void()>& callback : m_callbacks)
                callback();
        }

        [[nodiscard]] std::size_t SubscriberCount() const noexcept { return m_callbacks.size(); }

    private:
        std::vector<std::function<void()>> m_callbacks;
    };

    //! @brief 登場人物に共通する通知
    //! @details 敵も同じ物を持つ。レールの乗り降りは持たない。レールが無い
    struct EntityEvents
    {
        EntityEvent onGroundEnter; //!< 着地した歩
        EntityEvent onGroundExit;  //!< 足場から離れた歩
    };
} // namespace NS::Game::Entity
