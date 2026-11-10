#include "NSlib/CoreObject/IGet/IGetRenderer.h"
#include "NSlib/Core/Assert.h"

#include "NSlib/CoreObject/System/SceneRenderer.h"

namespace NS
{
    void ShowModel(const IGetRenderer* user, const Model* model) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, model != nullptr, "ShowModel: Model が null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->SetModelEnabled(model, true);
        }
    }

    void ShowModel(const IGetRenderer* user, const Shadow* shadow) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, shadow != nullptr, "ShowModel: Shadow が null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->SetShadowEnabled(shadow, true);
        }
    }

    void HideModel(const IGetRenderer* user, const Model* model) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, model != nullptr, "HideModel: Model が null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->SetModelEnabled(model, false);
        }
    }

    void HideModel(const IGetRenderer* user, const Shadow* shadow) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, shadow != nullptr, "HideModel: Shadow が null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->SetShadowEnabled(shadow, false);
        }
    }

    void EndDraw(const IGetRenderer* user, const Model* model) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, model != nullptr, "EndDraw: Model が null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->UnregisterModel(model);
        }
    }

    void EndDraw(const IGetRenderer* user, const Shadow* shadow) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, shadow != nullptr, "EndDraw: Shadow が null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->UnregisterShadow(shadow);
        }
    }

    void ShowModel(const IGetRenderer* user, const WorldLines* lines) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, lines != nullptr, "ShowModel: WorldLines が null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->SetWorldLinesEnabled(lines, true);
        }
    }

    void HideModel(const IGetRenderer* user, const WorldLines* lines) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, lines != nullptr, "HideModel: WorldLines が null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->SetWorldLinesEnabled(lines, false);
        }
    }

    void EndDraw(const IGetRenderer* user, const WorldLines* lines) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, lines != nullptr, "EndDraw: WorldLines が null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->UnregisterWorldLines(lines);
        }
    }

    void EnableLight(const IGetRenderer* user, const DirectionalLight* light) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, light != nullptr, "EnableLight: 平行光が null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->SetLightEnabled(light, true);
        }
    }

    void DisableLight(const IGetRenderer* user, const DirectionalLight* light) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, light != nullptr, "DisableLight: 平行光が null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->SetLightEnabled(light, false);
        }
    }

    void EndLight(const IGetRenderer* user, const DirectionalLight* light) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, light != nullptr, "EndLight: 平行光が null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->UnregisterLight(light);
        }
    }
} // namespace NS
