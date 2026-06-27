#include "Game/Level/LevelData.h"

#include "Framework/Math/Math.h"
#include "Game/Blocks/BlockRegistry.h"
#include "Game/Blocks/BuildPlacedObject.h"
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

        /// 長さ prefix + 中身バイトで文字列を hash する
        std::uint32_t UpdateWithString(std::uint32_t crc, const std::string& text) noexcept
        {
            crc = UpdateWith(crc, static_cast<std::uint64_t>(text.size()));
            if (!text.empty())
            {
                const auto* raw = reinterpret_cast<const std::byte*>(text.data());
                crc = detail::Crc32Update(crc, std::span<const std::byte>(raw, text.size()));
            }
            return crc;
        }

        /// FieldValue を「名前 + variant tag + 値」の順で hash する。 順序固定で決定的
        /// case の数値は FieldValue::value の変種宣言順に対応する
        /// 変種を増減・並べ替えるときは FieldValue::operator== の switch と必ず一緒に直すこと
        std::uint32_t UpdateWithFieldValue(std::uint32_t crc, const FieldValue& field) noexcept
        {
            crc = UpdateWithString(crc, field.name);
            crc = UpdateWith(crc, static_cast<std::uint8_t>(field.value.index()));
            switch (field.value.index())
            {
            case 0:
                return UpdateWith(crc, std::get<float>(field.value));
            case 1:
                return UpdateWith(crc, std::get<int>(field.value));
            case 2:
                return UpdateWith(crc, std::get<bool>(field.value));
            case 3:
                return UpdateWith(crc, std::get<NS::Math::Vector3>(field.value));
            case 4:
                return UpdateWithString(crc, std::get<std::string>(field.value));
            default:
                // valueless_by_exception 等の想定外 index。 tag は hash 済なので値は足さない
                return crc;
            }
        }

        /// ObjectInstance のスカラ部を宣言順で hash し、 続けて components を hash する
        /// component の field は名前順に hash するため、 save→load の正準化 (名前昇順) を跨いでも CRC が安定する
        std::uint32_t UpdateWithObject(std::uint32_t crc, const ObjectInstance& object) noexcept
        {
            crc = UpdateWith(crc, object.positionX);
            crc = UpdateWith(crc, object.positionY);
            crc = UpdateWith(crc, object.positionZ);
            crc = UpdateWith(crc, object.rotationX);
            crc = UpdateWith(crc, object.rotationY);
            crc = UpdateWith(crc, object.rotationZ);
            crc = UpdateWith(crc, object.rotationW);
            crc = UpdateWith(crc, object.scaleX);
            crc = UpdateWith(crc, object.scaleY);
            crc = UpdateWith(crc, object.scaleZ);
            crc = UpdateWith(crc, object.materialIndex);
            crc = UpdateWith(crc, object.flags);
            crc = UpdateWith(crc, object.shapeCollider);
            crc = UpdateWith(crc, object.reserved1);
            crc = UpdateWith(crc, object.colliderHalfExtentsX);
            crc = UpdateWith(crc, object.colliderHalfExtentsY);
            crc = UpdateWith(crc, object.colliderHalfExtentsZ);
            crc = UpdateWith(crc, object.colliderOffsetX);
            crc = UpdateWith(crc, object.colliderOffsetY);
            crc = UpdateWith(crc, object.colliderOffsetZ);
            crc = UpdateWith(crc, object.colliderRotationX);
            crc = UpdateWith(crc, object.colliderRotationY);
            crc = UpdateWith(crc, object.colliderRotationZ);
            crc = UpdateWith(crc, object.colliderRotationW);

            for (const auto& component : object.components)
            {
                crc = UpdateWithString(crc, component.typeName);
                const std::size_t fieldCount = component.fields.size();
                crc = UpdateWith(crc, static_cast<std::uint64_t>(fieldCount));
                // JSON は field を名前順に正準化するので CRC も名前順で hash する。 field 名は component 内で一意なので
                // heap を使わず「直前より大きい最小名」を順に選んで安定させる (noexcept・ 非確保)
                const std::string* previousName = nullptr;
                for (std::size_t emitted = 0; emitted < fieldCount; ++emitted)
                {
                    const FieldValue* next = nullptr;
                    for (const auto& field : component.fields)
                    {
                        const bool afterPrevious = previousName == nullptr || field.name > *previousName;
                        if (afterPrevious && (next == nullptr || field.name < next->name))
                            next = &field;
                    }
                    if (next == nullptr)
                        break; // 想定外 (重複名) は安全側で打ち切る
                    crc = UpdateWithFieldValue(crc, *next);
                    previousName = &next->name;
                }
            }
            return crc;
        }
    } // namespace

    bool FieldValue::operator==(const FieldValue& other) const noexcept
    {
        if (name != other.name || value.index() != other.value.index())
        {
            return false;
        }
        switch (value.index())
        {
        case 0:
            return std::get<float>(value) == std::get<float>(other.value);
        case 1:
            return std::get<int>(value) == std::get<int>(other.value);
        case 2:
            return std::get<bool>(value) == std::get<bool>(other.value);
        case 3:
        {
            const auto& a = std::get<NS::Math::Vector3>(value);
            const auto& b = std::get<NS::Math::Vector3>(other.value);
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }
        case 4:
            return std::get<std::string>(value) == std::get<std::string>(other.value);
        default:
            // 両者 index 一致を確認済なので、 valueless 同士など想定外 index は等しくないとみなす
            return false;
        }
    }

    std::uint32_t LevelData::ComputeCrc32() const noexcept
    {
        std::uint32_t crc = detail::kCrc32Init;

        // objects の論理 size を先に hash しておくと「append したら CRC 必ず変わる」 を保証できる
        const std::uint64_t objectCount = static_cast<std::uint64_t>(objects.size());
        crc = UpdateWith(crc, objectCount);
        for (const auto& object : objects)
        {
            crc = UpdateWithObject(crc, object);
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
            crc = UpdateWithString(crc, materialPath);
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

    ShapeCollider ObjectShapeCollider(const ObjectInstance& object) noexcept
    {
        switch (object.shapeCollider)
        {
        case static_cast<std::uint8_t>(ShapeCollider::Sphere):
            return ShapeCollider::Sphere;
        case static_cast<std::uint8_t>(ShapeCollider::Capsule):
            return ShapeCollider::Capsule;
        case static_cast<std::uint8_t>(ShapeCollider::Mesh):
            return ShapeCollider::Mesh;
        default:
            // 未知値 (新しい shape を旧コードで読む等) は安全側で Box に倒す
            return ShapeCollider::Box;
        }
    }

    void SetObjectShapeCollider(ObjectInstance& object, ShapeCollider shape) noexcept
    {
        object.shapeCollider = static_cast<std::uint8_t>(shape);
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
            const float yaw = NS::Game::Blocks::BlockRotationToYaw(step);
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
        const float yaw = NS::Game::Blocks::BlockRotationToYaw(static_cast<std::uint8_t>(rotationStep & 0x03));
        const NS::Math::Quaternion rotation = NS::Math::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
        object.rotationX = rotation.x;
        object.rotationY = rotation.y;
        object.rotationZ = rotation.z;
        object.rotationW = rotation.w;
    }

    ObjectInstance MakeGridObject(std::int16_t x, std::int16_t y, std::int16_t z, std::uint8_t rotationStep)
    {
        ObjectInstance object{};
        object.positionX = static_cast<float>(x);
        object.positionY = static_cast<float>(y);
        object.positionZ = static_cast<float>(z);
        object.materialIndex = -1;
        object.flags = kObjectFlagGridAligned;
        SetGridRotationStep(object, rotationStep);
        object.components = NS::Game::Blocks::MaterializeLegacyKind(NS::Game::Blocks::kBlockIdSolid, object);
        return object;
    }

    void MigrateBlocksToObjects(LevelData& level, const std::vector<BlockEntry>& blocks)
    {
        level.objects.reserve(level.objects.size() + blocks.size());
        for (const auto& block : blocks)
        {
            // 旧 blockId が決めていた mesh / 当たり / 拾得を実 component へ起こして積む
            ObjectInstance object = MakeGridObject(block.x, block.y, block.z, block.rotation);
            object.components = NS::Game::Blocks::MaterializeLegacyKind(block.blockId, object);
            level.objects.push_back(std::move(object));
        }
    }

} // namespace NS::Game::Level
