#include "Game/Level/LevelData.h"

#include "Game/Level/detail/crc32.h"

#include <cstring>

namespace NS::Game::Level
{
    namespace
    {
        /// POD 値を std::byte span として view し CRC32 に流す helper。
        template <typename T> std::uint32_t UpdateWith(std::uint32_t crc, const T& value) noexcept
        {
            static_assert(std::is_trivially_copyable_v<T>, "UpdateWith expects trivially copyable type");
            const auto* raw = reinterpret_cast<const std::byte*>(&value);
            return detail::Crc32Update(crc, std::span<const std::byte>(raw, sizeof(T)));
        }
    } // namespace

    std::uint32_t LevelData::ComputeCrc32() const noexcept
    {
        std::uint32_t crc = detail::kCrc32Init;

        // blocks の論理 size を先に hash しておくと「append したら CRC 必ず変わる」 を保証できる。
        const std::uint64_t blockCount = static_cast<std::uint64_t>(blocks.size());
        crc = UpdateWith(crc, blockCount);

        if (!blocks.empty())
        {
            const auto* raw = reinterpret_cast<const std::byte*>(blocks.data());
            const std::size_t size = blocks.size() * sizeof(BlockEntry);
            crc = detail::Crc32Update(crc, std::span<const std::byte>(raw, size));
        }

        crc = UpdateWith(crc, spawnX);
        crc = UpdateWith(crc, spawnY);
        crc = UpdateWith(crc, spawnZ);

        crc = UpdateWith(crc, themeId);
        crc = UpdateWith(crc, bgmId);
        crc = UpdateWith(crc, coinThreshold);
        crc = UpdateWith(crc, timeLimitSeconds);

        return detail::Crc32Finalize(crc);
    }

} // namespace NS::Game::Level
