#include "Game/Level/LevelObjects.h"

#include "Game/Blocks/BuildPlacedObject.h"

namespace NS::Game::Level
{
    namespace
    {
        // cell ブラシの回転値 0..3 を Y 軸 90° 刻みの yaw ラジアンへ写す。 描画 / 当たり / 往復が同じ向き基準を共有する
        constexpr float kQuarterTurnYaw = NS::Math::kPi * 0.5f;
    } // namespace

    bool IsPlayerObject(const NS::Scene::ObjectData& object) noexcept
    {
        return FindComponentData(object, "PlayerInputComponent") != nullptr;
    }

    std::size_t FindPlayerObjectIndex(const NS::Scene::SceneData& level) noexcept
    {
        for (std::size_t i = 0; i < level.objects.size(); ++i)
        {
            if (IsPlayerObject(level.objects[i]))
                return i;
        }
        return NS::Scene::kNoObjectIndex;
    }

    NS::Scene::ObjectData MakePlayerObject(const NS::Math::Vector3& position, const NS::Math::Quaternion& rotation)
    {
        NS::Scene::ObjectData object{};
        object.positionX = position.x;
        object.positionY = position.y;
        object.positionZ = position.z;
        object.rotationX = rotation.x;
        object.rotationY = rotation.y;
        object.rotationZ = rotation.z;
        object.rotationW = rotation.w;
        // cube mesh の半サイズ 0.5 を capsule 当たり radius 0.4 / 半高 0.9 の AABB に合わせる縮み
        object.scaleX = 0.8f;
        object.scaleY = 1.8f;
        object.scaleZ = 0.8f;
        object.materialIndex = -1;
        object.components = NS::Game::Blocks::MakeDefaultPlayerComponents();
        return object;
    }

    bool IsFollowCameraObject(const NS::Scene::ObjectData& object) noexcept
    {
        return FindComponentData(object, "ThirdPersonFollowComponent") != nullptr;
    }

    std::size_t FindFollowCameraObjectIndex(const NS::Scene::SceneData& level) noexcept
    {
        for (std::size_t i = 0; i < level.objects.size(); ++i)
        {
            if (IsFollowCameraObject(level.objects[i]))
                return i;
        }
        return NS::Scene::kNoObjectIndex;
    }

    NS::Scene::ObjectData MakeFollowCameraObject(std::uint32_t targetObjectId)
    {
        NS::Scene::ObjectData object{};
        object.materialIndex = -1;
        object.components = NS::Game::Blocks::MakeFollowCameraComponents(targetObjectId);
        return object;
    }

    std::int16_t ObjectCellX(const NS::Scene::ObjectData& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(object.positionX));
    }

    std::int16_t ObjectCellY(const NS::Scene::ObjectData& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(object.positionY));
    }

    std::int16_t ObjectCellZ(const NS::Scene::ObjectData& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(object.positionZ));
    }

    bool IsCellBrushObject(const NS::Scene::ObjectData& object) noexcept
    {
        // プレイヤーとカメラはギズモ / 別経路で扱うため cell ブラシの対象から外す
        return !IsPlayerObject(object) && !IsFollowCameraObject(object) &&
               FindComponentData(object, "PlacedVirtualCamera") == nullptr;
    }

    std::size_t FindObjectAtCell(const NS::Scene::SceneData& level, std::int16_t x, std::int16_t y, std::int16_t z) noexcept
    {
        for (std::size_t i = 0; i < level.objects.size(); ++i)
        {
            const auto& object = level.objects[i];
            if (IsCellBrushObject(object) && ObjectCellX(object) == x && ObjectCellY(object) == y &&
                ObjectCellZ(object) == z)
            {
                return i;
            }
        }
        return NS::Scene::kNoObjectIndex;
    }

    std::uint8_t CellRotationStep(const NS::Scene::ObjectData& object) noexcept
    {
        // q と -q は同一回転なので fabs で符号を無視し 4 候補の最近接を選ぶ。 四半回転の向き規約に依存しない
        const NS::Math::Quaternion current{object.rotationX, object.rotationY, object.rotationZ, object.rotationW};
        std::uint8_t best = 0;
        float bestDot = -2.0f;
        for (std::uint8_t step = 0; step < 4; ++step)
        {
            const float yaw = static_cast<float>(step) * kQuarterTurnYaw;
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

    void SetCellRotationStep(NS::Scene::ObjectData& object, std::uint8_t rotationStep) noexcept
    {
        const float yaw = static_cast<float>(rotationStep & 0x03) * kQuarterTurnYaw;
        const NS::Math::Quaternion rotation = NS::Math::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
        object.rotationX = rotation.x;
        object.rotationY = rotation.y;
        object.rotationZ = rotation.z;
        object.rotationW = rotation.w;
    }

    NS::Scene::ObjectData MakeCellObject(std::int16_t x, std::int16_t y, std::int16_t z, std::uint8_t rotationStep)
    {
        NS::Scene::ObjectData object{};
        object.positionX = static_cast<float>(x);
        object.positionY = static_cast<float>(y);
        object.positionZ = static_cast<float>(z);
        object.materialIndex = -1;
        SetCellRotationStep(object, rotationStep);
        object.components = NS::Game::Blocks::MakeCellCubeComponents();
        return object;
    }

    int PickupKindOf(const NS::Scene::ObjectData& object) noexcept
    {
        const NS::Scene::ComponentData* pickup = FindComponentData(object, "PickupComponent");
        if (pickup == nullptr)
            return -1;
        const NS::Scene::FieldValue* field = FindField(*pickup, "Pickup Kind");
        if (field != nullptr && std::holds_alternative<int>(field->value))
            return std::get<int>(field->value);
        return 0; // PickupComponent はあるが field 欠損 → コイン既定
    }

} // namespace NS::Game::Level
