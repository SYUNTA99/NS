#include "Game/Level/ReboundPreview.h"

#include "NSlib/Core/Assert.h"

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

            // 1 本の矢印の形。長さは世界の m、太さは描画先の高さに対する割合
            struct ArrowLook
            {
                float length;
                float thickness;
                NS::Color color;
            };

            // 矢じりを開く向き。矢印の向きと世界の上向きに垂直な向きで、矢印が上か下を向く時は世界の X 軸
            [[nodiscard]] NS::Vector3 HeadSide(const NS::Vector3& direction) noexcept
            {
                const NS::Vector3 side = NS::Vector3::Up.Cross(direction);
                const float length = side.Length();
                if (!NS::HasPositiveMagnitude(length))
                {
                    return NS::Vector3{1.0f, 0.0f, 0.0f};
                }
                return side / length;
            }

            // 当たる点から direction へ伸びる矢印の、軸と矢じりの 2 本を足す
            void AppendArrow(const NS::Vector3& origin,
                             const NS::Vector3& direction,
                             const ArrowLook& look,
                             const ReboundPreviewDesc& desc,
                             std::vector<NS::WorldLine>& outLines)
            {
                const NS::Vector3 tip = origin + direction * look.length;
                outLines.push_back(
                    NS::WorldLine{.start = origin, .end = tip, .thickness = look.thickness, .color = look.color});
                // 矢じりは先から軸を戻る向きを、左右へ開いた 2 本
                const float headLength = look.length * desc.headLengthRatio;
                const float open = NS::DegreesToRadians(desc.headAngleDegrees);
                const NS::Vector3 side = HeadSide(direction);
                for (const float angle : {open, -open})
                {
                    const NS::Vector3 head = (side * std::sin(angle) - direction * std::cos(angle)) * headLength;
                    outLines.push_back(NS::WorldLine{
                        .start = tip, .end = tip + head, .thickness = look.thickness, .color = look.color});
                }
            }
        } // namespace

        bool BuildReboundArrowLines(const ReboundPreviewArrows& arrows,
                                    const ReboundPreviewDesc& desc,
                                    std::vector<NS::WorldLine>& outLines)
        {
            if (!IsValidDesc(desc))
            {
                return false;
            }
            const ArrowLook choice{.length = desc.choiceLength,
                                   .thickness = desc.choiceThickness / desc.referenceHeight,
                                   .color = desc.choiceColor};
            const ArrowLook assigned{.length = desc.assignedLength,
                                     .thickness = desc.assignedThickness / desc.referenceHeight,
                                     .color = desc.assignedColor};
            outLines.clear();
            for (const NS::Vector3& direction : arrows.choices)
            {
                AppendArrow(arrows.origin, direction, choice, desc, outLines);
            }
            AppendArrow(arrows.origin, arrows.assigned, assigned, desc, outLines);
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

        void ReboundPreview::WriteLines(NS::WorldLines* lines)
        {
            NS_ASSERT(Game, lines != nullptr, "ReboundPreview::WriteLines: 線の部品が null");
            if (!m_hasShown || !BuildReboundArrowLines(m_shown, m_desc, m_lineScratch))
            {
                m_lineScratch.clear();
            }
            lines->SetLines(m_lineScratch);
        }
    } // namespace Level
} // namespace GL
