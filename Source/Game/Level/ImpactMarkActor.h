#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace NS
{
    class Scene;
}

namespace GL
{
    namespace Level
    {
        //! @brief 床へ置き、時間で縮めて消す半透明の跡
        //! @details 寿命後はシーンが更新の終わりに破棄する
        class ImpactMarkActor : public NS::SceneActor
        {
        public:
            ImpactMarkActor() noexcept;

            NS_REFLECT_NONE(ImpactMarkActor, NS::SceneActor)

            //! 指定の位置へ跡の一時オブジェクトを出す。scene が nullptr なら nullptr を返す
            [[nodiscard]] static NS::SceneActor* SpawnAt(NS::Scene* scene, const NS::Vector3& position);

            [[nodiscard]] NS::UpdatePhase Phase() const noexcept override { return NS::UpdatePhase::Effects; }

            //! @brief 出た直後の水平の大きさを差し替え、根の倍率へ写す
            //! @param[in] diameter 水平の大きさ。単位は m。有限の正でなければ何も変えない
            void SetDiameter(float diameter) noexcept;
            //! @brief 消えるまでの秒を差し替える
            //! @param[in] seconds 消えるまでの秒。有限の 0 以上でなければ何も変えない
            void SetLifeSeconds(float seconds) noexcept;

        protected:
            void OnInit() override;
            void VisualStep() override;

        private:
            float m_diameter = 1.5f; // 出た直後の水平の大きさ
            float m_lifeSeconds = 6.0f;
            float m_age = 0.0f; // 出てからの経過秒
        };
    } // namespace Level
} // namespace GL
