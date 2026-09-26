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
    } // namespace

    bool BuildTargetMarkerShape(const NS::Core::Matrix& viewProjection,
                                NS::Core::Size2D targetSize,
                                const SlamLineTarget& target,
                                float pathStart,
                                const TargetMarkerDesc& desc,
                                TargetMarkerShape& outShape)
    {
        if (!IsPositiveFinite(desc.lineThickness) || !IsPositiveFinite(desc.armRatio) ||
            !IsPositiveFinite(desc.dotSpacing) || !IsPositiveFinite(desc.dotSize))
        {
            return false;
        }
        if (targetSize.width <= 0 || targetSize.height <= 0)
        {
            return false;
        }
        if (!std::isfinite(target.along) || !std::isfinite(pathStart))
        {
            return false;
        }

        // 終わりの点を相手の中心の真横に置き、間隔ずつ手前へ置き始める距離まで数える
        std::size_t dotCount = 0;
        if (target.along >= pathStart)
        {
            const float steps = std::floor((target.along - pathStart) / desc.dotSpacing);
            if (!(steps < static_cast<float>(k_MaxPathDots)))
            {
                return false;
            }
            dotCount = static_cast<std::size_t>(steps) + 1;
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

        const float dotSize = desc.dotSize * pixelScale;
        shape.dots.reserve(dotCount);
        // 自機の側から並べる。i が 0 の点が自機に一番近い
        for (std::size_t i = 0; i < dotCount; ++i)
        {
            const std::size_t fromEnd = dotCount - 1 - i;
            const float along = target.along - desc.dotSpacing * static_cast<float>(fromEnd);
            const NS::Core::Vector3 point = target.origin + target.direction * along;
            NS::Core::Vector2 pixel{};
            if (!TryProjectToPixels(viewProjection, point, width, height, pixel))
            {
                continue;
            }
            shape.dots.push_back(MarkerRect{pixel.x - dotSize * 0.5f, pixel.y - dotSize * 0.5f, dotSize, dotSize});
        }

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
        // 溜め量は放した後も残るので、溜めているかで示すフレームを決める
        if (m_input == nullptr || !m_input->IsCharging())
        {
            return;
        }
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
        if (!m_hasShown)
        {
            return false;
        }
        return BuildTargetMarkerShape(viewProjection, targetSize, m_shown, m_pathStart, m_desc, outShape);
    }

    NS_CLASS(TargetMarker)
} // namespace NS::Game::Level
