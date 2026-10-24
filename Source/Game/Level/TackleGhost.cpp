#include "Game/Level/TackleGhost.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/Gfx/Render/RenderContext.h"

#include <algorithm>
#include <cmath>

namespace GL
{
    namespace Level
    {
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
            const NS::Model* model = source.model;
            NS_ASSERT(Game, model != nullptr, "残像は InitDraw で元の Model と一緒に入れていない");
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
                    NS::Gfx::DrawItem item = model->MakeDrawItem(context, SideWorldMatrix(model, source.root, context.alpha, side));
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

        NS::AABB TackleGhost::WorldBounds(const NS::RenderSource& source) const noexcept
        {
            const NS::Model* model = source.model;
            if (model == nullptr)
            {
                return {};
            }
            // 描く形の箱を、描く時だけのずれと離れの分だけ広げる。どちらの写しも外さない
            NS::AABB out = model->WorldBounds(source);
            const NS::Vector3& offset = model->GetDrawOffset();
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

        NS::Vector3 TackleGhost::SortCenter(const NS::RenderSource& source) const noexcept
        {
            const NS::Model* model = source.model;
            if (model == nullptr)
            {
                return {};
            }
            return model->SortCenter(source);
        }
    } // namespace Level
} // namespace GL
