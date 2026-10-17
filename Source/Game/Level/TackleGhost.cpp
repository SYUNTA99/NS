#include "Game/Level/TackleGhost.h"

#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Gfx/Render/RenderContext.h"

#include <algorithm>
#include <cmath>

namespace GL
{
    namespace Level
    {
        void TackleGhost::Register()
        {
            if (m_owner->OwningScene() == nullptr)
            {
                return;
            }
            m_owner->OwningScene()->RegisterRenderable(this);
        }

        void TackleGhost::Unregister()
        {
            if (m_owner->OwningScene() == nullptr)
            {
                return;
            }
            m_owner->OwningScene()->UnregisterRenderable(this);
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

        NS::Matrix TackleGhost::SideWorldMatrix(float alpha, float side) const noexcept
        {
            const NS::Model* model = m_owner->ModelClassObj();
            if (model == nullptr)
            {
                return NS::Matrix::Identity;
            }
            return model->DrawWorldMatrixWithOffset(alpha, m_spread * side);
        }

        void TackleGhost::CaptureAfterimage() noexcept
        {
            const NS::Model* model = m_owner->ModelClassObj();
            if (model == nullptr)
            {
                return;
            }
            m_afterimageWorld = model->DrawWorldMatrix(1.0f);
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

        void TackleGhost::Collect(const NS::Gfx::RenderContext& context, std::vector<NS::Gfx::DrawItem>& out)
        {
            const NS::Model* model = m_owner->ModelClassObj();
            if (model == nullptr || !model->CanDraw())
            {
                return;
            }
            // 本体のマテリアルを混ぜて重ねる。本体のピクセルシェーダーは描く 1 回ごとの不透明度を透明度に出す
            // 床の波の光は写しに乗せない。写しは本体の姿だけを薄く残す
            const NS::Vector3& spread = m_spread;
            if (spread.x != 0.0f || spread.y != 0.0f || spread.z != 0.0f)
            {
                for (const float side : {1.0f, -1.0f})
                {
                    NS::Gfx::DrawItem item = model->MakeDrawItem(context, SideWorldMatrix(context.alpha, side));
                    item.blend = NS::Gfx::BlendMode::Alpha;
                    item.constants.groundWaveStrength = 0.0f;
                    item.constants.opacity = m_sideOpacity;
                    out.push_back(item);
                }
            }
            if (m_afterimageOpacity > 0.0f)
            {
                // 控えた姿のまま残す。震えを足すと止まった姿に見えない
                NS::Gfx::DrawItem item = model->MakeDrawItem(context, m_afterimageWorld);
                item.blend = NS::Gfx::BlendMode::Alpha;
                item.constants.groundWaveStrength = 0.0f;
                item.constants.tremor = NS::Gfx::TremorCB{};
                item.constants.opacity = m_afterimageOpacity;
                out.push_back(item);
            }
        }

        NS::AABB TackleGhost::WorldBounds() const noexcept
        {
            const NS::Model* model = m_owner->ModelClassObj();
            if (model == nullptr)
            {
                return {};
            }
            // 描く形の箱を、描く時だけのずれと離れの分だけ広げる。どちらの写しも外さない
            NS::AABB out = model->WorldBounds();
            const NS::Vector3 offset = model->DrawAdjustment().offset;
            out.Extents.x += std::abs(m_spread.x) + std::abs(offset.x);
            out.Extents.y += std::abs(m_spread.y) + std::abs(offset.y);
            out.Extents.z += std::abs(m_spread.z) + std::abs(offset.z);
            const NS::AABB* local = model->DrawnLocalBounds();
            if (m_afterimageOpacity > 0.0f && local != nullptr)
            {
                // 根が飛び去っても写しは当たった所に残る。根の箱だけで間引くと、写しが画面の中でも消える
                NS::AABB afterimage{};
                local->Transform(afterimage, m_afterimageWorld);
                NS::AABB merged{};
                NS::AABB::CreateMerged(merged, out, afterimage);
                return merged;
            }
            return out;
        }

        NS::Vector3 TackleGhost::SortCenter() const noexcept
        {
            const NS::Model* model = m_owner->ModelClassObj();
            if (model == nullptr)
            {
                return {};
            }
            return model->SortCenter();
        }
    } // namespace Level
} // namespace GL
