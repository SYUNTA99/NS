#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ActorJson.h"

#include <optional>

namespace NS
{
    namespace Gfx
    {
        class DebugShapes;
    }
    namespace Phys
    {
        class PhysicsScene;
    }
    namespace Editor
    {
        class StageBuildTool
        {
        public:
            enum class Part
            {
                Floor,
                Wall,
                Slope
            };

            void SelectPart(Part part) noexcept;
            [[nodiscard]] Part SelectedPart() const noexcept { return m_part; }
            [[nodiscard]] Vector3& Size() noexcept { return m_size; }
            [[nodiscard]] float& YawDegrees() noexcept { return m_yawDegrees; }
            [[nodiscard]] float& EmptySpaceHeight() noexcept { return m_emptySpaceHeight; }

            // 実形状へ接する中心座標。未命中では指定高さの水平面へ置く。
            // 不正な寸法・非有限値・置ける面のない光線では空を返す。
            [[nodiscard]] std::optional<Vector3> Placement(const Ray& ray, const Phys::PhysicsScene& physics) const;
            // 寸法と向きを含む配置用の文書。不正な設定や中心座標では空の文書を返す。
            [[nodiscard]] nlohmann::json MakeActor(const Vector3& center) const;
            void DrawPreview(Gfx::DebugShapes& shapes, const Vector3& center) const;
            void RenderControls();

        private:
            [[nodiscard]] bool HasValidSettings() const noexcept;
            Part m_part = Part::Floor;
            Vector3 m_size{6.0f, 0.5f, 6.0f};
            float m_yawDegrees = 0.0f;
            float m_emptySpaceHeight = 0.0f;
        };
    } // namespace Editor
} // namespace NS
