#include "Game/Level/TargetMarker.h"

#include "Game/Level/ImpactInputJudge.h"
#include "Game/Player/PlayerStatus.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Gfx/Render/ScreenOverlay.h"
#include "NSlib/Gfx/Render/RenderContext.h"
#include "NSlib/Gfx/D3DDevice.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace GL
{
    namespace Level
    {
        namespace
        {
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

            [[nodiscard]] bool IsValidFrameDesc(const TargetMarkerDesc& desc) noexcept
            {
                if (!NS::IsFinite(desc.color.ToVector3()) || !NS::IsFinite(desc.appearColor.ToVector3()) ||
                    !NS::IsFinite(desc.outlineColor.ToVector3()))
                {
                    return false;
                }
                if (!NS::IsPositiveFinite(desc.referenceHeight) || !NS::IsPositiveFinite(desc.frameMinSide) ||
                    !NS::IsPositiveFinite(desc.color.w) || !NS::IsPositiveFinite(desc.appearScale) ||
                    !NS::IsPositiveFinite(desc.appearMaxSide) || !NS::IsPositiveFinite(desc.lostScale))
                {
                    return false;
                }
                if (!NS::IsNonNegativeFinite(desc.frameGap) || !NS::IsNonNegativeFinite(desc.appearColor.w) ||
                    !NS::IsNonNegativeFinite(desc.outlineColor.w) || !NS::IsNonNegativeFinite(desc.outlineWidth))
                {
                    return false;
                }
                // 点の数が上限を超えると Evaluate が配列の外を読む
                return desc.appearFrames >= 0 && desc.lostFrames >= 0 && desc.appearCurve.count <= NS::Curve::k_MaxKeys;
            }

            [[nodiscard]] bool CanBuild(NS::Size2D targetSize, const TargetMarkerDesc& desc) noexcept
            {
                if (!NS::IsPositiveFinite(desc.lineThickness) || !NS::IsPositiveFinite(desc.armRatio))
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
                const float time = NS::Clamp(elapsed / static_cast<float>(desc.appearFrames), 0.0f, 1.0f);
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
                return NS::Clamp(progress, 0.0f, 1.0f);
            }

        } // namespace

        bool BuildLockOnFrame(const NS::Matrix& viewProjection,
                              NS::Size2D targetSize,
                              const NS::AABB& bounds,
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
            const float pixelScale = height / desc.referenceHeight;
            const NS::Vector3 center{bounds.Center.x, bounds.Center.y, bounds.Center.z};
            NS::Vector2 centerPixel{};
            float centerW = 0.0f;
            if (!NS::Gfx::TryProjectToPixels(viewProjection, center, width, height, centerPixel, centerW))
            {
                outFrame = std::move(frame);
                return true;
            }

            // 中心と同じ奥行きの面の上の半径なので、カメラの向きで大きさが変わらない
            const float radius = std::max({bounds.Extents.x, bounds.Extents.y, bounds.Extents.z});
            const float radiusPixels = NS::Gfx::ProjectedLengthPixels(viewProjection, radius, centerW, height);
            const float settledSide =
                std::max(2.0f * (radiusPixels + desc.frameGap * pixelScale), desc.frameMinSide * pixelScale);
            // 近い大きな相手で一辺が上限を超える時は、縮まずに出る
            const float appearSide =
                std::max(std::min(settledSide * desc.appearScale, desc.appearMaxSide * pixelScale), settledSide);

            const float progress = AppearProgress(frames.sinceCapture, desc);
            float side = NS::Lerp(appearSide, settledSide, progress);
            if (lost)
            {
                side *= desc.lostScale;
            }
            const float alpha = NS::Lerp(desc.appearColor.w, desc.color.w, progress);

            AppendCornerHooks(centerPixel.x - side * 0.5f,
                              centerPixel.y - side * 0.5f,
                              side,
                              desc.lineThickness * pixelScale,
                              desc.armRatio,
                              frame.corners);
            const float outlineWidth = desc.outlineWidth * pixelScale;
            frame.outline.reserve(frame.corners.size());
            for (const MarkerRect& rect : frame.corners)
            {
                frame.outline.push_back(MarkerRect{rect.x - outlineWidth,
                                                   rect.y - outlineWidth,
                                                   rect.width + outlineWidth * 2.0f,
                                                   rect.height + outlineWidth * 2.0f});
            }
            frame.color = NS::Color{NS::Lerp(desc.appearColor.x, desc.color.x, progress),
                                    NS::Lerp(desc.appearColor.y, desc.color.y, progress),
                                    NS::Lerp(desc.appearColor.z, desc.color.z, progress),
                                    alpha};
            // 白く大きく透けて出る間に暗い縁だけが濃く見えないよう、枠の不透明度に比例させる
            frame.outlineColor = NS::Color{desc.outlineColor.x,
                                           desc.outlineColor.y,
                                           desc.outlineColor.z,
                                           desc.outlineColor.w * alpha / desc.color.w};

            outFrame = std::move(frame);
            return true;
        }

        void TargetMarker::Update(const GL::Player::ChargeStatus& charge)
        {
            const bool hadShown = m_hasShown;
            const NS::AABB previousBounds = m_shown.bounds;
            m_hasShown = false;
            // 溜め量は放した後も残るので、溜めているかで示すフレームを決める
            if (!charge.Judge().IsCharging())
            {
                // 放したフレームは外れた後の枠も出さない
                m_framesSinceLost = -1;
                return;
            }
            m_hasShown = charge.TryGetAimTarget(m_shown);

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
            if (context.device == nullptr || context.overlay == nullptr)
            {
                return;
            }
            LockOnFrameShape frame{};
            if (!BuildShownShape(context.viewProjection, context.device->Size(), frame))
            {
                return;
            }
            // 暗い縁を先に描き、明るい線を上に重ねる
            for (const MarkerRect& rect : frame.outline)
            {
                context.overlay->DrawScreenRect(rect.x, rect.y, rect.width, rect.height, frame.outlineColor);
            }
            for (const MarkerRect& rect : frame.corners)
            {
                context.overlay->DrawScreenRect(rect.x, rect.y, rect.width, rect.height, frame.color);
            }
        }

        NS::SceneActor* TargetMarker::ShownTarget() const noexcept
        {
            if (!m_hasShown)
            {
                return nullptr;
            }
            return m_shown.target;
        }

        bool TargetMarker::BuildShownShape(const NS::Matrix& viewProjection,
                                           NS::Size2D targetSize,
                                           LockOnFrameShape& outFrame) const
        {
            if (m_hasShown)
            {
                return BuildLockOnFrame(viewProjection,
                                        targetSize,
                                        m_shown.bounds,
                                        LockOnFrames{.sinceCapture = m_framesSinceCapture},
                                        m_desc,
                                        outFrame);
            }
            if (m_framesSinceLost < 0)
            {
                return false;
            }
            const LockOnFrames frames{.sinceCapture = m_framesSinceCapture, .sinceLost = m_framesSinceLost};
            return BuildLockOnFrame(viewProjection, targetSize, m_lostBounds, frames, m_desc, outFrame);
        }
    } // namespace Level
} // namespace GL
