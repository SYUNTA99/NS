#include "Editor/StageBuildTool.h"

#include "Editor/EditorActors.h"
#include "Editor/PlacementCatalog.h"
#include "NSlib/Core/Geometry/MeshPrimitives.h"
#include "NSlib/Gfx/Render/DebugDraw.h"
#include "NSlib/Phys/PhysicsScene.h"

#include <algorithm>
#include <cmath>
#include <limits>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS
{
    namespace Editor
    {
        namespace
        {
            [[nodiscard]] Quaternion PartRotation(float yawDegrees) noexcept
            {
                return Quaternion::CreateFromAxisAngle(Vector3::Up,
                                                       ToRadians(Degrees{std::remainder(yawDegrees, 360.0f)}).value);
            }

            [[nodiscard]] MeshGeometry PartGeometry(StageBuildTool::Part part, const Vector3& size, float yawDegrees)
            {
                MeshGeometry geometry = MakeCube(Vector3{0.5f, 0.5f, 0.5f});
                if (part == StageBuildTool::Part::Slope)
                {
                    geometry = MakeSlope(45.0f, Vector3{0.5f, 0.5f, 0.5f});
                }
                const Matrix transform =
                    Matrix::CreateScale(size) * Matrix::CreateFromQuaternion(PartRotation(yawDegrees));
                for (StaticVertex& vertex : geometry.vertices)
                {
                    vertex.position = Vector3::Transform(vertex.position, transform);
                }
                return geometry;
            }
        } // namespace

        void StageBuildTool::SelectPart(Part part) noexcept
        {
            if (part == m_part)
            {
                return;
            }
            m_part = part;
            switch (part)
            {
            case Part::Floor:
                m_size = {6.0f, 0.5f, 6.0f};
                break;
            case Part::Wall:
                m_size = {6.0f, 4.0f, 0.5f};
                break;
            case Part::Slope:
                m_size = {4.0f, 4.0f, 4.0f};
                break;
            }
        }

        std::optional<Vector3> StageBuildTool::Placement(const Ray& ray, const Phys::PhysicsScene& physics) const
        {
            if (!HasValidSettings() || !IsFinite(ray.position) || !IsFinite(ray.direction))
            {
                return std::nullopt;
            }
            Vector3 direction = ray.direction;
            const float length = direction.Length();
            if (!IsPositiveFinite(length))
            {
                return std::nullopt;
            }
            direction /= length;
            constexpr float k_MaxDistance = 5000.0f;
            float distance = 0.0f;
            Vector3 normal = Vector3::Up;
            if (physics.Raycast(ray.position, direction, k_MaxDistance, distance, normal, JPH::BodyID{}))
            {
                if (normal.Dot(direction) > 0.0f)
                {
                    normal = -normal;
                }
            }
            else
            {
                if (IsNearlyZero(direction.y))
                {
                    return std::nullopt;
                }
                distance = (m_emptySpaceHeight - ray.position.y) / direction.y;
                if (!(distance > 0.0f) || distance > k_MaxDistance)
                {
                    return std::nullopt;
                }
            }

            const MeshGeometry geometry = PartGeometry(m_part, m_size, m_yawDegrees);
            float support = std::numeric_limits<float>::max();
            for (const StaticVertex& vertex : geometry.vertices)
            {
                support = std::min(support, vertex.position.Dot(normal));
            }
            const Vector3 center = ray.position + direction * distance - normal * support;
            if (!IsFinite(center))
            {
                return std::nullopt;
            }
            return center;
        }

        nlohmann::json StageBuildTool::MakeActor(const Vector3& center) const
        {
            if (!HasValidSettings() || !IsFinite(center))
            {
                return nlohmann::json::object();
            }
            const char* mesh = "cube";
            if (m_part == Part::Slope)
            {
                mesh = "wedge45";
            }
            nlohmann::json actor = MakeMeshPartsPrototype(mesh);
            SetActorJsonPosition(actor, center);
            SetActorJsonScale(actor, m_size);
            SetActorJsonRotation(actor, PartRotation(m_yawDegrees));
            return actor;
        }

        void StageBuildTool::DrawPreview(Gfx::DebugShapes& shapes, const Vector3& center) const
        {
            if (!HasValidSettings() || !IsFinite(center))
            {
                return;
            }
            const MeshGeometry geometry = PartGeometry(m_part, m_size, m_yawDegrees);
            const Color edge{0.2f, 1.0f, 0.6f, 1.0f};
            const Color fill{0.2f, 0.8f, 0.5f, 0.12f};
            for (std::size_t i = 0; i + 2 < geometry.indices.size(); i += 3)
            {
                const Vector3 a = center + geometry.vertices[geometry.indices[i]].position;
                const Vector3 b = center + geometry.vertices[geometry.indices[i + 1]].position;
                const Vector3 c = center + geometry.vertices[geometry.indices[i + 2]].position;
                shapes.Triangle(a, b, c, fill);
                shapes.Line(a, b, edge);
                shapes.Line(b, c, edge);
                shapes.Line(c, a, edge);
            }
        }

        void StageBuildTool::RenderControls()
        {
#if NS_EDITOR_ENABLED
            ImGui::SeparatorText("置く部品");
            for (const Part part : {Part::Floor, Part::Wall, Part::Slope})
            {
                const char* label = "床";
                if (part == Part::Wall)
                {
                    label = "壁";
                }
                if (part == Part::Slope)
                {
                    label = "坂";
                }
                if (part != Part::Floor)
                {
                    ImGui::SameLine();
                }
                if (ImGui::RadioButton(label, m_part == part))
                {
                    SelectPart(part);
                }
            }
            ImGui::TextUnformatted("大きさ (m): 幅 / 高さ / 奥行き");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat3("##stageSize", &m_size.x, 0.1f, 0.01f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("向き (度)", &m_yawDegrees, 1.0f);
            ImGui::DragFloat("空白に置く高さ (m)", &m_emptySpaceHeight, 0.1f);
            if (!HasValidSettings())
            {
                ImGui::TextWrapped("正の大きさと、有限の向き・高さを指定してください。");
            }
            ImGui::TextWrapped("左クリックで連続配置。地形の面に接して置き、空白では指定した高さを使います。");
            ImGui::TextWrapped("R: 90度回転 / Esc: 選択・変形へ戻る");
#endif
        }

        bool StageBuildTool::HasValidSettings() const noexcept
        {
            return IsPositiveFinite(m_size.x) && IsPositiveFinite(m_size.y) && IsPositiveFinite(m_size.z) &&
                   std::isfinite(m_yawDegrees) && std::isfinite(m_emptySpaceHeight);
        }
    } // namespace Editor
} // namespace NS
