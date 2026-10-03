#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/Model.h"

namespace NS::Obj
{
    class Actor;
    class Scene;
} // namespace NS::Obj

namespace NS::Game::Level
{
    //! @brief ぶつかった場所の床へ寝かせる跡
    //! @details 半透明の板を置き、時間で縮めて消す。消えた跡はシーンが更新の終わりに破棄する
    //! 依存: NS::Obj::Model, NS::Obj::Scene
    class ImpactMark : public NS::Obj::Actor
    {
    public:
        ImpactMark() noexcept;

        NS_REFLECT_NONE(ImpactMark, NS::Obj::Actor)

        //! 指定の位置へ跡の一時オブジェクトを出す。scene が nullptr なら nullptr を返す
        [[nodiscard]] static NS::Obj::Actor* SpawnAt(NS::Obj::Scene* scene, const NS::Core::Vector3& position);

        //! 跡は効果の段で進む
        [[nodiscard]] NS::Obj::UpdatePhase Phase() const noexcept override { return NS::Obj::UpdatePhase::Effects; }

        //! @brief 出た直後の水平の大きさを差し替え、根の倍率へ写す
        //! @param[in] diameter 水平の大きさ。単位は m。有限の正でなければ何も変えない
        void SetDiameter(float diameter) noexcept;
        //! @brief 消えるまでの秒を差し替える
        //! @param[in] seconds 消えるまでの秒。有限の 0 以上でなければ何も変えない
        void SetLifeSeconds(float seconds) noexcept;

    protected:
        //! 経過秒を進めて縮め、寿命が尽きたら退場する
        void VisualStep() override;

    private:
        float m_diameter = 1.5f;    // 出た直後の水平の大きさ
        float m_lifeSeconds = 6.0f; // 消えるまでの秒
        float m_age = 0.0f;         // 出てからの経過秒
    };
} // namespace NS::Game::Level
