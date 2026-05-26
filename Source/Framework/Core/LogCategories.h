#pragma once

/// @file LogCategories.h
/// @brief NS::Core::LogCat — `NS_LOG_*` マクロが受け取るログカテゴリ enum。
///
/// @details `magic_enum::enum_name(cat)` で実行時に文字列化され、 ログ行頭の
/// `[Core]` `[Graphics]` 等のタグに使う。 値の追加は呼出側に影響しないが、
/// 衝突が発生したら層別の独自 enum に分割する判断を取る。

namespace NS::Core
{

    /// ログ出力カテゴリ。
    /// magic_enum::enum_name で文字列化し、ログ行頭の `[Core]` 等のタグに使う。
    /// 衝突が発生したら層別 namespace に分割する。
    enum class LogCat
    {
        Core,
        Platform,
        Graphics,
        App,
        Game,
        UI,
    };

} // namespace NS::Core
