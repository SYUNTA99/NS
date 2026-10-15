#include "NSlib/Object/Actors/LightActor.h"

#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/SubObjects/DirectionalLight.h"

namespace NS::Obj
{
    void LightActor::OnInit()
    {
        (void)CreateSubObj(m_light);
        NS::Vector3 initialDirection{-0.3f, -1.0f, -0.2f};
        initialDirection.Normalize();
        Root().SetRotation(NS::Quaternion::FromToRotation(NS::Vector3::UnitZ, initialDirection));
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
} // namespace NS::Obj
