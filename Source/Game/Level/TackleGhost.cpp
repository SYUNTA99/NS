#include "Game/Level/TackleGhost.h"
#include "NSlib/CoreObject/ChildActorObj.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace GL
{
    namespace Level
    {
        void TackleGhost::Start(const NS::HitOwnerRun& run, const NS::SceneActor* owner)
        {
            NS_ASSERT(Game, owner != nullptr, "残像に親アクターが無い");
            if (run.length <= 0)
            {
                return;
            }
            const std::optional<NS::CameraPose> pose = NS::GetCameraViewPose(owner);
            if (!pose.has_value())
            {
                return;
            }
            const NS::HitOwnerItem& item = run.item;
            const NS::Vector3 forward = NS::CalcCameraForwardHorizontal(owner);
            m_desc = TackleGhostDesc{
                .axis = NS::Vector3{forward.z, 0.0f, -forward.x},
                .spread =
                    NS::ScreenPixelsToMeters(HitOwnerValue(item, "離れのピクセル", 0.0f), *pose, owner->Root().Position()),
                .sideOpacity = HitOwnerValue(item, "左右の残像の不透明度", 0.0f),
                .stopLength = run.length,
                .releaseFrames = HitOwnerFrames(item, "明けの残像のフレーム数", 0),
                .releaseOpacity = HitOwnerValue(item, "明けの残像の不透明度", 0.0f)};
            m_frame = 0;
            m_running = true;
        }

        void TackleGhost::Advance(NS::SceneActor* owner)
        {
            NS_ASSERT(Game, owner != nullptr, "残像の持ち主が渡されていない");
            NS::ChildActorObj* child = owner->ChildActor();
            if (child == nullptr || !child->IsAppeared())
            {
                return;
            }
            child->Root().CopyFrom(owner->Root());
            if (m_running)
            {
                ++m_frame;
                m_running = m_desc.WriteTo(this, m_frame, owner->ModelClassObj(), owner->Root().WorldMatrix());
            }
            std::array<NS::ModelDraw, 3> draws{};
            std::size_t count = 0;
            if (m_spread.LengthSquared() > 0.0f)
            {
                for (const float side : {1.0f, -1.0f})
                {
                    draws[count++] = NS::ModelDraw{.offset = m_spread * side,
                                                   .opacity = m_sideOpacity,
                                                   .groundWave = false};
                }
            }
            if (m_afterimageOpacity > 0.0f)
            {
                draws[count++] = NS::ModelDraw{.world = m_afterimageWorld,
                                               .opacity = m_afterimageOpacity,
                                               .groundWave = false,
                                               .tremor = false};
            }
            child->SetModelDraws(std::span<const NS::ModelDraw>{draws.data(), count});
        }

        void TackleGhost::Stop(NS::SceneActor* owner) noexcept
        {
            NS_ASSERT(Game, owner != nullptr, "残像の持ち主が渡されていない");
            m_running = false;
            (void)SetSpread(NS::Vector3{});
            (void)SetAfterimageOpacity(0.0f);
            if (owner->ChildActor() != nullptr)
            {
                owner->ChildActor()->SetModelDraws({});
            }
        }

        void TackleGhost::ScaleOpacity(float scale) noexcept
        {
            m_desc.sideOpacity *= scale;
            m_desc.releaseOpacity *= scale;
        }

        bool TackleGhost::SetSpread(const NS::Vector3& spread) noexcept
        {
            if (!NS::IsFinite(spread))
            {
                return false;
            }
            m_spread = spread;
            return true;
        }

        bool TackleGhost::SetSideOpacity(float opacity) noexcept
        {
            if (!std::isfinite(opacity))
            {
                return false;
            }
            m_sideOpacity = std::clamp(opacity, 0.0f, 1.0f);
            return true;
        }

        NS::Matrix TackleGhost::SideWorldMatrix(const NS::Model* model,
                                                const NS::Matrix& root,
                                                float alpha,
                                                float side) const noexcept
        {
            NS_ASSERT(Game, model != nullptr, "残像の元の Model が渡されていない");
            return model->DrawWorldMatrixWithOffset(root, alpha, m_spread * side);
        }

        void TackleGhost::CaptureAfterimage(const NS::Model* model, const NS::Matrix& root) noexcept
        {
            NS_ASSERT(Game, model != nullptr, "残像の元の Model が渡されていない");
            m_afterimageWorld = model->DrawWorldMatrix(root, 1.0f);
        }

        bool TackleGhost::SetAfterimageOpacity(float opacity) noexcept
        {
            if (!std::isfinite(opacity))
            {
                return false;
            }
            m_afterimageOpacity = std::clamp(opacity, 0.0f, 1.0f);
            return true;
        }

    } // namespace Level
} // namespace GL
