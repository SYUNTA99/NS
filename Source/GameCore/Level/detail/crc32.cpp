#include "GameCore/Level/detail/crc32.h"

namespace NS::GameCore::Level::detail
{
    namespace
    {
        constexpr std::uint32_t kPolynomial = 0xEDB88320u;

        constexpr std::array<std::uint32_t, 256> BuildTable() noexcept
        {
            std::array<std::uint32_t, 256> table{};
            for (std::uint32_t i = 0; i < 256; ++i)
            {
                std::uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                {
                    if ((c & 1u) != 0u)
                        c = kPolynomial ^ (c >> 1);
                    else
                        c = c >> 1;
                }
                table[i] = c;
            }
            return table;
        }

        constexpr auto kTable = BuildTable();
    } // namespace

    std::uint32_t Crc32Update(std::uint32_t crc, std::span<const std::byte> bytes) noexcept
    {
        for (std::byte b : bytes)
        {
            const std::uint8_t byteValue = static_cast<std::uint8_t>(b);
            crc = kTable[(crc ^ byteValue) & 0xFFu] ^ (crc >> 8);
        }
        return crc;
    }

    std::uint32_t Crc32Finalize(std::uint32_t crc) noexcept
    {
        return crc ^ 0xFFFFFFFFu;
    }

} // namespace NS::GameCore::Level::detail
