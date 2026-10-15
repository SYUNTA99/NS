#include "Editor/EditorObjects.h"
#include "NSlib/Core/Types.h"

#include "Game/Level/DeathZoneActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Reflection/Archetype.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/CoreObject/Reflection/SubObjectEntry.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/Collider.h"
#include "NSlib/CoreObject/SubObjects/Model.h"
#include "NSlib/CoreObject/SubObjects/Transform.h"
#include "NSlib/Graphics/Mesh.h"

#include <algorithm>

namespace NS
{
    namespace Editor
    {
        namespace
        {
            // cell ブラシの回転値 0..3 を Y 軸 90° 刻みの yaw ラジアンへ写す係数
            constexpr float k_QuarterTurnYaw = NS::k_Pi * 0.5f;

            // 登録の表示名、なければクラス名。未登録は SceneActor。返す文字列は登録情報が所有する
            const char* ClassDisplayName(std::string_view className) noexcept
            {
                if (className.empty())
                {
                    return "SceneActor";
                }
                const NS::TypeRegistry::Entry* entry = NS::TypeRegistry::Get().Find(className);
                if (entry == nullptr)
                {
                    return "SceneActor";
                }
                if (entry->label != nullptr)
                {
                    return entry->label;
                }
                return entry->className;
            }

            bool IsPlayerObject(const nlohmann::json& object) noexcept
            {
                return NS::ObjectJsonClass(object) == ::PlayerActor::StaticReflection()->typeName;
            }

            bool IsDeathZoneObject(const nlohmann::json& object) noexcept
            {
                return NS::ObjectJsonClass(object) == GL::Level::DeathZoneActor::StaticReflection()->typeName;
            }

            nlohmann::json MakeDeathZoneObject()
            {
                nlohmann::json object = NS::MakePrototypeJson<GL::Level::DeathZoneActor>();
                // 上面 y=-50 は従来の落下死の高さ
                NS::SetObjectPosition(object, NS::Vector3{0.0f, -55.0f, 0.0f});
                return object;
            }
        } // namespace

        bool IsCellBrushObject(const NS::SceneActor& object) noexcept
        {
            // 実行時の一時オブジェクトは配置物でないため対象外
            // 自前の振る舞いを持つ SceneActor は、ブラシの置換や削除で崩さない
            if (object.IsTransient() || std::string_view{object.ClassName()} != "MapPartsActor")
            {
                return false;
            }
            const NS::Model* model = object.ModelSubObj();
            if (model == nullptr)
            {
                return false;
            }
            const std::string_view mesh = model->MeshRef();
            return mesh == "cube" || mesh == "wedge45";
        }

        NS::int16 ObjectCellX(const NS::SceneActor& object) noexcept
        {
            return static_cast<NS::int16>(std::lround(object.Root().Position().x));
        }

        NS::int16 ObjectCellY(const NS::SceneActor& object) noexcept
        {
            return static_cast<NS::int16>(std::lround(object.Root().Position().y));
        }

        NS::int16 ObjectCellZ(const NS::SceneActor& object) noexcept
        {
            return static_cast<NS::int16>(std::lround(object.Root().Position().z));
        }

        NS::uint32 FindObjectIdAtCell(const NS::ActorList& objects, NS::int16 x, NS::int16 y, NS::int16 z) noexcept
        {
            for (NS::SceneActor* objPtr : objects)
            {
                NS::SceneActor& object = *objPtr;
                if (IsCellBrushObject(object) && ObjectCellX(object) == x && ObjectCellY(object) == y &&
                    ObjectCellZ(object) == z)
                {
                    return object.Id();
                }
            }
            return NS::k_NoObjectId;
        }

        bool HasPlacedObjectAtCell(const NS::ActorList& objects, NS::int16 x, NS::int16 y, NS::int16 z) noexcept
        {
            for (const NS::SceneActor* object : objects)
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
            NS::SetObjectRotation(object, NS::ObjectRotation(object) * quarter);
        }

        void SetCellRotationStep(nlohmann::json& object, NS::uint8 rotationStep) noexcept
        {
            const float yaw = static_cast<float>(rotationStep & 0x03) * k_QuarterTurnYaw;
            const NS::Quaternion rotation = NS::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
            NS::SetObjectRotation(object, rotation);
        }

        const char* ObjectDisplayName(const NS::SceneActor& object)
        {
            if (!object.Name().empty())
            {
                return object.Name().c_str();
            }
            return ClassDisplayName(object.ClassName());
        }

        NS::AABB PickLocalBounds(const NS::SceneActor& object) noexcept
        {
            const NS::Model* renderer = object.ModelSubObj();
            if (renderer != nullptr && renderer->GetMesh() != nullptr)
            {
                return renderer->GetMesh()->LocalBounds();
            }
            return NS::AABB{NS::Vector3{0.0f, 0.0f, 0.0f}, NS::Vector3{0.5f, 0.5f, 0.5f}};
        }

        std::size_t FindPlayerObjectIndex(const nlohmann::json& scene) noexcept
        {
            const nlohmann::json& objects = NS::SceneJsonObjects(scene);
            for (std::size_t i = 0; i < objects.size(); ++i)
            {
                if (IsPlayerObject(objects[i]))
                {
                    return i;
                }
            }
            return NS::k_NoObjectIndex;
        }

        nlohmann::json MakePlayerObject(const NS::Vector3& position, const NS::Quaternion& rotation)
        {
            nlohmann::json object = NS::MakePrototypeJson<::PlayerActor>();
            NS::SetObjectPosition(object, position);
            NS::SetObjectRotation(object, rotation);
            return object;
        }

        bool EnsurePlayerObject(nlohmann::json& scene)
        {
            bool created = false;
            if (FindPlayerObjectIndex(scene) == NS::k_NoObjectIndex)
            {
                // 高さはカプセルの寸法の持ち主 Collider から引く。種類の既定でカプセルを変えても足元が床の上に出る
                const NS::SceneActor& baseline =
                    NS::ArchetypeLibrary::Get().Baseline(::PlayerActor::StaticReflection()->typeName);
                const ::PlayerActor* player = NS::Cast<::PlayerActor>(&baseline);
                if (player == nullptr)
                {
                    NS_LOG_ERROR(Game, "プレイヤーの種類の既定を PlayerActor として引けず、プレイヤーを補えない");
                    return false;
                }
                NS::SceneJsonObjects(scene).push_back(
                    MakePlayerObject(player->DefaultSpawnPosition(), NS::Quaternion{}));
                created = true;
            }

            const nlohmann::json& objects = NS::SceneJsonObjects(scene);
            const std::ptrdiff_t count = std::count_if(objects.begin(), objects.end(), IsPlayerObject);
            if (count > 1)
            {
                NS_LOG_WARN(Game, "プレイヤーが {} 体ある。先頭の 1 体を正とし、残りは無効として扱う", count);
            }

            // 追従カメラの追従先の欄が id で結ぶので、ここで採番まで済ませる
            NS::EnsureUniqueObjectIds(scene);
            return created;
        }

        bool EnsureDeathZoneObject(nlohmann::json& scene)
        {
            const nlohmann::json& objects = NS::SceneJsonObjects(scene);
            if (std::any_of(objects.begin(), objects.end(), IsDeathZoneObject))
            {
                return false;
            }
            NS::SceneJsonObjects(scene).push_back(MakeDeathZoneObject());
            NS::EnsureUniqueObjectIds(scene);
            return true;
        }
    } // namespace Editor
} // namespace NS
