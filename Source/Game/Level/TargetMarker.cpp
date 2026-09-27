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
#include <limits>
#include <utility>

namespace NS::Game::Level
{
    namespace
    {
        // 画素の欄は描画先の高さがこの値のときの大きさで書く
        constexpr float k_ReferenceHeight = 720.0f;
        // 道筋の点の不透明度。後ろの相手の輪郭をわずかに透かす
        constexpr float k_DotAlpha = 0.9f;
        // 暗い縁が明るい線からはみ出す片側の幅。描画先の高さ 720 のときの画素。明るい線を読ませる最小の幅
        constexpr float k_OutlineWidth = 1.0f;
        // 組む点の数の上限。極端な間隔の欄で描く四角の数が膨らむのを止める
        constexpr std::size_t k_MaxPathDots = 1024;

        // 世界の点を描画先の画素 (左上が原点) へ投げる。カメラの後ろ (投げた w が 0 以下) なら false
        [[nodiscard]] bool TryProjectToPixels(const NS::Core::Matrix& viewProjection,
                                              const NS::Core::Vector3& point,
                                              float width,
                                              float height,
                                              NS::Core::Vector2& outPixel,
                                              float& outW) noexcept
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
            outW = clip.w;
            return true;
        }

        [[nodiscard]] bool IsPositiveFinite(float value) noexcept
        {
            return std::isfinite(value) && value > 0.0f;
        }

        [[nodiscard]] bool IsNonNegativeFinite(float value) noexcept
        {
            return std::isfinite(value) && value >= 0.0f;
        }

        [[nodiscard]] bool IsFiniteColor(const NS::Core::Vector3& color) noexcept
        {
            return std::isfinite(color.x) && std::isfinite(color.y) && std::isfinite(color.z);
        }

        // 左上 (left, top)・一辺 side の正方形の四隅に、横と縦の 2 本ずつのかぎ形を足す
        void AppendCornerHooks(
            float left, float top, float side, float thickness, float armRatio, std::vector<MarkerRect>& outCorners)
        {
            const float right = left + side;
            const float bottom = top + side;
            const float arm = armRatio * side;
            outCorners.push_back(MarkerRect{left, top, arm, thickness});
            outCorners.push_back(MarkerRect{left, top, thickness, arm});
            outCorners.push_back(MarkerRect{right - arm, top, arm, thickness});
            outCorners.push_back(MarkerRect{right - thickness, top, thickness, arm});
            outCorners.push_back(MarkerRect{left, bottom - thickness, arm, thickness});
            outCorners.push_back(MarkerRect{left, bottom - arm, thickness, arm});
            outCorners.push_back(MarkerRect{right - arm, bottom - thickness, arm, thickness});
            outCorners.push_back(MarkerRect{right - thickness, bottom - arm, thickness, arm});
        }

        // 枠の欄が組める値か
        [[nodiscard]] bool IsValidFrameDesc(const TargetMarkerDesc& desc) noexcept
        {
            if (!IsFiniteColor(desc.color) || !IsFiniteColor(desc.appearColor) || !IsFiniteColor(desc.outlineColor))
            {
                return false;
            }
            if (!IsPositiveFinite(desc.frameMinSide) || !IsPositiveFinite(desc.frameAlpha) ||
                !IsPositiveFinite(desc.appearScale) || !IsPositiveFinite(desc.appearMaxSide) ||
                !IsPositiveFinite(desc.lostScale))
            {
                return false;
            }
            if (!IsNonNegativeFinite(desc.frameGap) || !IsNonNegativeFinite(desc.appearAlpha) ||
                !IsNonNegativeFinite(desc.outlineAlpha))
            {
                return false;
            }
            // 点の数が上限を超えると Evaluate が配列の外を読む
            return desc.appearFrames >= 0 && desc.lostFrames >= 0 &&
                   desc.appearCurve.count <= NS::Obj::Curve::k_MaxKeys;
        }

        // 欄の値と描画先の大きさが組める値か。枠の欄は点だけを組む時も同じく見る
        [[nodiscard]] bool CanBuild(NS::Core::Size2D targetSize, const TargetMarkerDesc& desc) noexcept
        {
            if (!IsPositiveFinite(desc.lineThickness) || !IsPositiveFinite(desc.armRatio) ||
                !IsPositiveFinite(desc.dotSpacing) || !IsPositiveFinite(desc.dotSize))
            {
                return false;
            }
            if (!IsValidFrameDesc(desc))
            {
                return false;
            }
            return targetSize.width > 0 && targetSize.height > 0;
        }

