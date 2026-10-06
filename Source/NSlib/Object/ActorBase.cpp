#include "NSlib/Object/ActorBase.h"

#include "NSlib/Object/Scene/Scene.h"

namespace NS::Obj
{
    void ActorBase::Appear()
    {
        if (m_alive)
        {
            return;
        }
        m_alive = true;
        OnAppear();
    }

    void ActorBase::Kill() noexcept
    {
        if (!m_alive)
        {
            return;
        }
        m_alive = false;
        OnKill();
    }

    CameraManager* ActorBase::GetCameraManager() const noexcept
    {
        if (m_scene == nullptr)
        {
            return nullptr;
        }
        return m_scene->GetCameraManager();
    }

    SceneObjHolder* ActorBase::GetSceneObjHolder() const noexcept
    {
        if (m_scene == nullptr)
        {
            return nullptr;
        }
        return m_scene->GetSceneObjHolder();
    }

    NS::Phys::PhysicsScene* ActorBase::GetPhysicsScene() const noexcept
    {
        if (m_scene == nullptr)
        {
            return nullptr;
        }
        return m_scene->GetPhysicsScene();
    }

    NS::Gfx::EffectScene* ActorBase::GetEffectScene() const noexcept
    {
        if (m_scene == nullptr)
        {
            return nullptr;
        }
        return m_scene->GetEffectScene();
    }
} // namespace NS::Obj
