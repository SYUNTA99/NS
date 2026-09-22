#include "Game/Level/FollowCameraObject.h"

#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/ObjectRef.h"

#include <utility>

namespace NS::Game::Level
{
    using NS::Obj::ObjectData;
    using NS::Obj::SceneData;

    bool IsFollowCameraObject(const ObjectData& object) noexcept
    {
        return NS::Obj::FindComponentEntry(object, "ThirdPersonFollow") != nullptr;
    }

    std::size_t FindFollowCameraObjectIndex(const SceneData& scene) noexcept
    {
        for (std::size_t i = 0; i < scene.objects.size(); ++i)
        {
            if (IsFollowCameraObject(scene.objects[i]))
            {
                return i;
            }
        }
        return NS::Obj::k_NoObjectIndex;
    }

    ObjectData MakeFollowCameraObject(std::uint32_t targetObjectId)
    {
        // 値はコード既定を使い、データが持つのは誰を追うかだけ
        nlohmann::json follow = NS::Obj::MakeComponentEntry("ThirdPersonFollow");
        NS::Obj::SetField(follow, "追従対象", NS::Obj::ObjectRef{targetObjectId});

        ObjectData object{};
        object.components = nlohmann::json::array({std::move(follow)});
        // 姿勢は実行時に追従で決まるが、データ側にも transform を 1 つ持たせる
        NS::Obj::EnsureTransformComponent(object);
        return object;
    }

    bool EnsureFollowCameraObject(SceneData& scene, std::uint32_t targetObjectId)
    {
        if (FindFollowCameraObjectIndex(scene) != NS::Obj::k_NoObjectIndex)
        {
            return false;
        }

        scene.objects.push_back(MakeFollowCameraObject(targetObjectId));
        NS::Obj::EnsureUniqueObjectIds(scene);
        return true;
    }
} // namespace NS::Game::Level
