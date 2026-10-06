#include "Editor/EditorObjects.h"

#include "Game/Level/DeathZone.h"
#include "Game/Player.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Graphics/Mesh.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Components/Collider.h"
#include "NSlib/Object/Components/Model.h"
#include "NSlib/Object/Components/TransformComponent.h"
#include "NSlib/Object/ObjectList.h"
#include "NSlib/Object/Reflection/Archetype.h"
#include "NSlib/Object/Reflection/ComponentEntry.h"
#include "NSlib/Object/Reflection/ObjectBuilder.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"

namespace NS::Editor
{
    namespace
    {
        // cell ブラシの回転値 0..3 を Y 軸 90° 刻みの yaw ラジアンへ写す係数
        constexpr float k_QuarterTurnYaw = NS::k_Pi * 0.5f;

        // クラス名から UI に出す名前を引く。置けるクラスは登録の表示名、置けないクラスはクラス名
        // 戻り値は登録か JSON の文字列を指すので、呼出側が持っている間は切れない
        const char* ClassDisplayName(std::string_view className) noexcept
        {
            if (className.empty())
            {
                return "Actor";
            }
            const NS::Obj::TypeRegistry::Entry* entry = NS::Obj::TypeRegistry::Get().Find(className);
            if (entry == nullptr)
            {
                return "Actor";
            }
            if (entry->label != nullptr)
            {
                return entry->label;
            }
            return entry->className;
        }
    } // namespace

    bool IsCellBrushObject(const nlohmann::json& object) noexcept
    {
        // 自前の振る舞いを持つ Actor は、ブラシの置換や削除で崩さない
        return NS::Obj::ObjectJsonClass(object) == "MapParts";
    }

    bool IsCellBrushObject(const NS::Obj::Actor& object) noexcept
    {
        // 実行時の一時オブジェクトは配置物でないため対象外
        return !object.IsTransient() && std::string_view{object.ClassName()} == "MapParts";
    }

