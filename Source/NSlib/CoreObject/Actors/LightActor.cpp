#include "NSlib/CoreObject/Actors/LightActor.h"

#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SubObjects/DirectionalLight.h"

namespace NS
{
    void LightActor::OnInit()
    {
        CreateSubObj(m_light);
        Vector3 initialDirection{-0.3f, -1.0f, -0.2f};
        initialDirection.Normalize();
        Root().SetRotation(Quaternion::FromToRotation(Vector3::UnitZ, initialDirection));
    }

    // 光はプレイの間ずっと登録しておく。消えている間は、描く側が IsActive を見て飛ばす
    void LightActor::OnStart()
    {
        SceneActor::OnStart();
        Scene* scene = OwningScene();
        if (scene != nullptr && m_light != nullptr)
        {
            scene->RegisterLight(m_light.get());
        }
    }

    void LightActor::OnEndPlay()
    {
        SceneActor::OnEndPlay();
        Scene* scene = OwningScene();
        if (scene != nullptr && m_light != nullptr)
        {
            scene->UnregisterLight(m_light.get());
        }
    }

    NS_PLACEABLE(LightActor, "ライト")
} // namespace NS
