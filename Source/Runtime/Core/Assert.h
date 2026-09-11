#pragma once

// マクロで NS_LOG_FATAL を使うので、勝手に include が消されないようにする
#include "Runtime/Core/Logger.h" // IWYU pragma: keep

#if defined(NS_ENABLE_ASSERT) && NS_ENABLE_ASSERT

//! @brief 条件が偽なら Fatal ログを出してプロセスを止める
//! @details アサートを無効にした構成ではマクロごと消えるので、条件式に処理を書かない
//! @param[in] cat ログのカテゴリ
//! @param[in] cond 偽の場合に停止する条件式
//! @param[in] ... 停止時に出力するメッセージ
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

// アサート不要な環境では何もしない
#define NS_ASSERT(cat, cond, ...) ((void)0)

#endif