    std::int16_t ObjectCellX(const NS::Obj::Actor& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(object.Root().Position().x));
    }

    std::int16_t ObjectCellY(const NS::Obj::Actor& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(object.Root().Position().y));
    }

    std::int16_t ObjectCellZ(const NS::Obj::Actor& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(object.Root().Position().z));
    }

    std::uint32_t FindObjectIdAtCell(const NS::Obj::ObjectList& objects,
                                     std::int16_t x,
                                     std::int16_t y,
                                     std::int16_t z) noexcept
    {
        for (NS::Obj::Actor* objPtr : objects)
        {
            NS::Obj::Actor& object = *objPtr;
            if (IsCellBrushObject(object) && ObjectCellX(object) == x && ObjectCellY(object) == y &&
                ObjectCellZ(object) == z)
            {
                return object.Id();
            }
        }
        return NS::Obj::k_NoObjectId;
    }

    std::uint8_t CellRotationStep(const nlohmann::json& object) noexcept
    {
        // q と -q は同じ回転なので fabs で符号を無視し、4 候補から一番近いものを選ぶ
        const NS::Quaternion current = NS::Obj::ObjectRotation(object);
        std::uint8_t best = 0;
        float bestDot = -2.0f;
        for (std::uint8_t step = 0; step < 4; ++step)
        {
            const float yaw = static_cast<float>(step) * k_QuarterTurnYaw;
            const NS::Quaternion candidate = NS::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
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

    void SetCellRotationStep(nlohmann::json& object, std::uint8_t rotationStep) noexcept
    {
        const float yaw = static_cast<float>(rotationStep & 0x03) * k_QuarterTurnYaw;
        const NS::Quaternion rotation = NS::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
        NS::Obj::SetObjectRotation(object, rotation);
    }

    bool IsRotatableObject(const nlohmann::json& object)
    {
        // 地形の部品はどれも回せる。球は回しても見た目が変わらないだけ
        return IsCellBrushObject(object);
    }

    const char* ObjectDisplayName(const NS::Obj::Actor& object)
    {
        if (!object.Name().empty())
        {
            return object.Name().c_str();
        }
        return ClassDisplayName(object.ClassName());
    }

    NS::AABB PickLocalBounds(const NS::Obj::Actor& object) noexcept
    {
        const NS::Obj::Model* renderer = object.ModelPart();
        if (renderer != nullptr && renderer->GetMesh() != nullptr)
        {
            return renderer->GetMesh()->LocalBounds();
        }
        return NS::AABB{NS::Vector3{0.0f, 0.0f, 0.0f}, NS::Vector3{0.5f, 0.5f, 0.5f}};
    }

    bool IsPlayerObject(const nlohmann::json& object) noexcept
    {
        return NS::Obj::ObjectJsonClass(object) == ::Player::StaticReflection()->typeName;
    }

    std::size_t FindPlayerObjectIndex(const nlohmann::json& scene) noexcept
    {
        const nlohmann::json& objects = NS::Obj::SceneJsonObjects(scene);
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            if (IsPlayerObject(objects[i]))
            {
                return i;
            }
        }
        return NS::Obj::k_NoObjectIndex;
    }

    nlohmann::json MakePlayerObject(const NS::Vector3& position, const NS::Quaternion& rotation)
    {
        // 構成は Player のコンストラクタが決める。ひな形は型名だけ持ち、値はコード既定を使う
        nlohmann::json object = NS::Obj::MakePrototypeJson<::Player>();
        NS::Obj::SetObjectPosition(object, position);
        NS::Obj::SetObjectRotation(object, rotation);
        return object;
    }

    bool EnsurePlayerObject(nlohmann::json& scene)
    {
        bool created = false;
        if (FindPlayerObjectIndex(scene) == NS::Obj::k_NoObjectIndex)
        {
            // 高さはカプセルの寸法の持ち主 Collider から引く。種類の既定でカプセルを変えても足元が床の上に出る
            const NS::Obj::Actor& baseline =
                NS::Obj::ArchetypeLibrary::Get().Baseline(::Player::StaticReflection()->typeName);
            const ::Player* player = NS::Obj::Cast<::Player>(&baseline);
            if (player == nullptr)
            {
                NS_LOG_ERROR(Game, "プレイヤーの種類の既定を Player として引けず、プレイヤーを補えない");
                return false;
            }
            NS::Obj::SceneJsonObjects(scene).push_back(
                MakePlayerObject(player->DefaultSpawnPosition(), NS::Quaternion{}));
            created = true;
        }

        std::size_t count = 0;
        for (const nlohmann::json& object : NS::Obj::SceneJsonObjects(scene))
        {
            if (IsPlayerObject(object))
            {
                ++count;
            }
        }
        if (count > 1)
        {
            NS_LOG_WARN(Game, "プレイヤーが {} 体ある。先頭の 1 体を正とし、残りは無効として扱う", count);
        }

        // 追従カメラの追従先の欄が id で結ぶので、ここで採番まで済ませる
        NS::Obj::EnsureUniqueObjectIds(scene);
        return created;
    }

    bool IsDeathZoneObject(const nlohmann::json& object) noexcept
    {
        return NS::Obj::ObjectJsonClass(object) == NS::Game::Level::DeathZone::StaticReflection()->typeName;
    }

    nlohmann::json MakeDeathZoneObject()
    {
        nlohmann::json object = NS::Obj::MakePrototypeJson<NS::Game::Level::DeathZone>();
        // 上面 y=-50 は従来の落下死の高さ
        NS::Obj::SetObjectPosition(object, NS::Vector3{0.0f, -55.0f, 0.0f});
        return object;
    }

    bool EnsureDeathZoneObject(nlohmann::json& scene)
    {
        for (const nlohmann::json& object : NS::Obj::SceneJsonObjects(scene))
        {
            if (IsDeathZoneObject(object))
            {
                return false;
            }
        }
        NS::Obj::SceneJsonObjects(scene).push_back(MakeDeathZoneObject());
        NS::Obj::EnsureUniqueObjectIds(scene);
        return true;
    }
} // namespace NS::Editor
