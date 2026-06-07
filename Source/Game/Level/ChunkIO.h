#pragma once

/// @file ChunkIO.h
/// @brief `.nslvl` chunk-based binary I/O (RIFF / PNG 風 FourCC + size レイアウト)
///
/// @details buffer-style API: 書き手は全 chunk を memory に積んでから `EndFile` で
/// 一括 disk 書込、 読み手はコンストラクタで全 file を一度 memory に読み込んで Magic /
/// Version / CRC32 を検証する。 想定 file size は数十 KB なので memory pressure は
/// 無視できる。 streaming I/O は採用しない (実装の単純さを優先)
/// 未知 FourCC chunk は size 分 skip して読み続けるため forward compatible

#include <cstdint>
#include <filesystem>
#include <vector>

namespace NS::Game::Level
{

    struct LevelData;

    constexpr std::uint16_t kCurrentVersionMajor = 1;
    constexpr std::uint16_t kCurrentVersionMinor = 0;

    /// `.nslvl` 書込: `BeginFile` → `BeginChunk` → `Write` 任意回 → `EndChunk` を
    /// 繰り返し → `EndFile` (`CRC3` chunk 自動追加 + disk flush)
    /// 各メソッドは失敗時に false + `NS_LOG_ERROR`、 以降のメソッドは何もしない
    class ChunkWriter
    {
    public:
        explicit ChunkWriter(std::filesystem::path outPath) noexcept;
        ~ChunkWriter() noexcept = default;

        ChunkWriter(const ChunkWriter&) = delete;
        ChunkWriter& operator=(const ChunkWriter&) = delete;

        [[nodiscard]] bool IsValid() const noexcept { return m_valid; }

        bool BeginFile(std::uint16_t versionMajor, std::uint16_t versionMinor) noexcept;
        bool BeginChunk(const char fourcc[4]) noexcept;
        bool Write(const void* data, std::size_t bytes) noexcept;
        bool EndChunk() noexcept;
        bool EndFile() noexcept;

    private:
        std::filesystem::path m_path;
        std::vector<std::byte> m_buffer;
        std::size_t m_currentChunkSizeOffset = 0;
        bool m_valid = false;
        bool m_inChunk = false;
        bool m_fileBegan = false;
        bool m_fileEnded = false;
    };

    /// `.nslvl` 読込: コンストラクタで `Filesystem::ReadAllBytes` → Magic / Version / CRC32 を
    /// 検証。 検証失敗時は `IsValid() == false` を返し、 以降の `SeekChunk` / `Read`
    /// は何もしない (false 返却)
    class ChunkReader
    {
    public:
        explicit ChunkReader(const std::filesystem::path& inPath) noexcept;
        ~ChunkReader() noexcept = default;

        ChunkReader(const ChunkReader&) = delete;
        ChunkReader& operator=(const ChunkReader&) = delete;

        [[nodiscard]] bool IsValid() const noexcept { return m_valid; }
        [[nodiscard]] std::uint16_t VersionMajor() const noexcept { return m_major; }
        [[nodiscard]] std::uint16_t VersionMinor() const noexcept { return m_minor; }

        /// 先頭 chunk から線形走査して `fourcc` 一致の chunk を見つける
        /// 見つかれば `outSize` に chunk のデータ size をセットし内部 cursor を data 先頭へ
        /// 未知 chunk は size 分 skip するので「不在 == false、 存在 == true」 を返す
        bool SeekChunk(const char fourcc[4], std::uint32_t& outSize) noexcept;

        /// 直前 `SeekChunk` の data 領域から `bytes` byte をコピー。 領域外なら false
        bool Read(void* outBuffer, std::size_t bytes) noexcept;

    private:
        std::vector<std::byte> m_buffer;
        std::size_t m_cursor = 0;
        std::uint16_t m_major = 0;
        std::uint16_t m_minor = 0;
        bool m_valid = false;
    };

    /// 1 関数で LevelData を `.nslvl` に書く。 失敗時は false + `NS_LOG_ERROR`
    [[nodiscard]] bool SaveLevelToFile(const LevelData& level, const std::filesystem::path& path) noexcept;

    /// 1 関数で LevelData を `.nslvl` から読む。 失敗時は false + `NS_LOG_ERROR`、
    /// `outLevel` は空 LevelData (default-constructed) に reset される
    [[nodiscard]] bool LoadLevelFromFile(LevelData& outLevel, const std::filesystem::path& path) noexcept;

} // namespace NS::Game::Level
