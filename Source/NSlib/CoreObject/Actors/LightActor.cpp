#include "NSlib/CoreObject/Actors/LightActor.h"

#include "NSlib/CoreObject/ClassObjs/DirectionalLight.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"

namespace NS
{
    void LightActor::OnInit()
    {
        CreateClassObj(m_light);
        InitLight(this, m_light.get(), &Root());
        Vector3 initialDirection{-0.3f, -1.0f, -0.2f};
        initialDirection.Normalize();
        Root().SetRotation(Quaternion::FromToRotation(Vector3::UnitZ, initialDirection));
    }

    void LightActor::OnAppear()
    {
        SceneActor::OnAppear();
        if (IsAppeared())
        {
            EnableLight(this, m_light.get());
        }
    }

    void LightActor::OnDisappear() noexcept
    {
        SceneActor::OnDisappear();
        DisableLight(this, m_light.get());
    }

    void LightActor::OnEndPlay()
    {
        SceneActor::OnEndPlay();
        EndLight(this, m_light.get());
    }

    NS_PLACEABLE(LightActor, "ライト")
} // namespace NS
