#pragma once

// マクロで NS_LOG_FATAL を使うので、勝手に include が消されないようにする
#include "NSlib/Core/Logger.h" // IWYU pragma: keep

#if defined(NS_ENABLE_ASSERT) && NS_ENABLE_ASSERT

/// 条件が偽なら Fatal ログを出して停止する。無効な構成では評価しないため、条件式に副作用を含めない
#define NS_ASSERT(cat, cond, ...)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            NS_LOG_FATAL(cat, __VA_ARGS__);                                                                            \
        }                                                                                                              \
    }                                                                                                                  \
    while (false)

#else

#define NS_ASSERT(cat, cond, ...) ((void)0)

#endif
