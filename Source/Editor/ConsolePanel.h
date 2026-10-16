#pragma once

#include "NSlib/Core/NonCopyable.h"

namespace NS
{
    namespace Editor
    {
        //! @brief フレーム統計を出すコンソールパネル
        class ConsolePanel : public NonCopyable
        {
        public:
            //! 毎秒の描画回数・フレーム時間・固定ステップを表示する
            void Render() noexcept;
        };
    } // namespace Editor
} // namespace NS
