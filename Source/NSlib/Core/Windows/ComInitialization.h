#pragma once

#include "NSlib/Core/NonCopyable.h"

namespace NS
{
    namespace OS
    {
        //! @brief 作ったスレッドで COM を初期化し、壊す時に初期化を 1 回戻す
        //! @details 初期化済みのスレッドで作っても成功の扱いになり、入れ子で作れる。
        //!          初期化に失敗した時は IsValid() が false で、壊す時に何も戻さない
        class ComInitialization : private NonCopyable
        {
        public:
            ComInitialization() noexcept;
            ~ComInitialization();

            //! COM の初期化に成功したか
            [[nodiscard]] bool IsValid() const noexcept
            {
                return m_initialized;
            }

        private:
            bool m_initialized = false;
        };
    } // namespace OS
} // namespace NS
