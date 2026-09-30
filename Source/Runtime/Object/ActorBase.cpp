#include "Runtime/Object/ActorBase.h"

#include "Runtime/Object/Scene/Scene.h"

namespace NS::Obj
{
    CameraManager* ActorBase::GetCameraManager() const noexcept
    {
        return m_scene != nullptr ? m_scene->GetCameraManager() : nullptr;
    }

    SceneObjHolder* ActorBase::GetSceneObjHolder() const noexcept
    {
        return m_scene != nullptr ? m_scene->GetSceneObjHolder() : nullptr;
    }

    NS::Phys::PhysicsScene* ActorBase::GetPhysicsScene() const noexcept
    {
        return m_scene != nullptr ? m_scene->GetPhysicsScene() : nullptr;
    }

    NS::Gfx::EffectScene* ActorBase::GetEffectScene() const noexcept
    {
        return m_scene != nullptr ? m_scene->GetEffectScene() : nullptr;
    }
} // namespace NS::Obj
