#include "NSlib/CoreObject/IGet/IGetRenderer.h"
#include "NSlib/Core/Assert.h"

#include "NSlib/CoreObject/System/SceneRenderer.h"

namespace NS
{
    void InitDraw(const IGetRenderer* user, Model* model, const Transform* root)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, model != nullptr, "InitDraw: Model が null");
        NS_ASSERT(Scene, root != nullptr, "InitDraw: 持ち主の根が null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->RegisterModel(model, root);
        }
    }

    void InitDraw(const IGetRenderer* user, Shadow* shadow, const Transform* root)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, shadow != nullptr, "InitDraw: Shadow が null");
        NS_ASSERT(Scene, root != nullptr, "InitDraw: 持ち主の根が null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->RegisterShadow(shadow, root);
        }
    }

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

    void InitOverlay(const IGetRenderer* user, IOverlay* overlay)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, overlay != nullptr, "InitOverlay: 重ね描きが null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->RegisterOverlay(overlay);
        }
    }

    void ShowOverlay(const IGetRenderer* user, const IOverlay* overlay) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, overlay != nullptr, "ShowOverlay: 重ね描きが null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->SetOverlayEnabled(overlay, true);
        }
    }

    void HideOverlay(const IGetRenderer* user, const IOverlay* overlay) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, overlay != nullptr, "HideOverlay: 重ね描きが null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->SetOverlayEnabled(overlay, false);
        }
    }

    void EndOverlay(const IGetRenderer* user, const IOverlay* overlay) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, overlay != nullptr, "EndOverlay: 重ね描きが null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->UnregisterOverlay(overlay);
        }
    }

    void InitLight(const IGetRenderer* user, DirectionalLight* light, const Transform* root)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, light != nullptr, "InitLight: 平行光が null");
        NS_ASSERT(Scene, root != nullptr, "InitLight: 持ち主の根が null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->RegisterLight(light, root);
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
