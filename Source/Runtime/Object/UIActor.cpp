#include "Runtime/Object/UIActor.h"

#include "Runtime/Object/Scene/Scene.h"

namespace NS::Obj
{
    UIActor::~UIActor() noexcept
    {
        Close();
    }

    void UIActor::Open(Scene& scene)
    {
        if (IsOpen())
        {
            return;
        }
        AttachScene(&scene);
        scene.RegisterUIActor(this);
    }

    void UIActor::Close() noexcept
    {
        Scene* scene = OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        scene->UnregisterUIActor(this);
        AttachScene(nullptr);
    }
} // namespace NS::Obj
