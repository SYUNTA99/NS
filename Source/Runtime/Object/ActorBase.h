#pragma once

#include "Runtime/Object/IUse/IUseCamera.h"
#include "Runtime/Object/IUse/IUseCollision.h"
#include "Runtime/Object/IUse/IUseEffect.h"
#include "Runtime/Object/IUse/IUseSceneObj.h"
#include "Runtime/Object/IUse/IUseState.h"
#include "Runtime/Object/Object.h"

namespace NS::Obj
{
    class Scene;

    //! @brief 世界に置く Actor と画面に出す UIActor の共通の土台
    //! @details 所属シーンを持ち、窓口 (カメラ・シーンに 1 つの物・地形の当たり・エフェクト) をシーンへ繋ぐ
    //! オデッセイは LiveActor と LayoutActor が窓口だけを共有する。NS は土台を 1 つにし、シーンとの繋ぎを 2 回書かない
    class ActorBase : public Object,
                      public IUseCamera,
                      public IUseSceneObj,
                      public IUseCollision,
                      public IUseEffect,
                      public IUseState
    {
    public:
        ActorBase() noexcept = default;
        ~ActorBase() noexcept override = default;

        //! @brief 世界に出す。更新の段・描画・当たり・センサーへ登録する。出ていれば何もしない
        void Appear();
        //! @brief 世界から外す。出ていなければ何もしない
        //! @details 配置を止めるだけで、ゲームの死ではない。死の意味を足す派生は別の名前の関数から呼ぶ
        void Kill() noexcept;
        [[nodiscard]] bool IsAlive() const noexcept { return m_alive; }

        [[nodiscard]] const std::string& Name() const noexcept { return m_name; }
        NS_REFLECT_NONE(ActorBase, Object)

        //! 所有 Scene。Scene attach 前 / 破棄後は nullptr
        [[nodiscard]] Scene* OwningScene() const noexcept { return m_scene; }
        //! Scene 側が attach 時に呼ぶ。派生から手動で呼ばない
        void AttachScene(Scene* scene) noexcept { m_scene = scene; }

        [[nodiscard]] CameraManager* GetCameraManager() const noexcept override;
        [[nodiscard]] SceneObjHolder* GetSceneObjHolder() const noexcept override;
        [[nodiscard]] NS::Phys::PhysicsScene* GetPhysicsScene() const noexcept override;
        [[nodiscard]] NS::Gfx::EffectScene* GetEffectScene() const noexcept override;
        [[nodiscard]] IStateMachine* GetStateMachine() noexcept override { return nullptr; }
        [[nodiscard]] const IStateMachine* GetStateMachine() const noexcept override { return nullptr; }

    protected:
        virtual void OnAppear() {}
        virtual void OnKill() noexcept {}

    private:
        bool m_alive = false;
        friend class ObjectList;
        void SetName(std::string name) noexcept { m_name = std::move(name); }
        std::string m_name;
        Scene* m_scene = nullptr; // 所有 Scene、attach 前後は nullptr
    };
} // namespace NS::Obj
