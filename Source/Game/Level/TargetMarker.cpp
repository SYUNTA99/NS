#include "Game/Level/TargetMarker.h"

#include "Game/Level/CollisionInput.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Graphics/RenderContext.h"
#include "Runtime/Graphics/Renderer.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace NS::Game::Level
{
    namespace
    {
        // 画素の欄は描画先の高さがこの値のときの大きさで書く
        constexpr float k_ReferenceHeight = 720.0f;
        // 印と道筋の点の不透明度。後ろの相手の輪郭をわずかに透かす
        constexpr float k_MarkerAlpha = 0.9f;
        // 組む点の数の上限。極端な間隔の欄で描く四角の数が膨らむのを止める
        constexpr std::size_t k_MaxPathDots = 1024;

        // 世界の点を描画先の画素 (左上が原点) へ投げる。カメラの後ろ (投げた w が 0 以下) なら false
        [[nodiscard]] bool TryProjectToPixels(const NS::Core::Matrix& viewProjection,
                                              const NS::Core::Vector3& point,
                                              float width,
                                              float height,
                                              NS::Core::Vector2& outPixel) noexcept
        {
            const NS::Core::Vector4 clip =
                NS::Core::Vector4::Transform(NS::Core::Vector4{point.x, point.y, point.z, 1.0f}, viewProjection);
            if (!(clip.w > 0.0f))
            {
                return false;
            }
            const float ndcX = clip.x / clip.w;
            const float ndcY = clip.y / clip.w;
            outPixel = NS::Core::Vector2{(ndcX + 1.0f) * 0.5f * width, (1.0f - ndcY) * 0.5f * height};
            return true;
        }

        [[nodiscard]] bool IsPositiveFinite(float value) noexcept
        {
            return std::isfinite(value) && value > 0.0f;
        }

        // 外接箱の 8 つの角を投げた矩形の四隅に、横と縦の 2 本ずつのかぎ形を足す。角がカメラの後ろなら何も足さない
        void AppendCornerHooks(const NS::Core::Matrix& viewProjection,
                               float width,
                               float height,
                               const NS::Core::AABB& bounds,
                               float thickness,
                               float armRatio,
                               std::vector<MarkerRect>& outCorners)
        {
            float left = 0.0f;
            float top = 0.0f;
            float right = 0.0f;
            float bottom = 0.0f;
            for (int corner = 0; corner < 8; ++corner)
            {
                // 角の番号の 3 つのビットを、x・y・z の負と正の側に割り当てる
                float signX = -1.0f;
                if ((corner & 1) != 0)
                {
                    signX = 1.0f;
                }
                float signY = -1.0f;
                if ((corner & 2) != 0)
                {
                    signY = 1.0f;
                }
                float signZ = -1.0f;
                if ((corner & 4) != 0)
                {
                    signZ = 1.0f;
                }
                const NS::Core::Vector3 point{bounds.Center.x + signX * bounds.Extents.x,
                                              bounds.Center.y + signY * bounds.Extents.y,
                                              bounds.Center.z + signZ * bounds.Extents.z};
                NS::Core::Vector2 pixel{};
                if (!TryProjectToPixels(viewProjection, point, width, height, pixel))
                {
                    return;
                }
                if (corner == 0)
                {
                    left = pixel.x;
                    right = pixel.x;
                    top = pixel.y;
                    bottom = pixel.y;
                    continue;
                }
                left = std::min(left, pixel.x);
                right = std::max(right, pixel.x);
                top = std::min(top, pixel.y);
                bottom = std::max(bottom, pixel.y);
            }

            const float arm = armRatio * std::min(right - left, bottom - top);
            outCorners.push_back(MarkerRect{left, top, arm, thickness});
            outCorners.push_back(MarkerRect{left, top, thickness, arm});
            outCorners.push_back(MarkerRect{right - arm, top, arm, thickness});
            outCorners.push_back(MarkerRect{right - thickness, top, thickness, arm});
            outCorners.push_back(MarkerRect{left, bottom - thickness, arm, thickness});
            outCorners.push_back(MarkerRect{left, bottom - arm, thickness, arm});
            outCorners.push_back(MarkerRect{right - arm, bottom - thickness, arm, thickness});
            outCorners.push_back(MarkerRect{right - thickness, bottom - arm, thickness, arm});
        }

        // 欄の値と描画先の大きさが組める値か。印の欄は点だけを組む時も同じく見る
        [[nodiscard]] bool CanBuild(NS::Core::Size2D targetSize, const TargetMarkerDesc& desc) noexcept
        {
            if (!IsPositiveFinite(desc.lineThickness) || !IsPositiveFinite(desc.armRatio) ||
                !IsPositiveFinite(desc.dotSpacing) || !IsPositiveFinite(desc.dotSize))
            {
                return false;
            }
            return targetSize.width > 0 && targetSize.height > 0;
        }

        // 終わりの点を線に沿った距離 pathEnd に置き、間隔ずつ手前へ置き始める距離まで数える。
        // 数が上限を超える場合は false
        [[nodiscard]] bool TryCountPathDots(float pathEnd,
                                            float pathStart,
                                            float spacing,
                                            std::size_t& outCount) noexcept
        {
            outCount = 0;
            if (pathEnd >= pathStart)
            {
                const float steps = std::floor((pathEnd - pathStart) / spacing);
                if (!(steps < static_cast<float>(k_MaxPathDots)))
                {
                    return false;
                }
                outCount = static_cast<std::size_t>(steps) + 1;
            }
            return true;
        }

        // 線の上に数えた点を、自機の側から並べて足す。i が 0 の点が自機に一番近い。カメラの後ろの点は足さない
        void AppendPathDots(const NS::Core::Matrix& viewProjection,
                            float width,
                            float height,
                            const NS::Core::Vector3& origin,
                            const NS::Core::Vector3& direction,
                            float pathEnd,
                            std::size_t dotCount,
                            float spacing,
                            float dotSize,
                            std::vector<MarkerRect>& outDots)
        {
            outDots.reserve(dotCount);
            for (std::size_t i = 0; i < dotCount; ++i)
            {
                const std::size_t fromEnd = dotCount - 1 - i;
                const float along = pathEnd - spacing * static_cast<float>(fromEnd);
                const NS::Core::Vector3 point = origin + direction * along;
                NS::Core::Vector2 pixel{};
                if (!TryProjectToPixels(viewProjection, point, width, height, pixel))
                {
                    continue;
                }
                outDots.push_back(MarkerRect{pixel.x - dotSize * 0.5f, pixel.y - dotSize * 0.5f, dotSize, dotSize});
            }
        }
    } // namespace

    bool BuildTargetMarkerShape(const NS::Core::Matrix& viewProjection,
                                NS::Core::Size2D targetSize,
                                const SlamLineTarget& target,
                                float pathStart,
                                const TargetMarkerDesc& desc,
                                TargetMarkerShape& outShape)
    {
        if (!CanBuild(targetSize, desc))
        {
            return false;
        }
        if (!std::isfinite(target.along) || !std::isfinite(pathStart))
        {
            return false;
        }

        // 終わりの点を相手の中心の真横に置く
        std::size_t dotCount = 0;
        if (!TryCountPathDots(target.along, pathStart, desc.dotSpacing, dotCount))
        {
            return false;
        }

        const float width = static_cast<float>(targetSize.width);
        const float height = static_cast<float>(targetSize.height);
        const float pixelScale = height / k_ReferenceHeight;

        TargetMarkerShape shape{};
        AppendCornerHooks(viewProjection,
                          width,
                          height,
                          target.bounds,
                          desc.lineThickness * pixelScale,
                          desc.armRatio,
                          shape.corners);

        AppendPathDots(viewProjection,
                       width,
                       height,
                       target.origin,
                       target.direction,
                       target.along,
                       dotCount,
                       desc.dotSpacing,
                       desc.dotSize * pixelScale,
                       shape.dots);

        outShape = std::move(shape);
        return true;
    }

    bool BuildAimPathShape(const NS::Core::Matrix& viewProjection,
                           NS::Core::Size2D targetSize,
                           const AimLine& line,
                           float pathStart,
                           const TargetMarkerDesc& desc,
                           TargetMarkerShape& outShape)
    {
        if (!CanBuild(targetSize, desc))
        {
            return false;
        }
        if (!std::isfinite(line.length) || !std::isfinite(pathStart))
        {
            return false;
        }

        // 終わりの点を突進が止まる所に置く
        std::size_t dotCount = 0;
        if (!TryCountPathDots(line.length, pathStart, desc.dotSpacing, dotCount))
        {
            return false;
        }

        const float width = static_cast<float>(targetSize.width);
        const float height = static_cast<float>(targetSize.height);
        const float pixelScale = height / k_ReferenceHeight;

        TargetMarkerShape shape{};
        AppendPathDots(viewProjection,
                       width,
                       height,
                       line.origin,
                       line.direction,
                       line.length,
                       dotCount,
                       desc.dotSpacing,
                       desc.dotSize * pixelScale,
                       shape.dots);

        outShape = std::move(shape);
        return true;
    }

    // -130 は CollisionInput (-140) がこのフレームの狙う相手を控えた後に読むため
    TargetMarker::TargetMarker() noexcept : NS::Obj::OverlayRenderer(NS::Obj::TickPriority::Update - 130) {}

    void TargetMarker::OnStart()
    {
        // 基底が重ね描きの登録簿へ自分を入れる
        NS::Obj::OverlayRenderer::OnStart();

        m_input = Owner()->FindComponent<CollisionInput>();
        m_movement = Owner()->FindComponent<NS::Game::Player::PlayerComponent>();
    }

    void TargetMarker::OnUpdate()
    {
        m_hasShown = false;
        m_hasLine = false;
        // 溜め量は放した後も残るので、溜めているかで示すフレームを決める
        if (m_input == nullptr || !m_input->IsCharging())
        {
            return;
        }
        m_hasLine = m_input->TryGetAimLine(m_line);
        m_hasShown = m_input->TryGetAimTarget(m_shown);
        m_pathStart = 0.0f;
        if (m_movement != nullptr)
        {
            m_pathStart = m_movement->CapsuleRadius();
        }
    }

    void TargetMarker::OnRenderOverlay(const NS::Gfx::RenderContext& context)
    {
        if (context.renderer == nullptr)
        {
            return;
        }
        TargetMarkerShape shape{};
        if (!BuildShownShape(context.viewProjection, context.renderer->Size(), shape))
        {
            return;
        }
        const NS::Core::Color color{m_color.x, m_color.y, m_color.z, k_MarkerAlpha};
        for (const MarkerRect& rect : shape.corners)
        {
            context.renderer->DrawScreenRect(rect.x, rect.y, rect.width, rect.height, color);
        }
        for (const MarkerRect& rect : shape.dots)
        {
            context.renderer->DrawScreenRect(rect.x, rect.y, rect.width, rect.height, color);
        }
    }

    NS::Obj::ObjectRef TargetMarker::ShownTargetRef() const noexcept
    {
        if (!m_hasShown)
        {
            return NS::Obj::ObjectRef{};
        }
        return m_shown.target;
    }

    bool TargetMarker::BuildShownShape(const NS::Core::Matrix& viewProjection,
                                       NS::Core::Size2D targetSize,
                                       TargetMarkerShape& outShape) const
    {
        if (m_hasShown)
        {
            return BuildTargetMarkerShape(viewProjection, targetSize, m_shown, m_pathStart, m_desc, outShape);
        }
        if (m_hasLine)
        {
            return BuildAimPathShape(viewProjection, targetSize, m_line, m_pathStart, m_desc, outShape);
        }
        return false;
    }

    NS_CLASS(TargetMarker)
} // namespace NS::Game::Level
