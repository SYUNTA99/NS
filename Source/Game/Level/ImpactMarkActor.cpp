#include "Game/Level/ImpactMarkActor.h"

#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Windows/Clock.h"

#include <cmath>

namespace GL
{
    namespace Level
    {
        ImpactMarkActor::ImpactMarkActor() noexcept
        {
            Root().SetScale(NS::Vector3{m_diameter, 1.0f, m_diameter});
        }

        void ImpactMarkActor::OnInit()
        {
            NS::Model* model = CreateSubObj(m_model);
            model->SetMeshRef("shadowQuad");
            model->SetMaterialRef("shadow");
        }

        NS::SceneActor* ImpactMarkActor::SpawnAt(NS::Scene* scene, const NS::Vector3& position)
        {
            if (scene == nullptr)
            {
                return nullptr;
            }

            ImpactMarkActor* mark = scene->SpawnTransient<ImpactMarkActor>();
            mark->Root().SetPosition(position);
            return mark;
        }

        void ImpactMarkActor::SetDiameter(float diameter) noexcept
        {
            if (std::isfinite(diameter) && diameter > 0.0f)
            {
                m_diameter = diameter;
                Root().SetScale(NS::Vector3{diameter, 1.0f, diameter});
            }
        }

        void ImpactMarkActor::SetLifeSeconds(float seconds) noexcept
        {
            if (std::isfinite(seconds) && seconds >= 0.0f)
            {
                m_lifeSeconds = seconds;
            }
        }

        void ImpactMarkActor::VisualStep()
        {
            NS::SceneActor::VisualStep();
            if (m_age >= m_lifeSeconds)
            {
                return;
            }
            m_age += NS::OS::FrameTimer::FixedDelta();
            float t = 1.0f;
            if (m_lifeSeconds > 0.0f)
            {
                t = NS::Clamp(m_age / m_lifeSeconds, 0.0f, 1.0f);
            }
            const float size = m_diameter * (1.0f - t);
            Root().SetScale(NS::Vector3{size, 1.0f, size});
            if (t >= 1.0f)
            {
                Destroy();
            }
        }
    } // namespace Level
} // namespace GL
