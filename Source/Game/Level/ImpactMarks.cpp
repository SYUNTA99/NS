#include "Game/Level/ImpactMarks.h"

#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/Gfx/D3DMaterial.h"
#include "NSlib/Gfx/D3DMesh.h"
#include "NSlib/Gfx/Render/RenderContext.h"

#include <cmath>

namespace GL::Level
{
        ImpactMarks::ImpactMarks()
        {
            NS::LoadAssetField(m_mesh, "shadowQuad");
            NS::LoadAssetField(m_material, "Assets/Materials/shadow.mat");
        }

        void ImpactMarks::Spawn(const NS::Vector3& position)
        {
            if (m_capacity <= 0)
            {
                return;
            }
            const std::size_t capacity = static_cast<std::size_t>(m_capacity);
            // インスペクタで数を変えた後は、出ている跡を捨てて置き場を作り直す
            if (m_marks.size() != capacity)
            {
                m_marks.assign(capacity, Mark{});
                m_next = 0;
            }
            m_marks[m_next] = Mark{.position = position, .age = 0.0f, .live = true};
            m_newest = m_next;
            m_next = (m_next + 1) % capacity;
        }

        void ImpactMarks::Step(float dt) noexcept
        {
            for (Mark& mark : m_marks)
            {
                if (!mark.live)
                {
                    continue;
                }
                mark.age += dt;
                // 寿命が 0 以下か非数の跡は、出た次の歩で消す
                if (!NS::IsGreaterThan(m_lifeSeconds, mark.age))
                {
                    mark.live = false;
                }
            }
        }

        std::size_t ImpactMarks::LiveCount() const noexcept
        {
            std::size_t count = 0;
            for (const Mark& mark : m_marks)
            {
                if (mark.live)
                {
                    ++count;
                }
            }
            return count;
        }

        float ImpactMarks::SizeOf(const Mark& mark) const noexcept
        {
            float t = 1.0f;
            if (m_lifeSeconds > 0.0f)
            {
                t = NS::Clamp(mark.age / m_lifeSeconds, 0.0f, 1.0f);
            }
            return m_diameter * (1.0f - t);
        }

        std::vector<NS::Matrix> ImpactMarks::MarkWorldMatrices() const
        {
            std::vector<NS::Matrix> worlds;
            for (const Mark& mark : m_marks)
            {
                if (!mark.live)
                {
                    continue;
                }
                const float size = SizeOf(mark);
                if (!std::isfinite(size) || size <= 0.0f)
                {
                    continue;
                }
                worlds.push_back(NS::Matrix::CreateScale(size, 1.0f, size) *
                                 NS::Matrix::CreateTranslation(mark.position));
            }
            return worlds;
        }

        void ImpactMarks::Collect(const NS::Gfx::RenderContext& context,
                                  const NS::RenderSource& source,
                                  std::vector<NS::Gfx::DrawItem>& out)
        {
            // 跡は出した位置に描くので、持ち主の根は読まない
            (void)source;
            if (m_mesh->D3D() == nullptr || m_material->D3D() == nullptr)
            {
                return;
            }
            for (const NS::Matrix& world : MarkWorldMatrices())
            {
                NS::Gfx::DrawItem item{};
                item.mesh = m_mesh->D3D();
                item.material = m_material->D3D();
                item.blend = item.material->Blend();
                item.constants.world = world;
                item.constants.viewProj = context.viewProjection;
                // x は shadow.ps が読む不透明度
                item.constants.baseColor = NS::Vector3{NS::Clamp(m_opacity, 0.0f, 1.0f), 0.0f, 0.0f};
                out.push_back(item);
            }
        }

        NS::AABB ImpactMarks::WorldBounds(const NS::RenderSource& source) const noexcept
        {
            NS::AABB bounds{source.root.Translation(), NS::Vector3{}};
            bool first = true;
            for (const Mark& mark : m_marks)
            {
                if (!mark.live)
                {
                    continue;
                }
                const NS::AABB one{mark.position, NS::Vector3{m_diameter, m_diameter, m_diameter}};
                if (first)
                {
                    bounds = one;
                    first = false;
                    continue;
                }
                NS::AABB merged{};
                NS::AABB::CreateMerged(merged, bounds, one);
                bounds = merged;
            }
            return bounds;
        }

        NS::Vector3 ImpactMarks::SortCenter(const NS::RenderSource& source) const noexcept
        {
            if (m_newest < m_marks.size() && m_marks[m_newest].live)
            {
                return m_marks[m_newest].position;
            }
            return source.root.Translation();
        }
} // namespace GL::Level
