#pragma once

/// @file Assert.h
/// @brief NS_ASSERT — 契約違反を開発ビルドで即クラッシュさせる表明マクロ
///
/// @details 呼び出し側のバグ（事前条件・不変条件の破れ）を開発時に炙り出すための表明
/// `NS_ENABLE_ASSERT == 1`（Debug / Development / GameDebug）でのみ条件を評価し、偽なら
/// 既存の `NS_LOG_FATAL` 経由で flush → `__debugbreak()` → `std::abort()` へ落とす
/// `NS_ENABLE_ASSERT == 0`（GameRelease）では条件式ごと `((void)0)` に畳むため、
/// 条件に副作用のある式を渡してはならない
/// 出荷でも停止すべき回復不能な実行時状態には NS_ASSERT ではなく `NS_LOG_FATAL` を直接使う
/// メッセージは `std::format` 構文で、破れた契約を必ず言葉で残すため cat と説明は必須

// NS_ASSERT が展開する NS_LOG_FATAL の定義元。マクロ経由のため静的解析には見えず保持する
#include "Framework/Core/Logger.h" // IWYU pragma: keep

#if defined(NS_ENABLE_ASSERT) && NS_ENABLE_ASSERT

/// 契約違反表明。cond が偽なら NS_LOG_FATAL でプロセスを停止する
/// @param cat  ::NS::Core::LogCat の値
/// @param cond 満たされるべき条件。偽で停止する
/// @param ...  std::format 構文の説明メッセージ。破れた契約を必ず書く
#define NS_ASSERT(cat, cond, ...)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            NS_LOG_FATAL((cat), __VA_ARGS__);                                                                          \
        }                                                                                                              \
    }                                                                                                                  \
    while (false)

#else

#define NS_ASSERT(cat, cond, ...) ((void)0)

#endif
