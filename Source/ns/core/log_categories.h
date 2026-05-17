#pragma once

namespace ns::core
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
    };

} // namespace ns::core
