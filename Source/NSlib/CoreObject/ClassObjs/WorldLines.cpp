#include "NSlib/CoreObject/ClassObjs/WorldLines.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Gfx/Render/RenderContext.h"

namespace NS
{
    bool TryProjectWorldLine(const WorldLine& line,
                             const Matrix& viewProjection,
                             Size2D targetSize,
                             PixelLine& outLine) noexcept
    {
        if (targetSize.width <= 0 || targetSize.height <= 0)
        {
            return false;
        }
        const float width = static_cast<float>(targetSize.width);
        const float height = static_cast<float>(targetSize.height);
        Vector2 start{};
        Vector2 end{};
        float startW = 0.0f;
        float endW = 0.0f;
        if (!Gfx::TryProjectToPixels(viewProjection, line.start, width, height, start, startW) ||
            !Gfx::TryProjectToPixels(viewProjection, line.end, width, height, end, endW))
        {
            return false;
        }
        const float thickness = line.thickness * height;
        if ((end - start).Length() < thickness)
        {
            return false;
        }
        outLine = PixelLine{.start = start, .end = end, .thickness = thickness, .color = line.color};
        return true;
    }

    WorldLine WorldLines::LineAt(std::size_t index, float alpha) const noexcept
    {
        const std::vector<WorldLine>& current = m_lines.Current();
        const std::vector<WorldLine>& previous = m_lines.Previous();
        NS_ASSERT(Scene, index < current.size(), "WorldLines::LineAt: 番号が線の数を超えている");
        WorldLine line = current[index];
        if (previous.size() != current.size() || alpha >= 1.0f)
        {
            return line;
        }
        line.start = InterpolateValue(previous[index].start, line.start, alpha);
        line.end = InterpolateValue(previous[index].end, line.end, alpha);
        return line;
    }
} // namespace NS
