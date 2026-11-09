#include "Game/Level/TackleGhost.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IGet/IGetCamera.h"
#include "NSlib/CoreObject/IGet/IGetRenderer.h"

#include <algorithm>
#include <cmath>

namespace GL
{
    namespace Level
    {
        void TackleGhost::Start(const NS::HitOwnerRun& run)
        {
            const NS::SceneActor* owner = ParentActor();
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
            const NS::HitOwnerInfo& item = run.item;
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

        void TackleGhost::Stop() noexcept
        {
            m_running = false;
            (void)SetSpread(NS::Vector3{});
            (void)SetAfterimageOpacity(0.0f);
            m_draws.clear();
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

        void TackleGhost::Collect(const NS::Gfx::RenderContext& context,
                                  const NS::RenderSource& source,
                                  std::vector<NS::Gfx::DrawItem>& out)
        {
            const NS::Model* model = ParentModel();
            if (m_drawRegistered && model != nullptr)
            {
                model->CollectDraws(context, source, m_draws, out);
            }
        }

        NS::AABB TackleGhost::WorldBounds(const NS::RenderSource& source) const noexcept
        {
            const NS::Model* model = ParentModel();
            if (m_drawRegistered && model != nullptr)
            {
                return model->DrawsWorldBounds(source, m_draws);
            }
            return {};
        }

        NS::Vector3 TackleGhost::SortCenter(const NS::RenderSource& source) const noexcept
        {
            const NS::Model* model = ParentModel();
            if (m_drawRegistered && model != nullptr)
            {
                return model->SortCenter(source);
            }
            return source.root.Translation();
        }

        void TackleGhost::PrepareRender()
        {
            // 親の根は、親の更新の後に届いた衝突の知らせでも動く。描く直前に補間の前後をそろえる
            NS_ASSERT(Game, ParentActor() != nullptr, "残像に親アクターが無い");
            Root().CopyFrom(ParentActor()->Root());
            NS::SceneActor::PrepareRender();
        }

        void TackleGhost::OnEndPlay()
        {
            NS::SceneActor::OnEndPlay();
            m_draws.clear();
            if (m_drawRegistered)
            {
                NS::EndDraw(this, this);
                m_drawRegistered = false;
            }
        }

        void TackleGhost::OnInit()
        {
            if (ParentModel() == nullptr)
            {
                NS_LOG_ERROR(Game, "残像の親に描く Model が無いので、残像を描かない");
                return;
            }
            NS::InitDraw(this, this, &Root());
            m_drawRegistered = true;
        }

        void TackleGhost::OnAppear()
        {
            NS::SceneActor::OnAppear();
            if (m_drawRegistered)
            {
                NS::ShowModel(this, this);
            }
        }

        void TackleGhost::OnDisappear() noexcept
        {
            NS::SceneActor::OnDisappear();
            if (m_drawRegistered)
            {
                NS::HideModel(this, this);
            }
        }

        void TackleGhost::VisualStep()
        {
            const NS::SceneActor* owner = ParentActor();
            NS_ASSERT(Game, owner != nullptr, "残像に親アクターが無い");
            if (m_running)
            {
                ++m_frame;
                m_running = m_desc.WriteTo(this, m_frame, owner->ModelClassObj(), owner->Root().WorldMatrix());
            }
            m_draws.clear();
            if (m_spread.LengthSquared() > 0.0f)
            {
                for (const float side : {1.0f, -1.0f})
                {
                    m_draws.push_back(NS::ModelDraw{.offset = m_spread * side,
                                                    .opacity = m_sideOpacity,
                                                    .sceneValues = false});
                }
            }
            if (m_afterimageOpacity > 0.0f)
            {
                m_draws.push_back(NS::ModelDraw{.world = m_afterimageWorld,
                                                .opacity = m_afterimageOpacity,
                                                .sceneValues = false,
                                                .objectValues = false});
            }
        }

        const NS::Model* TackleGhost::ParentModel() const noexcept
        {
            // 親は CreateChildActor が OnInit より先に設定する
            NS_ASSERT(Game, ParentActor() != nullptr, "残像に親アクターが無い");
            return ParentActor()->ModelClassObj();
        }

    } // namespace Level
} // namespace GL
