#pragma once

/// @file LogCategories.h
/// @brief NS::Core::LogCat — `NS_LOG_*` マクロが受け取るログカテゴリ enum
///
/// @details `magic_enum::enum_name` で文字列化し、ログ行頭の `[Core]` `[Graphics]` 等のタグに使う
/// 値の追加は呼出側に影響しない。衝突が発生したら層別 enum に分割する

namespace NS::Core
{

    /// ログ出力カテゴリ
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
