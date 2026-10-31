#include "Game/Level/ReboundPreview.h"

#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/Render/RenderContext.h"
#include "NSlib/Gfx/Render/ScreenOverlay.h"

#include <cmath>

namespace GL
{
    namespace Level
    {
        namespace
        {
            [[nodiscard]] bool IsValidDesc(const ReboundPreviewDesc& desc) noexcept
            {
                return NS::IsPositiveFinite(desc.referenceHeight) && NS::IsPositiveFinite(desc.assignedLength) &&
                       NS::IsPositiveFinite(desc.choiceLength) && NS::IsPositiveFinite(desc.assignedThickness) &&
                       NS::IsPositiveFinite(desc.choiceThickness) && NS::IsNonNegativeFinite(desc.headLengthRatio) &&
                       std::isfinite(desc.headAngleDegrees);
            }

            // 1 本の矢印の形。長さは世界の m、太さは画面のピクセル
            struct ArrowLook
            {
                float length;
                float thickness;
                NS::Color color;
            };

            // 当たる点から direction へ伸びる矢印の、軸と矢じりの 2 本を足す。カメラの後ろに出る矢印と、
            // 画面に映った長さが太さより短い矢印 (カメラへまっすぐ向く物) は足さない
            void AppendArrow(const NS::Vector3& origin,
                             const NS::Vector3& direction,
                             const ArrowLook& look,
                             const NS::Matrix& viewProjection,
                             NS::Size2D targetSize,
                             const ReboundPreviewDesc& desc,
                             std::vector<ReboundArrowLine>& outLines)
            {
                const float width = static_cast<float>(targetSize.width);
                const float height = static_cast<float>(targetSize.height);
                NS::Vector2 start{};
                NS::Vector2 tip{};
                float startW = 0.0f;
                float tipW = 0.0f;
                if (!NS::Gfx::TryProjectToPixels(viewProjection, origin, width, height, start, startW) ||
                    !NS::Gfx::TryProjectToPixels(
                        viewProjection, origin + direction * look.length, width, height, tip, tipW))
                {
                    return;
                }
                const NS::Vector2 shaft = tip - start;
                const float shaftLength = shaft.Length();
                if (shaftLength < look.thickness)
                {
                    return;
                }
                outLines.push_back(ReboundArrowLine{.startX = start.x,
                                                    .startY = start.y,
                                                    .endX = tip.x,
                                                    .endY = tip.y,
                                                    .thickness = look.thickness,
                                                    .color = look.color});
                // 矢じりは先から軸を戻る向きを、左右へ開いた 2 本
                const NS::Vector2 back = shaft * (-desc.headLengthRatio);
                const float open = NS::DegreesToRadians(desc.headAngleDegrees);
                for (const float angle : {open, -open})
                {
                    const float c = std::cos(angle);
                    const float s = std::sin(angle);
                    const NS::Vector2 head{back.x * c - back.y * s, back.x * s + back.y * c};
                    outLines.push_back(ReboundArrowLine{.startX = tip.x,
                                                        .startY = tip.y,
                                                        .endX = tip.x + head.x,
                                                        .endY = tip.y + head.y,
                                                        .thickness = look.thickness,
                                                        .color = look.color});
                }
            }
        } // namespace

        bool BuildReboundArrowLines(const ReboundPreviewArrows& arrows,
                                    const NS::Matrix& viewProjection,
                                    NS::Size2D targetSize,
                                    const ReboundPreviewDesc& desc,
                                    std::vector<ReboundArrowLine>& outLines)
        {
            if (!IsValidDesc(desc) || targetSize.width <= 0 || targetSize.height <= 0)
            {
                return false;
            }
            const float pixelScale = static_cast<float>(targetSize.height) / desc.referenceHeight;
            const ArrowLook choice{.length = desc.choiceLength,
                                   .thickness = desc.choiceThickness * pixelScale,
                                   .color = desc.choiceColor};
            const ArrowLook assigned{.length = desc.assignedLength,
                                     .thickness = desc.assignedThickness * pixelScale,
                                     .color = desc.assignedColor};
            outLines.clear();
            for (const NS::Vector3& direction : arrows.choices)
            {
                AppendArrow(arrows.origin, direction, choice, viewProjection, targetSize, desc, outLines);
            }
            AppendArrow(arrows.origin, arrows.assigned, assigned, viewProjection, targetSize, desc, outLines);
            return true;
        }

        void ReboundPreview::Show(const ReboundPreviewArrows& arrows)
        {
            m_shown = arrows;
            m_hasShown = true;
        }

        void ReboundPreview::Hide() noexcept
        {
            m_hasShown = false;
        }

        const ReboundPreviewArrows* ReboundPreview::ShownArrows() const noexcept
        {
            if (!m_hasShown)
            {
                return nullptr;
            }
            return &m_shown;
        }

        void ReboundPreview::OnRenderOverlay(const NS::Gfx::RenderContext& context)
        {
            if (!m_hasShown || context.device == nullptr || context.overlay == nullptr)
            {
                return;
            }
            if (!BuildReboundArrowLines(m_shown, context.viewProjection, context.device->Size(), m_desc, m_lineScratch))
            {
                return;
            }
            for (const ReboundArrowLine& line : m_lineScratch)
            {
                context.overlay->DrawScreenLine(line.startX, line.startY, line.endX, line.endY, line.thickness, line.color);
            }
        }
    } // namespace Level
} // namespace GL
