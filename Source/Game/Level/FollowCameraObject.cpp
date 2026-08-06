#include "Game/Level/FollowCameraObject.h"

#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/ObjectRef.h"

#include <utility>

namespace NS::Game::Level
{
    using NS::Object::ObjectData;
    using NS::Object::SceneData;

    bool IsFollowCameraObject(const ObjectData& object) noexcept
    {
        return NS::Object::FindComponentEntry(object, "ThirdPersonFollowComponent") != nullptr;
    }

    std::size_t FindFollowCameraObjectIndex(const SceneData& scene) noexcept
    {
        for (std::size_t i = 0; i < scene.objects.size(); ++i)
        {
            if (IsFollowCameraObject(scene.objects[i]))
                return i;
        }
        return NS::Object::k_NoObjectIndex;
    }

    ObjectData MakeFollowCameraObject(std::uint32_t targetObjectId)
    {
        // 値はコード既定を使い、データが持つのは誰を追うかだけ
        nlohmann::json follow = NS::Object::MakeComponentEntry("ThirdPersonFollowComponent");
        NS::Object::SetField(follow, "追従対象", NS::Object::ObjectRef{targetObjectId});

        ObjectData object{};
        object.components = nlohmann::json::array({std::move(follow)});
        // 姿勢は実行時に追従で決まるが、 データ側にも transform を 1 つ持たせる
        NS::Object::EnsureTransformComponent(object);
        return object;
    }

    bool EnsureFollowCameraObject(SceneData& scene, std::uint32_t targetObjectId)
    {
        if (FindFollowCameraObjectIndex(scene) != NS::Object::k_NoObjectIndex)
            return false;
        scene.objects.push_back(MakeFollowCameraObject(targetObjectId));
        NS::Object::EnsureUniqueObjectIds(scene);
        return true;
    }
} // namespace NS::Game::Level
