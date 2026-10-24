#include "NSlib/CoreObject/IUse/IUseRenderer.h"
#include "NSlib/Core/Assert.h"

#include "NSlib/CoreObject/ClassObjs/Shadow.h"
#include "NSlib/CoreObject/System/SceneRenderer.h"

namespace NS
{
    void InitDraw(const IUseRenderer* user, IRenderable* renderable, const Transform* root)
    {
        InitDraw(user, renderable, root, static_cast<const Model*>(nullptr));
    }

    void InitDraw(const IUseRenderer* user, IRenderable* renderable, const Transform* root, const Model* model)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, renderable != nullptr, "InitDraw: 描く物が null");
        NS_ASSERT(Scene, root != nullptr, "InitDraw: 持ち主の根が null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->RegisterRenderable(renderable, root, model);
        }
    }

    void InitDraw(const IUseRenderer* user, Shadow* shadow, const Transform* root, const IUseCollision* collision)
    {
        NS_ASSERT(Scene, shadow != nullptr, "InitDraw: 影が null");
        shadow->UseCollision(collision);
        InitDraw(user, static_cast<IRenderable*>(shadow), root);
    }

    void ShowModel(const IUseRenderer* user, const IRenderable* renderable) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->SetRenderableEnabled(renderable, true);
        }
    }

    void HideModel(const IUseRenderer* user, const IRenderable* renderable) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->SetRenderableEnabled(renderable, false);
        }
    }

    void EndDraw(const IUseRenderer* user, const IRenderable* renderable) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->UnregisterRenderable(renderable);
        }
    }

    void InitOverlay(const IUseRenderer* user, IOverlay* overlay)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, overlay != nullptr, "InitOverlay: 重ね描きが null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->RegisterOverlay(overlay);
        }
    }

    void ShowOverlay(const IUseRenderer* user, const IOverlay* overlay) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->SetOverlayEnabled(overlay, true);
        }
    }

    void HideOverlay(const IUseRenderer* user, const IOverlay* overlay) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->SetOverlayEnabled(overlay, false);
        }
    }

    void EndOverlay(const IUseRenderer* user, const IOverlay* overlay) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->UnregisterOverlay(overlay);
        }
    }

    void InitLight(const IUseRenderer* user, DirectionalLight* light, const Transform* root)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        NS_ASSERT(Scene, light != nullptr, "InitLight: 平行光が null");
        NS_ASSERT(Scene, root != nullptr, "InitLight: 持ち主の根が null");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->RegisterLight(light, root);
        }
    }

    void EnableLight(const IUseRenderer* user, const DirectionalLight* light) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->SetLightEnabled(light, true);
        }
    }

    void DisableLight(const IUseRenderer* user, const DirectionalLight* light) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->SetLightEnabled(light, false);
        }
    }

    void EndLight(const IUseRenderer* user, const DirectionalLight* light) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        if (SceneRenderer* renderer = user->GetSceneRenderer())
        {
            renderer->UnregisterLight(light);
        }
    }
} // namespace NS
