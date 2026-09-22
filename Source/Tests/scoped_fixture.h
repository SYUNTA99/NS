#pragma once

#include <Runtime/Platform/Filesystem.h>
#include <Runtime/Core/StringUtils.h>

#include <cstddef>
#include <gtest/gtest.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

namespace NsTest
{
    //! @brief コンストラクタで実行ファイルの隣に書き、デストラクタで消す試し用のファイル
    //! @details 片付けないと実行ファイルの隣に溜まり続ける。名前を変えた試しの残骸は誰も消さない
    class ScopedFixture
    {
    public:
        //! @brief text を実行ファイルの隣の name へ書き出す
        //! @param[in] name 実行ファイルのあるディレクトリから見たファイル名
        //! @param[in] text 書き出す中身
        ScopedFixture(const char* name, std::string_view text)
            : m_path(NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::GetExeDirectory(), name))
        {
            Write(std::as_bytes(std::span<const char>{text.data(), text.size()}));
        }

        //! @brief bytes を実行ファイルの隣の name へ書き出す
        //! @param[in] name 実行ファイルのあるディレクトリから見たファイル名
        //! @param[in] bytes 書き出す中身
        ScopedFixture(const char* name, const std::vector<unsigned char>& bytes)
            : m_path(NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::GetExeDirectory(), name))
        {
            Write(std::as_bytes(std::span<const unsigned char>{bytes.data(), bytes.size()}));
        }

        ~ScopedFixture()
        {
            // 消せなくても試しの結果は変わらない。後始末の失敗で試しを落とさない
            ::DeleteFileW(NS::Core::StringUtils::WideFromUtf8(m_path).c_str());
        }

        ScopedFixture(const ScopedFixture&) = delete;
        ScopedFixture& operator=(const ScopedFixture&) = delete;
        ScopedFixture(ScopedFixture&&) = delete;
        ScopedFixture& operator=(ScopedFixture&&) = delete;

        //! @brief コンストラクタで書いたファイルを text で上書きする
        void Rewrite(std::string_view text) { Write(std::as_bytes(std::span<const char>{text.data(), text.size()})); }

        [[nodiscard]] const std::string& Path() const noexcept { return m_path; } //!< 書き出し先の絶対パス

    private:
        void Write(std::span<const std::byte> bytes)
        {
            // ASSERT_* は void を返す関数でしか使えないので、呼び元のコンストラクタでは書けない
            EXPECT_TRUE(NS::Platform::FileSystem::WriteAllBytes(m_path, bytes));
        }

        std::string m_path;
    };
} // namespace NsTest
