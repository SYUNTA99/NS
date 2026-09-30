#pragma once

#include "Runtime/Object/IUseCamera.h"
#include "Runtime/Object/IUseCollision.h"
#include "Runtime/Object/IUseEffect.h"
#include "Runtime/Object/IUseSceneObj.h"
#include "Runtime/Object/Object.h"

namespace NS::Obj
{
    class Scene;

    //! @brief 世界に置く Actor と画面に出す UIActor の共通の土台
    //! @details 所属シーンを持ち、窓口 (カメラ・シーンに 1 つの物・地形の当たり・エフェクト) をシーンへ繋ぐ
    //! オデッセイは LiveActor と LayoutActor が窓口だけを共有する。NS は土台を 1 つにし、シーンとの繋ぎを 2 回書かない
    class ActorBase : public Object, public IUseCamera, public IUseSceneObj, public IUseCollision, public IUseEffect
    {
    public:
        ActorBase() noexcept = default;
        ~ActorBase() noexcept override = default;

        //! 所有 Scene。Scene attach 前 / 破棄後は nullptr
        [[nodiscard]] Scene* OwningScene() const noexcept { return m_scene; }
        //! Scene 側が attach 時に呼ぶ。派生から手動で呼ばない
        void AttachScene(Scene* scene) noexcept { m_scene = scene; }

        [[nodiscard]] CameraManager* GetCameraManager() const noexcept override;
        [[nodiscard]] SceneObjHolder* GetSceneObjHolder() const noexcept override;
        [[nodiscard]] NS::Phys::PhysicsScene* GetPhysicsScene() const noexcept override;
        [[nodiscard]] NS::Gfx::EffectScene* GetEffectScene() const noexcept override;

    private:
        Scene* m_scene = nullptr; // 所有 Scene、attach 前後は nullptr
    };
} // namespace NS::Obj
