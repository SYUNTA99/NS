#include "Game/Level/LevelData.h"

#include "Framework/Math/Math.h"
#include "Game/Editor/BlockRegistry.h"
#include "Game/Level/detail/crc32.h"

#include <cmath>

namespace NS::Game::Level
{
    namespace
    {
        /// POD 値を std::byte span として view し CRC32 に流す helper
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

        // objects の論理 size を先に hash しておくと「append したら CRC 必ず変わる」 を保証できる
        const std::uint64_t objectCount = static_cast<std::uint64_t>(objects.size());
        crc = UpdateWith(crc, objectCount);
        if (!objects.empty())
        {
            const auto* raw = reinterpret_cast<const std::byte*>(objects.data());
            const std::size_t size = objects.size() * sizeof(ObjectInstance);
            crc = detail::Crc32Update(crc, std::span<const std::byte>(raw, size));
        }

        const std::uint64_t cameraVolumeCount = static_cast<std::uint64_t>(cameraVolumes.size());
        crc = UpdateWith(crc, cameraVolumeCount);
        if (!cameraVolumes.empty())
        {
            const auto* raw = reinterpret_cast<const std::byte*>(cameraVolumes.data());
            const std::size_t size = cameraVolumes.size() * sizeof(CameraVolume);
            crc = detail::Crc32Update(crc, std::span<const std::byte>(raw, size));
        }

        const std::uint64_t materialCount = static_cast<std::uint64_t>(materialPaths.size());
        crc = UpdateWith(crc, materialCount);
        for (const auto& materialPath : materialPaths)
        {
            const std::uint64_t length = static_cast<std::uint64_t>(materialPath.size());
            crc = UpdateWith(crc, length);
            if (!materialPath.empty())
            {
                const auto* raw = reinterpret_cast<const std::byte*>(materialPath.data());
                crc = detail::Crc32Update(crc, std::span<const std::byte>(raw, materialPath.size()));
            }
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

    std::int16_t ObjectCellX(const ObjectInstance& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(object.positionX));
    }

    std::int16_t ObjectCellY(const ObjectInstance& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(object.positionY));
    }

    std::int16_t ObjectCellZ(const ObjectInstance& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(object.positionZ));
    }

    std::size_t FindGridObjectAtCell(const LevelData& level, std::int16_t x, std::int16_t y, std::int16_t z) noexcept
    {
        for (std::size_t i = 0; i < level.objects.size(); ++i)
        {
            const auto& object = level.objects[i];
            if ((object.flags & kObjectFlagGridAligned) != 0 && ObjectCellX(object) == x && ObjectCellY(object) == y &&
                ObjectCellZ(object) == z)
            {
                return i;
            }
        }
        return kNoObjectIndex;
    }

    std::uint8_t GridRotationStep(const ObjectInstance& object) noexcept
    {
        // q と -q は同一回転なので符号無視 (fabs) で 4 候補の最近接を選ぶ。 BlockRotationToYaw の符号規約に依存しない
        const NS::Math::Quaternion current{object.rotationX, object.rotationY, object.rotationZ, object.rotationW};
        std::uint8_t best = 0;
        float bestDot = -2.0f;
        for (std::uint8_t step = 0; step < 4; ++step)
        {
            const float yaw = NS::Game::Editor::BlockRotationToYaw(step);
            const NS::Math::Quaternion candidate = NS::Math::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
            const float dot = std::fabs(current.x * candidate.x + current.y * candidate.y + current.z * candidate.z +
                                        current.w * candidate.w);
            if (dot > bestDot)
            {
                bestDot = dot;
                best = step;
            }
        }
        return best;
    }

    void SetGridRotationStep(ObjectInstance& object, std::uint8_t rotationStep) noexcept
    {
        const float yaw = NS::Game::Editor::BlockRotationToYaw(static_cast<std::uint8_t>(rotationStep & 0x03));
        const NS::Math::Quaternion rotation = NS::Math::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
        object.rotationX = rotation.x;
        object.rotationY = rotation.y;
        object.rotationZ = rotation.z;
        object.rotationW = rotation.w;
    }

    ObjectInstance MakeGridObject(
        std::int16_t x, std::int16_t y, std::int16_t z, std::uint16_t kind, std::uint8_t rotationStep) noexcept
    {
        ObjectInstance object{};
        object.positionX = static_cast<float>(x);
        object.positionY = static_cast<float>(y);
        object.positionZ = static_cast<float>(z);
        object.kind = kind;
        object.materialIndex = -1;
        object.flags = kObjectFlagGridAligned;
        SetGridRotationStep(object, rotationStep);
        return object;
    }

    void MigrateBlocksToObjects(LevelData& level, const std::vector<BlockEntry>& blocks) noexcept
    {
        level.objects.reserve(level.objects.size() + blocks.size());
        for (const auto& block : blocks)
        {
            level.objects.push_back(MakeGridObject(block.x, block.y, block.z, block.blockId, block.rotation));
        }
    }

} // namespace NS::Game::Level
