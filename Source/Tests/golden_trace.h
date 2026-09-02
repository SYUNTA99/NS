#pragma once

#include <Runtime/Core/Math.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace NS::Tests
{
    //! @brief 1 固定ステップぶんの記録
    //! @details コヨーテ時間・先行入力の内部の秒は、ジャンプの成否として位置・速度・接地に出る
    //! 基準比較はこの並びを 1 つの値へ畳んで突き合わせる
    struct StepRecord
    {
        NS::Core::Vector3 position{};
        NS::Core::Vector3 velocity{};
        bool grounded = false;
    };

    namespace detail
    {
        //! FNV-1a 64 ビットへ 4 バイトを畳み込む
        [[nodiscard]] inline std::uint64_t FoldFnv1a(std::uint64_t hash, std::uint32_t value) noexcept
        {
            for (int shift = 0; shift < 32; shift += 8)
            {
                hash ^= (value >> shift) & 0xFFu;
                hash *= 0x100000001B3ULL;
            }
            return hash;
        }
    } // namespace detail

    //! @brief 記録の並びを 1 つのハッシュへ畳む
    //! @details float はビット表現のまま投入するので、1 ビットでも挙動が変われば値が変わる
    //! @param[in] trace 毎固定ステップの記録
    //! @return 並び全体のハッシュ
    [[nodiscard]] inline std::uint64_t FoldTrace(const std::vector<StepRecord>& trace) noexcept
    {
        std::uint64_t hash = 0xCBF29CE484222325ULL;
        for (const StepRecord& s : trace)
        {
            hash = detail::FoldFnv1a(hash, std::bit_cast<std::uint32_t>(s.position.x));
            hash = detail::FoldFnv1a(hash, std::bit_cast<std::uint32_t>(s.position.y));
            hash = detail::FoldFnv1a(hash, std::bit_cast<std::uint32_t>(s.position.z));
            hash = detail::FoldFnv1a(hash, std::bit_cast<std::uint32_t>(s.velocity.x));
            hash = detail::FoldFnv1a(hash, std::bit_cast<std::uint32_t>(s.velocity.y));
            hash = detail::FoldFnv1a(hash, std::bit_cast<std::uint32_t>(s.velocity.z));
            std::uint32_t groundedBit = 0u;
            if (s.grounded)
                groundedBit = 1u;
            hash = detail::FoldFnv1a(hash, groundedBit);
        }
        return hash;
    }

    //! @brief 不一致時の手がかりの文字列を作る
    //! @param[in] trace 毎固定ステップの記録
    //! @param[in] hash 実測のハッシュ
    //! @return 実測のハッシュと 10 ステップごとの位置・速度・接地
    [[nodiscard]] inline std::string DescribeTrace(const std::vector<StepRecord>& trace, std::uint64_t hash)
    {
        std::ostringstream out;
        out << "actual hash = 0x" << std::hex << hash << std::dec << "\n";
        for (std::size_t i = 0; i < trace.size(); i += 10)
        {
            const StepRecord& s = trace[i];
            out << "step " << i << ": pos " << s.position.x << " " << s.position.y << " " << s.position.z << " / vel "
                << s.velocity.x << " " << s.velocity.y << " " << s.velocity.z << " / grounded " << s.grounded << "\n";
        }
        return out.str();
    }
} // namespace NS::Tests
