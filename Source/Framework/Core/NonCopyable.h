#pragma once

/// @file NonCopyable.h
/// @brief NS::Core::NonCopyable — コピー / ムーブを禁止する継承用基底
///
/// @details 継承するだけで派生のコピー / ムーブ 4 関数が delete 扱いになり、 identity が固定される
/// 単一所有 (unique_ptr 保持 + 生ポインタで参照共有) のリソース型を想定する
/// コンストラクタ / デストラクタは protected なので本型単体では生成・破棄できない (継承専用)
/// デストラクタは非 virtual。 NonCopyable* を介した多態破棄はしない前提

namespace NS::Core
{
    /// コピー / ムーブを禁止する継承用基底 (空クラス、 EBO でサイズ増加なし)
    /// 4 関数を明示的に delete し、 派生の意図 (複製・移動不可) をヘッダだけで読み取れるようにする
    class NonCopyable
    {
    protected:
        NonCopyable() = default;
        ~NonCopyable() = default;

    public:
        NonCopyable(const NonCopyable&) = delete;
        NonCopyable& operator=(const NonCopyable&) = delete;
        NonCopyable(NonCopyable&&) = delete;
        NonCopyable& operator=(NonCopyable&&) = delete;
    };
} // namespace NS::Core