        // 捉えた瞬間の形を 0、捉えている間の形を 1 とした縮みの進み
        [[nodiscard]] float AppearProgress(int framesSinceCapture, const TargetMarkerDesc& desc) noexcept
        {
            // 縮むフレーム数 0 は捉えたフレームから捉えている間の形で出す
            if (desc.appearFrames <= 0)
            {
                return 1.0f;
            }
            const float elapsed = static_cast<float>(std::max(framesSinceCapture, 0));
            const float time = NS::Core::Clamp(elapsed / static_cast<float>(desc.appearFrames), 0.0f, 1.0f);
            // Inspector で点を全部消すと Evaluate が 0 を返し、枠が縮み切らない。点が無ければ時間どおりに進める
            if (desc.appearCurve.count == 0)
            {
                return time;
            }
            const float progress = desc.appearCurve.Evaluate(time);
            // 点に非数があると非数が返る。Clamp は非数を止めない
            if (!std::isfinite(progress))
            {
                return time;
            }
            return NS::Core::Clamp(progress, 0.0f, 1.0f);
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
                float w = 0.0f;
                if (!TryProjectToPixels(viewProjection, point, width, height, pixel, w))
                {
                    continue;
                }
                outDots.push_back(MarkerRect{pixel.x - dotSize * 0.5f, pixel.y - dotSize * 0.5f, dotSize, dotSize});
            }
        }
    } // namespace

    bool BuildLockOnFrame(const NS::Core::Matrix& viewProjection,
                          NS::Core::Size2D targetSize,
                          const NS::Core::AABB& bounds,
                          LockOnFrames frames,
                          const TargetMarkerDesc& desc,
                          LockOnFrameShape& outFrame)
    {
        if (!CanBuild(targetSize, desc))
        {
            return false;
        }

        LockOnFrameShape frame{};
        const bool lost = frames.sinceLost >= 0;
        if (lost && frames.sinceLost >= desc.lostFrames)
        {
            outFrame = std::move(frame);
            return true;
        }

        const float width = static_cast<float>(targetSize.width);
        const float height = static_cast<float>(targetSize.height);
        const float pixelScale = height / k_ReferenceHeight;
        const NS::Core::Vector3 center{bounds.Center.x, bounds.Center.y, bounds.Center.z};
        NS::Core::Vector2 centerPixel{};
        float centerW = 0.0f;
        if (!TryProjectToPixels(viewProjection, center, width, height, centerPixel, centerW))
        {
            outFrame = std::move(frame);
            return true;
        }

        // 行列の縦の成分のうち世界の x・y・z に掛かる 3 つの長さは、ビューの回転で変わらず射影の縦の倍率になる
        // 中心と同じ奥行きの面の上の半径なので、カメラの向きで大きさが変わらない
        const float verticalScale =
            std::sqrt(viewProjection._12 * viewProjection._12 + viewProjection._22 * viewProjection._22 +
                      viewProjection._32 * viewProjection._32);
        const float radius = std::max({bounds.Extents.x, bounds.Extents.y, bounds.Extents.z});
        const float radiusPixels = radius * verticalScale / centerW * height * 0.5f;
        const float settledSide =
            std::max(2.0f * (radiusPixels + desc.frameGap * pixelScale), desc.frameMinSide * pixelScale);
        // 近い大きな相手で一辺が上限を超える時は、縮まずに出る
        const float appearSide =
            std::max(std::min(settledSide * desc.appearScale, desc.appearMaxSide * pixelScale), settledSide);

        const float progress = AppearProgress(frames.sinceCapture, desc);
        float side = NS::Core::Lerp(appearSide, settledSide, progress);
        if (lost)
        {
            side *= desc.lostScale;
        }
        const float alpha = NS::Core::Lerp(desc.appearAlpha, desc.frameAlpha, progress);

        AppendCornerHooks(centerPixel.x - side * 0.5f,
                          centerPixel.y - side * 0.5f,
                          side,
                          desc.lineThickness * pixelScale,
                          desc.armRatio,
                          frame.corners);
        const float outlineWidth = k_OutlineWidth * pixelScale;
        frame.outline.reserve(frame.corners.size());
        for (const MarkerRect& rect : frame.corners)
        {
            frame.outline.push_back(MarkerRect{rect.x - outlineWidth,
                                               rect.y - outlineWidth,
                                               rect.width + outlineWidth * 2.0f,
                                               rect.height + outlineWidth * 2.0f});
        }
        frame.color = NS::Core::Color{NS::Core::Lerp(desc.appearColor.x, desc.color.x, progress),
                                      NS::Core::Lerp(desc.appearColor.y, desc.color.y, progress),
                                      NS::Core::Lerp(desc.appearColor.z, desc.color.z, progress),
                                      alpha};
        // 白く大きく透けて出る間に暗い縁だけが濃く見えないよう、枠の不透明度に比例させる
        frame.outlineColor = NS::Core::Color{
            desc.outlineColor.x, desc.outlineColor.y, desc.outlineColor.z, desc.outlineAlpha * alpha / desc.frameAlpha};

        outFrame = std::move(frame);
        return true;
    }

    bool BuildTargetMarkerShape(const NS::Core::Matrix& viewProjection,
                                NS::Core::Size2D targetSize,
                                const SlamLineTarget& target,
                                float pathStart,
                                int framesSinceCapture,
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
        if (!BuildLockOnFrame(viewProjection,
                              targetSize,
                              target.bounds,
                              LockOnFrames{.sinceCapture = framesSinceCapture},
                              desc,
                              shape.frame))
        {
            return false;
        }

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
        const bool hadShown = m_hasShown;
        const NS::Core::AABB previousBounds = m_shown.bounds;
        m_hasShown = false;
        m_hasLine = false;
        // 溜め量は放した後も残るので、溜めているかで示すフレームを決める
        if (m_input == nullptr || !m_input->IsCharging())
        {
            // 放したフレームは外れた後の枠も出さない
            m_framesSinceLost = -1;
            return;
        }
        m_hasLine = m_input->TryGetAimLine(m_line);
        m_hasShown = m_input->TryGetAimTarget(m_shown);
        m_pathStart = 0.0f;
        if (m_movement != nullptr)
        {
            m_pathStart = m_movement->CapsuleRadius();
        }

        if (m_hasShown)
        {
            // 別の相手へ替わった時は数え直さず、縮みを繰り返さない
            if (!hadShown)
            {
                m_framesSinceCapture = 0;
            }
            else if (m_framesSinceCapture < std::numeric_limits<int>::max())
            {
                ++m_framesSinceCapture;
            }
            m_framesSinceLost = -1;
            return;
        }
        if (hadShown)
        {
            // 捉えてからのフレーム数は外れたフレームの値で止め、直前に出した枠を縮めて出す
            m_lostBounds = previousBounds;
            m_framesSinceLost = 0;
            return;
        }
        if (m_framesSinceLost >= 0)
        {
            ++m_framesSinceLost;
            if (m_framesSinceLost >= m_desc.lostFrames)
            {
                m_framesSinceLost = -1;
            }
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
        // 暗い縁を先に描き、明るい線を上に重ねる
        for (const MarkerRect& rect : shape.frame.outline)
        {
            context.renderer->DrawScreenRect(rect.x, rect.y, rect.width, rect.height, shape.frame.outlineColor);
        }
        for (const MarkerRect& rect : shape.frame.corners)
        {
            context.renderer->DrawScreenRect(rect.x, rect.y, rect.width, rect.height, shape.frame.color);
        }
        const NS::Core::Color dotColor{m_desc.color.x, m_desc.color.y, m_desc.color.z, k_DotAlpha};
        for (const MarkerRect& rect : shape.dots)
        {
            context.renderer->DrawScreenRect(rect.x, rect.y, rect.width, rect.height, dotColor);
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
            return BuildTargetMarkerShape(
                viewProjection, targetSize, m_shown, m_pathStart, m_framesSinceCapture, m_desc, outShape);
        }
        if (!m_hasLine)
        {
            return false;
        }
        TargetMarkerShape shape{};
        if (!BuildAimPathShape(viewProjection, targetSize, m_line, m_pathStart, m_desc, shape))
        {
            return false;
        }
        if (m_framesSinceLost >= 0)
        {
            const LockOnFrames frames{.sinceCapture = m_framesSinceCapture, .sinceLost = m_framesSinceLost};
            if (!BuildLockOnFrame(viewProjection, targetSize, m_lostBounds, frames, m_desc, shape.frame))
            {
                return false;
            }
        }
        outShape = std::move(shape);
        return true;
    }

    NS_CLASS(TargetMarker)
} // namespace NS::Game::Level
