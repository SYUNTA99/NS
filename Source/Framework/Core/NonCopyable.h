#pragma once

/// @file NonCopyable.h
/// @brief NS::Core::NonCopyable — コピー / ムーブを禁止する継承用基底
///
/// @details 継承するだけで派生のコピー / ムーブ 4 関数が delete 扱いになる
/// unique_ptr 保持 + 生ポインタ参照共有のリソース型を想定する
/// コンストラクタ / デストラクタは protected で継承専用。デストラクタは非 virtual

namespace NS::Core
{
    /// コピー / ムーブを禁止する継承用基底 (空クラス、EBO でサイズ増加なし)
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
