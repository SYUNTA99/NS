#include "Editor/EditorObjects.h"

#include "Game/Level/DeathZone.h"
#include "Game/Player.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Graphics/Mesh.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/ActorList.h"
#include "NSlib/Object/Reflection/Archetype.h"
#include "NSlib/Object/Reflection/ObjectBuilder.h"
#include "NSlib/Object/Reflection/SubObjectEntry.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/SubObjects/Collider.h"
#include "NSlib/Object/SubObjects/Model.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"

#include <algorithm>

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

        bool IsPlayerObject(const nlohmann::json& object) noexcept
        {
            return NS::Obj::ObjectJsonClass(object) == ::Player::StaticReflection()->typeName;
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
    } // namespace

    bool IsCellBrushObject(const NS::Obj::Actor& object) noexcept
    {
        // 実行時の一時オブジェクトは配置物でないため対象外
        // 自前の振る舞いを持つ Actor は、ブラシの置換や削除で崩さない
        if (object.IsTransient() || std::string_view{object.ClassName()} != "MapParts")
        {
            return false;
        }
        const NS::Obj::Model* model = object.ModelSubObj();
        if (model == nullptr)
        {
            return false;
        }
        const std::string_view mesh = model->MeshRef();
        return mesh == "cube" || mesh == "wedge45";
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

    std::uint32_t FindObjectIdAtCell(const NS::Obj::ActorList& objects,
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

    bool HasPlacedObjectAtCell(const NS::Obj::ActorList& objects,
                               std::int16_t x,
                               std::int16_t y,
                               std::int16_t z) noexcept
    {
        for (const NS::Obj::Actor* object : objects)
        {
            if (!object->IsTransient() && ObjectCellX(*object) == x && ObjectCellY(*object) == y &&
                ObjectCellZ(*object) == z)
            {
                return true;
            }
        }
        return false;
    }

    void AddCellQuarterTurn(nlohmann::json& object) noexcept
    {
        // 今の回転の後に世界の Y 軸まわりを掛ける。段へ丸めると傾きが消える
        const NS::Quaternion quarter = NS::Quaternion::CreateFromYawPitchRoll(k_QuarterTurnYaw, 0.0f, 0.0f);
        NS::Obj::SetObjectRotation(object, NS::Obj::ObjectRotation(object) * quarter);
    }

    void SetCellRotationStep(nlohmann::json& object, std::uint8_t rotationStep) noexcept
    {
        const float yaw = static_cast<float>(rotationStep & 0x03) * k_QuarterTurnYaw;
        const NS::Quaternion rotation = NS::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
        NS::Obj::SetObjectRotation(object, rotation);
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
        const NS::Obj::Model* renderer = object.ModelSubObj();
        if (renderer != nullptr && renderer->GetMesh() != nullptr)
        {
            return renderer->GetMesh()->LocalBounds();
        }
        return NS::AABB{NS::Vector3{0.0f, 0.0f, 0.0f}, NS::Vector3{0.5f, 0.5f, 0.5f}};
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

        const nlohmann::json& objects = NS::Obj::SceneJsonObjects(scene);
        const std::ptrdiff_t count = std::count_if(objects.begin(), objects.end(), IsPlayerObject);
        if (count > 1)
        {
            NS_LOG_WARN(Game, "プレイヤーが {} 体ある。先頭の 1 体を正とし、残りは無効として扱う", count);
        }

        // 追従カメラの追従先の欄が id で結ぶので、ここで採番まで済ませる
        NS::Obj::EnsureUniqueObjectIds(scene);
        return created;
    }

    bool EnsureDeathZoneObject(nlohmann::json& scene)
    {
        const nlohmann::json& objects = NS::Obj::SceneJsonObjects(scene);
        if (std::any_of(objects.begin(), objects.end(), IsDeathZoneObject))
        {
            return false;
        }
        NS::Obj::SceneJsonObjects(scene).push_back(MakeDeathZoneObject());
        NS::Obj::EnsureUniqueObjectIds(scene);
        return true;
    }
} // namespace NS::Editor
