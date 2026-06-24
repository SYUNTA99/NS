#include "Game/Level/ChunkIO.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/detail/crc32.h"

#include <cstring>
#include <span>
#include <string>
#include <utility>

namespace NS::Game::Level
{
    namespace
    {
        constexpr char kMagic[4] = {'N', 'S', 'L', 'V'};
        constexpr char kCrcFourCc[4] = {'C', 'R', 'C', '3'};
        constexpr char kMetaFourCc[4] = {'M', 'E', 'T', 'A'};
        constexpr char kBlksFourCc[4] = {'B', 'L', 'K', 'S'};
        constexpr char kObjsFourCc[4] = {'O', 'B', 'J', 'S'};
        constexpr char kMatsFourCc[4] = {'M', 'A', 'T', 'S'};
        constexpr char kSpwnFourCc[4] = {'S', 'P', 'W', 'N'};
        constexpr char kCamsFourCc[4] = {'C', 'A', 'M', 'S'};

        /// 読込 / 書込の上限。 巨大 size による memory exhaustion を防ぐ
        constexpr std::size_t kMaxLevelFileBytes = 16u * 1024u * 1024u;

        /// block_count u32 による大量 allocation を防ぐ上限
        constexpr std::uint32_t kMaxBlockCount = 100'000u;

        /// object_count u32 による大量 allocation を防ぐ上限
        constexpr std::uint32_t kMaxObjectCount = 100'000u;

        /// material 文字列表の上限 (枚数と 1 件あたり byte 長)
        constexpr std::uint32_t kMaxMaterialPaths = 4'096u;
        constexpr std::uint16_t kMaxMaterialPathLength = 1'024u;

        /// camera_volume_count u32 による大量 allocation を防ぐ上限
        constexpr std::uint32_t kMaxCameraVolumeCount = 4'096u;

        bool FourCcEqual(const char a[4], const char b[4]) noexcept
        {
            return std::memcmp(a, b, 4) == 0;
        }

        void AppendBytes(std::vector<std::byte>& buf, const void* data, std::size_t bytes)
        {
            const auto* raw = static_cast<const std::byte*>(data);
            buf.insert(buf.end(), raw, raw + bytes);
        }

        template <typename T> void AppendPod(std::vector<std::byte>& buf, const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>, "AppendPod expects trivially copyable type");
            AppendBytes(buf, &value, sizeof(T));
        }

        /// ObjectInstance のスカラ部だけを写した 88-byte の I/O DTO
        /// 脱 POD 後も binary I/O のバイト列を従来と一致させるための暫定表現で、 レベル保存を JSON へ移行し
        /// 旧 .nslvl を全て移行し終えたら撤去する。 フィールド順・型は脱 POD 前の ObjectInstance と同一
        /// ObjectInstance にスカラを足したら下の static_assert で検知する
        struct ObjectRecordV2
        {
            float positionX;
            float positionY;
            float positionZ;
            float rotationX;
            float rotationY;
            float rotationZ;
            float rotationW;
            float scaleX;
            float scaleY;
            float scaleZ;
            std::uint16_t kind;
            std::int16_t materialIndex;
            std::uint8_t flags;
            std::uint8_t shapeCollider;
            std::uint16_t reserved1;
            float colliderHalfExtentsX;
            float colliderHalfExtentsY;
            float colliderHalfExtentsZ;
            float colliderOffsetX;
            float colliderOffsetY;
            float colliderOffsetZ;
            float colliderRotationX;
            float colliderRotationY;
            float colliderRotationZ;
            float colliderRotationW;
        };
        static_assert(sizeof(ObjectRecordV2) == 88,
                      "ObjectRecordV2 must match the legacy 88-byte ObjectInstance layout");
        static_assert(std::is_trivially_copyable_v<ObjectRecordV2>,
                      "ObjectRecordV2 must be trivially copyable for I/O");
        // ObjectInstance はスカラ 88 byte と components の vector だけ、 という前提を縛る。 スカラを 1 つ足すと
        // この等式が崩れてここで落ち、 ObjectRecordV2/ToRecord/FromRecord の同期忘れを compile time に検知できる
        static_assert(sizeof(ObjectInstance) == sizeof(ObjectRecordV2) + sizeof(std::vector<ComponentData>),
                      "ObjectInstance scalar block must stay 88 bytes; sync ObjectRecordV2/ToRecord/FromRecord");

