#pragma once

#include "NSlib/Core/NonCopyable.h"

namespace NS
{
    namespace Editor
    {
        /// フレーム統計と、溜めた警告とエラーのログを出すコンソールパネル
        class ConsolePanel : public NonCopyable
        {
        public:
            /// 毎秒の描画回数・フレーム時間・固定ステップと、Logger::RecentWarnings の一覧を新しい順に表示する
            void Render() noexcept;
        };
    } // namespace Editor
} // namespace NS
