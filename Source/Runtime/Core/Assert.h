#pragma once

// マクロで NS_LOG_FATAL を使うので、勝手に include が消されないようにする
#include "Runtime/Core/Logger.h" // IWYU pragma: keep

#if defined(NS_ENABLE_ASSERT) && NS_ENABLE_ASSERT

/// @brief バグチェック用。変な値が来たらここで落とす
/// @details リリース版だとこのマクロごと綺麗に消えるので、条件式の中に計算とか処理を書いちゃダメ
/// @param cat ログのカテゴリ
/// @param cond これが false だとクラッシュする
/// @param ... 落ちたときに出すエラーメッセージ
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