        ObjectRecordV2 ToRecord(const ObjectInstance& o) noexcept
        {
            return ObjectRecordV2{o.positionX,
                                  o.positionY,
                                  o.positionZ,
                                  o.rotationX,
                                  o.rotationY,
                                  o.rotationZ,
                                  o.rotationW,
                                  o.scaleX,
                                  o.scaleY,
                                  o.scaleZ,
                                  o.kind,
                                  o.materialIndex,
                                  o.flags,
                                  o.shapeCollider,
                                  o.reserved1,
                                  o.colliderHalfExtentsX,
                                  o.colliderHalfExtentsY,
                                  o.colliderHalfExtentsZ,
                                  o.colliderOffsetX,
                                  o.colliderOffsetY,
                                  o.colliderOffsetZ,
                                  o.colliderRotationX,
                                  o.colliderRotationY,
                                  o.colliderRotationZ,
                                  o.colliderRotationW};
        }

        void FromRecord(const ObjectRecordV2& r, ObjectInstance& o) noexcept
        {
            o.positionX = r.positionX;
            o.positionY = r.positionY;
            o.positionZ = r.positionZ;
            o.rotationX = r.rotationX;
            o.rotationY = r.rotationY;
            o.rotationZ = r.rotationZ;
            o.rotationW = r.rotationW;
            o.scaleX = r.scaleX;
            o.scaleY = r.scaleY;
            o.scaleZ = r.scaleZ;
            o.kind = r.kind;
            o.materialIndex = r.materialIndex;
            o.flags = r.flags;
            o.shapeCollider = r.shapeCollider;
            o.reserved1 = r.reserved1;
            o.colliderHalfExtentsX = r.colliderHalfExtentsX;
            o.colliderHalfExtentsY = r.colliderHalfExtentsY;
            o.colliderHalfExtentsZ = r.colliderHalfExtentsZ;
            o.colliderOffsetX = r.colliderOffsetX;
            o.colliderOffsetY = r.colliderOffsetY;
            o.colliderOffsetZ = r.colliderOffsetZ;
            o.colliderRotationX = r.colliderRotationX;
            o.colliderRotationY = r.colliderRotationY;
            o.colliderRotationZ = r.colliderRotationZ;
            o.colliderRotationW = r.colliderRotationW;
        }
    } // namespace

    // ---------------------------------------------------------------- ChunkWriter

    ChunkWriter::ChunkWriter(std::filesystem::path outPath) noexcept : m_path(std::move(outPath)), m_valid(true)
    {
        m_buffer.reserve(4 * 1024);
    }

    bool ChunkWriter::BeginFile(std::uint16_t versionMajor, std::uint16_t versionMinor) noexcept
    {
        if (!m_valid || m_fileBegan)
        {
            return false;
        }
        AppendBytes(m_buffer, kMagic, sizeof(kMagic));
        AppendPod(m_buffer, versionMajor);
        AppendPod(m_buffer, versionMinor);
        m_fileBegan = true;
        return true;
    }

    bool ChunkWriter::BeginChunk(const char fourcc[4]) noexcept
    {
        if (!m_valid || !m_fileBegan || m_inChunk || m_fileEnded)
        {
            return false;
        }
        AppendBytes(m_buffer, fourcc, 4);
        const std::uint32_t sizePlaceholder = 0;
        m_currentChunkSizeOffset = m_buffer.size();
        AppendPod(m_buffer, sizePlaceholder);
        m_inChunk = true;
        return true;
    }

    bool ChunkWriter::Write(const void* data, std::size_t bytes) noexcept
    {
        if (!m_valid || !m_inChunk)
        {
            return false;
        }
        AppendBytes(m_buffer, data, bytes);
        return true;
    }

    bool ChunkWriter::EndChunk() noexcept
    {
        if (!m_valid || !m_inChunk)
        {
            return false;
        }
        const std::size_t dataEnd = m_buffer.size();
        const std::size_t dataStart = m_currentChunkSizeOffset + sizeof(std::uint32_t);
        const std::uint32_t chunkSize = static_cast<std::uint32_t>(dataEnd - dataStart);
        std::memcpy(m_buffer.data() + m_currentChunkSizeOffset, &chunkSize, sizeof(std::uint32_t));
        m_inChunk = false;
        return true;
    }

    bool ChunkWriter::EndFile() noexcept
    {
        if (!m_valid || !m_fileBegan || m_inChunk || m_fileEnded)
        {
            return false;
        }

        // 既存 buffer 全体に対する CRC32 を計算し、 CRC3 chunk として末尾に書き込む
        const std::uint32_t crc = detail::Crc32(std::span<const std::byte>(m_buffer.data(), m_buffer.size()));
        AppendBytes(m_buffer, kCrcFourCc, 4);
        const std::uint32_t crcSize = sizeof(std::uint32_t);
        AppendPod(m_buffer, crcSize);
        AppendPod(m_buffer, crc);

        if (m_buffer.size() > kMaxLevelFileBytes)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Game,
                         "ChunkWriter::EndFile: 出力 file が上限 ({} byte) を超過: {} byte",
                         kMaxLevelFileBytes,
                         m_buffer.size());
            m_valid = false;
            return false;
        }

        if (!::NS::Core::FileSystem::WriteAllBytes(m_path, m_buffer))
        {
            m_valid = false;
            return false;
        }
        m_fileEnded = true;
        return true;
    }

    // ---------------------------------------------------------------- ChunkReader

    ChunkReader::ChunkReader(const std::filesystem::path& inPath) noexcept
    {
        auto bytesOpt = ::NS::Core::FileSystem::ReadAllBytes(inPath);
        if (!bytesOpt.has_value())
        {
            return;
        }
        m_buffer = std::move(*bytesOpt);

        if (m_buffer.size() > kMaxLevelFileBytes)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Game,
                         "ChunkReader: file が上限 ({} byte) を超えるので reject: {}",
                         kMaxLevelFileBytes,
                         inPath.string());
            return;
        }

        // 最小 size: Magic 4 + Version 4 + CRC3 chunk (FourCC 4 + size 4 + data 4) = 20 byte
        constexpr std::size_t kMinFileSize = 4 + 4 + 4 + 4 + 4;
        if (m_buffer.size() < kMinFileSize)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Game,
                         "ChunkReader: file size 不足 ({} byte): {}",
                         m_buffer.size(),
                         inPath.string());
            return;
        }

        if (!FourCcEqual(reinterpret_cast<const char*>(m_buffer.data()), kMagic))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Game, "ChunkReader: Magic 'NSLV' 不一致: {}", inPath.string());
            return;
        }

        std::memcpy(&m_major, m_buffer.data() + 4, sizeof(std::uint16_t));
        std::memcpy(&m_minor, m_buffer.data() + 6, sizeof(std::uint16_t));

        if (m_major != kCurrentVersionMajor)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Game,
                         "ChunkReader: major version {} は未対応 (期待 {}): {}",
                         m_major,
                         kCurrentVersionMajor,
                         inPath.string());
            return;
        }

        // CRC3 chunk を末尾から検出。 仕様上は最終 chunk なので 12 byte 巻き戻して読む
        const std::size_t crcStart = m_buffer.size() - (4 + 4 + 4);
        if (!FourCcEqual(reinterpret_cast<const char*>(m_buffer.data() + crcStart), kCrcFourCc))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Game, "ChunkReader: 末尾 CRC3 chunk が存在しない: {}", inPath.string());
            return;
        }

        std::uint32_t crcChunkSize = 0;
        std::memcpy(&crcChunkSize, m_buffer.data() + crcStart + 4, sizeof(std::uint32_t));
        if (crcChunkSize != sizeof(std::uint32_t))
        {
            NS_LOG_ERROR(
                ::NS::Core::LogCat::Game, "ChunkReader: CRC3 chunk size 異常 ({}): {}", crcChunkSize, inPath.string());
            return;
        }

        std::uint32_t storedCrc = 0;
        std::memcpy(&storedCrc, m_buffer.data() + crcStart + 8, sizeof(std::uint32_t));

        const std::uint32_t computedCrc = detail::Crc32(std::span<const std::byte>(m_buffer.data(), crcStart));
        if (storedCrc != computedCrc)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Game,
                         "ChunkReader: CRC32 mismatch (stored=0x{:08X} computed=0x{:08X}): {}",
                         storedCrc,
                         computedCrc,
                         inPath.string());
            return;
        }

        // 内部 cursor は Magic + Version 直後 (= 最初の chunk 先頭) に置く
        m_cursor = 8;
        m_valid = true;
    }

    bool ChunkReader::SeekChunk(const char fourcc[4], std::uint32_t& outSize) noexcept
    {
        if (!m_valid)
        {
            return false;
        }

        // CRC3 までを線形走査。 未知 FourCC は size 分 skip
        std::size_t pos = 8;
        const std::size_t fileEnd = m_buffer.size();
        while (pos + 8 <= fileEnd)
        {
            const char* chunkFourCc = reinterpret_cast<const char*>(m_buffer.data() + pos);
            std::uint32_t chunkSize = 0;
            std::memcpy(&chunkSize, m_buffer.data() + pos + 4, sizeof(std::uint32_t));

            const std::size_t dataStart = pos + 8;
            if (chunkSize > fileEnd - dataStart)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game,
                             "ChunkReader::SeekChunk: chunk size 残量超過 (size={} remaining={})",
                             chunkSize,
                             fileEnd - dataStart);
                return false;
            }

            if (FourCcEqual(chunkFourCc, kCrcFourCc))
            {
                // CRC3 まで来たら見つからなかったということ
                return false;
            }

            if (FourCcEqual(chunkFourCc, fourcc))
            {
                outSize = chunkSize;
                m_cursor = dataStart;
                return true;
            }

            pos = dataStart + chunkSize;
        }
        return false;
    }

    bool ChunkReader::Read(void* outBuffer, std::size_t bytes) noexcept
    {
        if (!m_valid || m_cursor + bytes > m_buffer.size())
        {
            return false;
        }
        std::memcpy(outBuffer, m_buffer.data() + m_cursor, bytes);
        m_cursor += bytes;
        return true;
    }

    // ---------------------------------------------------------------- High-level helpers

    bool SaveLevelToFile(const LevelData& level, const std::filesystem::path& path) noexcept
    {
        if (level.objects.size() > kMaxObjectCount)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Game,
                         "SaveLevelToFile: object 数が上限超過 ({} > {})",
                         level.objects.size(),
                         kMaxObjectCount);
            return false;
        }

        if (level.materialPaths.size() > kMaxMaterialPaths)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Game,
                         "SaveLevelToFile: material path 数が上限超過 ({} > {})",
                         level.materialPaths.size(),
                         kMaxMaterialPaths);
            return false;
        }

        if (level.cameraVolumes.size() > kMaxCameraVolumeCount)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Game,
                         "SaveLevelToFile: camera volume 数が上限超過 ({} > {})",
                         level.cameraVolumes.size(),
                         kMaxCameraVolumeCount);
            return false;
        }
        for (const auto& materialPath : level.materialPaths)
        {
            if (materialPath.size() > kMaxMaterialPathLength)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game,
                             "SaveLevelToFile: material path が長すぎる ({} > {} byte)",
                             materialPath.size(),
                             kMaxMaterialPathLength);
                return false;
            }
        }

        ChunkWriter writer(path);
        if (!writer.BeginFile(kCurrentVersionMajor, kCurrentVersionMinor))
        {
            return false;
        }

        // META chunk: theme/bgm/threshold/time (2B × 4)
        if (!writer.BeginChunk(kMetaFourCc))
        {
            return false;
        }
        writer.Write(&level.themeId, sizeof(level.themeId));
        writer.Write(&level.bgmId, sizeof(level.bgmId));
        writer.Write(&level.coinThreshold, sizeof(level.coinThreshold));
        writer.Write(&level.timeLimitSeconds, sizeof(level.timeLimitSeconds));
        if (!writer.EndChunk())
        {
            return false;
        }

        // OBJS chunk: u32 count + N × ObjectInstance (88 byte each)
        if (!writer.BeginChunk(kObjsFourCc))
        {
            return false;
        }
        const std::uint32_t objectCount = static_cast<std::uint32_t>(level.objects.size());
        writer.Write(&objectCount, sizeof(objectCount));
        for (const auto& object : level.objects)
        {
            // components はこの暫定 binary I/O では落とす
            const ObjectRecordV2 record = ToRecord(object);
            writer.Write(&record, sizeof(record));
        }
        if (!writer.EndChunk())
        {
            return false;
        }

        // CAMS chunk: u32 count + N × CameraVolume (56 byte each)
        if (!writer.BeginChunk(kCamsFourCc))
        {
            return false;
        }
        const std::uint32_t cameraVolumeCount = static_cast<std::uint32_t>(level.cameraVolumes.size());
        writer.Write(&cameraVolumeCount, sizeof(cameraVolumeCount));
        if (cameraVolumeCount > 0)
        {
            writer.Write(level.cameraVolumes.data(), cameraVolumeCount * sizeof(CameraVolume));
        }
        if (!writer.EndChunk())
        {
            return false;
        }

        // MATS chunk: u32 count + N × (u16 length + length byte の UTF-8 path)
        if (!writer.BeginChunk(kMatsFourCc))
        {
            return false;
        }
        const std::uint32_t materialCount = static_cast<std::uint32_t>(level.materialPaths.size());
        writer.Write(&materialCount, sizeof(materialCount));
        for (const auto& materialPath : level.materialPaths)
        {
            const std::uint16_t length = static_cast<std::uint16_t>(materialPath.size());
            writer.Write(&length, sizeof(length));
            if (length > 0)
            {
                writer.Write(materialPath.data(), length);
            }
        }
        if (!writer.EndChunk())
        {
            return false;
        }

        // SPWN chunk: i16 × 3
        if (!writer.BeginChunk(kSpwnFourCc))
        {
            return false;
        }
        writer.Write(&level.spawnX, sizeof(level.spawnX));
        writer.Write(&level.spawnY, sizeof(level.spawnY));
        writer.Write(&level.spawnZ, sizeof(level.spawnZ));
        if (!writer.EndChunk())
        {
            return false;
        }

        return writer.EndFile();
    }

    bool LoadLevelFromFile(LevelData& outLevel, const std::filesystem::path& path) noexcept
    {
        outLevel = LevelData{};

        ChunkReader reader(path);
        if (!reader.IsValid())
        {
            return false;
        }

        std::uint32_t size = 0;

        if (reader.SeekChunk(kMetaFourCc, size))
        {
            // META は厳密 8 byte だが forward-compat のため不足 / 余りは無害化
            if (size >= 8)
            {
                reader.Read(&outLevel.themeId, sizeof(outLevel.themeId));
                reader.Read(&outLevel.bgmId, sizeof(outLevel.bgmId));
                reader.Read(&outLevel.coinThreshold, sizeof(outLevel.coinThreshold));
                reader.Read(&outLevel.timeLimitSeconds, sizeof(outLevel.timeLimitSeconds));
            }
        }

        // 旧 .nslvl の BLKS は読込時に一時 vector へ読んで objects へ移行する (新規保存は OBJS のみ)
        if (reader.SeekChunk(kBlksFourCc, size))
        {
            std::uint32_t blockCount = 0;
            if (!reader.Read(&blockCount, sizeof(blockCount)))
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game, "LoadLevelFromFile: BLKS chunk から block count を読めない");
                outLevel = LevelData{};
                return false;
            }
            if (blockCount > kMaxBlockCount)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game,
                             "LoadLevelFromFile: block count {} が上限 {} を超過",
                             blockCount,
                             kMaxBlockCount);
                outLevel = LevelData{};
                return false;
            }
            // chunk 残量チェック (truncated reject)
            const std::size_t expectedDataBytes = blockCount * sizeof(BlockEntry);
            if (expectedDataBytes + sizeof(blockCount) > size)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game,
                             "LoadLevelFromFile: BLKS chunk 内 data 不足 (期待 {} 実際 {})",
                             expectedDataBytes + sizeof(blockCount),
                             size);
                outLevel = LevelData{};
                return false;
            }
            std::vector<BlockEntry> legacyBlocks(blockCount);
            if (blockCount > 0)
            {
                reader.Read(legacyBlocks.data(), expectedDataBytes);
            }
            MigrateBlocksToObjects(outLevel, legacyBlocks);
        }

        if (reader.SeekChunk(kObjsFourCc, size))
        {
            std::uint32_t objectCount = 0;
            if (!reader.Read(&objectCount, sizeof(objectCount)))
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game, "LoadLevelFromFile: OBJS chunk から object count を読めない");
                outLevel = LevelData{};
                return false;
            }
            if (objectCount > kMaxObjectCount)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game,
                             "LoadLevelFromFile: object count {} が上限 {} を超過",
                             objectCount,
                             kMaxObjectCount);
                outLevel = LevelData{};
                return false;
            }
            // record サイズ: minor 0=48 byte、 minor 1=60 byte (collider half-extents)、 現行 minor 2=88 byte
            // (collider offset / rotation 追加分)。 chunk 実データ長 / count で判別し、 旧形式は先頭 prefix だけ
            // 読んで新フィールドは default (half 0.5 / offset 0 / rotation 単位) を残す
            constexpr std::size_t kObjectRecordV0Bytes = 48;
            constexpr std::size_t kObjectRecordV1Bytes = 60;
            const std::size_t recordBytes =
                (objectCount > 0) ? (size - sizeof(objectCount)) / objectCount : sizeof(ObjectRecordV2);
            const std::size_t expectedDataBytes = objectCount * recordBytes;
            const bool knownRecord = (recordBytes == sizeof(ObjectRecordV2) || recordBytes == kObjectRecordV1Bytes ||
                                      recordBytes == kObjectRecordV0Bytes);
            if (!knownRecord || expectedDataBytes + sizeof(objectCount) != size)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game,
                             "LoadLevelFromFile: OBJS chunk の record サイズが不正 (record {} count {} size {})",
                             recordBytes,
                             objectCount,
                             size);
                outLevel = LevelData{};
                return false;
            }
            outLevel.objects.resize(objectCount);
            // 既定値入りの record に先頭 recordBytes だけ上書きする。 V0/V1 は現行レイアウトの
            // 先頭 prefix なので不足分は ObjectInstance の default が残る
            // components はこの暫定 binary I/O では常に空
            const ObjectRecordV2 defaults = ToRecord(ObjectInstance{});
            for (std::uint32_t i = 0; i < objectCount; ++i)
            {
                ObjectRecordV2 record = defaults;
                reader.Read(&record, recordBytes);
                FromRecord(record, outLevel.objects[i]);
            }
        }

        if (reader.SeekChunk(kCamsFourCc, size))
        {
            std::uint32_t cameraVolumeCount = 0;
            if (!reader.Read(&cameraVolumeCount, sizeof(cameraVolumeCount)))
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game,
                             "LoadLevelFromFile: CAMS chunk から camera volume count を読めない");
                outLevel = LevelData{};
                return false;
            }
            if (cameraVolumeCount > kMaxCameraVolumeCount)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game,
                             "LoadLevelFromFile: camera volume count {} が上限 {} を超過",
                             cameraVolumeCount,
                             kMaxCameraVolumeCount);
                outLevel = LevelData{};
                return false;
            }
            const std::size_t expectedDataBytes = cameraVolumeCount * sizeof(CameraVolume);
            if (expectedDataBytes + sizeof(cameraVolumeCount) > size)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game,
                             "LoadLevelFromFile: CAMS chunk 内 data 不足 (期待 {} 実際 {})",
                             expectedDataBytes + sizeof(cameraVolumeCount),
                             size);
                outLevel = LevelData{};
                return false;
            }
            outLevel.cameraVolumes.resize(cameraVolumeCount);
            if (cameraVolumeCount > 0)
            {
                reader.Read(outLevel.cameraVolumes.data(), expectedDataBytes);
            }
        }

        if (reader.SeekChunk(kMatsFourCc, size))
        {
            std::uint32_t materialCount = 0;
            if (!reader.Read(&materialCount, sizeof(materialCount)))
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game, "LoadLevelFromFile: MATS chunk から material count を読めない");
                outLevel = LevelData{};
                return false;
            }
            if (materialCount > kMaxMaterialPaths)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Game,
                             "LoadLevelFromFile: material count {} が上限 {} を超過",
                             materialCount,
                             kMaxMaterialPaths);
                outLevel = LevelData{};
                return false;
            }

            // 可変長 string の連続。 chunk 残量を consumed で追って境界外 read を弾く
            std::size_t consumed = sizeof(materialCount);
            outLevel.materialPaths.reserve(materialCount);
            for (std::uint32_t i = 0; i < materialCount; ++i)
            {
                std::uint16_t length = 0;
                if (consumed + sizeof(length) > size || !reader.Read(&length, sizeof(length)))
                {
                    NS_LOG_ERROR(::NS::Core::LogCat::Game, "LoadLevelFromFile: MATS chunk から path length を読めない");
                    outLevel = LevelData{};
                    return false;
                }
                consumed += sizeof(length);
                if (length > kMaxMaterialPathLength || consumed + length > size)
                {
                    NS_LOG_ERROR(
                        ::NS::Core::LogCat::Game, "LoadLevelFromFile: MATS chunk の path length {} が不正", length);
                    outLevel = LevelData{};
                    return false;
                }
                std::string materialPath(length, '\0');
                if (length > 0 && !reader.Read(materialPath.data(), length))
                {
                    outLevel = LevelData{};
                    return false;
                }
                consumed += length;
                outLevel.materialPaths.push_back(std::move(materialPath));
            }
        }

        if (reader.SeekChunk(kSpwnFourCc, size))
        {
            if (size >= 6)
            {
                reader.Read(&outLevel.spawnX, sizeof(outLevel.spawnX));
                reader.Read(&outLevel.spawnY, sizeof(outLevel.spawnY));
                reader.Read(&outLevel.spawnZ, sizeof(outLevel.spawnZ));
            }
        }

        return true;
    }

} // namespace NS::Game::Level
