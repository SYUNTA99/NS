#include "Game/Level/CameraHitModifiers.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/IGet/IGetCamera.h"
#include "NSlib/CoreObject/System/CameraManager.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace GL
{
    namespace Level
    {
        float CameraSinkPixelsAt(const CameraSinkDesc& desc, int frame) noexcept
        {
            if (frame < 0 || frame >= desc.frames)
            {
                return 0.0f;
            }
            const float pi = std::numbers::pi_v<float>;
            const float bottom = -desc.bottomPixels;
            if (frame >= desc.bounceStartFrame)
            {
                // 行き過ぎの割合 r から減衰比 z を出し、底から 0 へ戻る減衰振動にする。半周期で -r × 底まで出る
                const float logRatio = std::log(desc.overshootRatio);
                const float damping = -logRatio / std::sqrt(pi * pi + logRatio * logRatio);
                const float dampedOmega = 2.0f * pi / static_cast<float>(desc.bouncePeriodFrames);
                const float omega = dampedOmega / std::sqrt(1.0f - damping * damping);
                const float u = static_cast<float>(frame - desc.bounceStartFrame);
                return bottom * std::exp(-damping * omega * u) *
                       (std::cos(dampedOmega * u) +
                        damping / std::sqrt(1.0f - damping * damping) * std::sin(dampedOmega * u));
            }
            if (frame < desc.sinkFrames)
            {
                return bottom *
                       std::sin(pi * 0.5f * static_cast<float>(frame + 1) / static_cast<float>(desc.sinkFrames));
            }
            const int trembleFrame = frame - desc.sinkFrames;
            if (trembleFrame < desc.trembleFrames)
            {
                const float t = static_cast<float>(trembleFrame);
                const float fade = 1.0f - t / static_cast<float>(desc.trembleFrames);
                return bottom + desc.tremblePixels * fade *
                                    std::cos(2.0f * pi * t / static_cast<float>(desc.tremblePeriodFrames));
            }
            return bottom;
        }

        std::unique_ptr<CameraSinkModifier> CameraSinkModifier::Create(const CameraSinkDesc& desc)
        {
            // 壊れた値が姿へ流れると画面が消える。入口で捨てる
            if (!NS::IsNonNegativeFinite(desc.bottomPixels) || !NS::IsNonNegativeFinite(desc.tremblePixels) ||
                !std::isfinite(desc.overshootRatio) || desc.frames <= 0 || desc.sinkFrames < 1 ||
                desc.trembleFrames < 0 || desc.tremblePeriodFrames < 1 || desc.bouncePeriodFrames < 1 ||
                !NS::IsPositive(desc.overshootRatio) || !NS::IsGreaterThan(1.0f, desc.overshootRatio) ||
                !std::isfinite(desc.referenceHeightPixels) || !NS::IsPositive(desc.referenceHeightPixels))
            {
                return nullptr;
            }
            std::unique_ptr<CameraSinkModifier> sink{new CameraSinkModifier()};
            sink->m_desc = desc;
            return sink;
        }

        const void* CameraSinkModifier::StaticKind() noexcept
        {
            static const char kind = 0;
            return &kind;
        }

        float CameraSinkModifier::Pixels() const noexcept { return CameraSinkPixelsAt(m_desc, Frame()); }

        void CameraSinkModifier::Advance() noexcept
        {
            if (!m_releasePending)
            {
                return;
            }
            m_releasePending = false;
            m_desc.bounceStartFrame = std::min(m_desc.bounceStartFrame, Frame());
        }

        void CameraSinkModifier::Modify(NS::CameraPose& pose, const NS::CameraAxes& axes) const noexcept
        {
            (void)axes;
            pose.screenOffset.y += Pixels() * 2.0f / m_desc.referenceHeightPixels;
        }

        bool CameraSinkModifier::IsFinished() const noexcept { return Frame() >= m_desc.frames; }

        std::unique_ptr<CameraZoomRollModifier> CameraZoomRollModifier::Create(const CameraZoomRollDesc& desc,
                                                                               float rollSign)
        {
            const bool finite =
                std::isfinite(desc.zoom) && std::isfinite(desc.rollDegrees) && NS::IsFinite(desc.rollDirection);
            if (!finite || desc.zoom < 1.0f || desc.holdFrames < 0 || desc.returnFrames < 0 || desc.delayFrames < 0)
            {
                return nullptr;
            }

            float rollDirection = 1.0f;
            if (rollSign < 0.0f)
            {
                rollDirection = -1.0f;
            }
            std::unique_ptr<CameraZoomRollModifier> zoomRoll{new CameraZoomRollModifier()};
            zoomRoll->m_full = CameraZoomRoll{
                .zoom = desc.zoom,
                .rollDegrees = desc.rollDegrees * rollDirection,
            };
            zoomRoll->m_holdFrames = desc.holdFrames;
            zoomRoll->m_returnFrames = desc.returnFrames;
            zoomRoll->m_delayFrames = desc.delayFrames;
            return zoomRoll;
        }

        const void* CameraZoomRollModifier::StaticKind() noexcept
        {
            static const char kind = 0;
            return &kind;
        }

        CameraZoomRoll CameraZoomRollModifier::Current() const noexcept
        {
            const int frame = ZoomFrame();
            if (frame < 0 || frame >= m_holdFrames + m_returnFrames)
            {
                return CameraZoomRoll{};
            }
            if (frame < m_holdFrames)
            {
                return m_full;
            }

            // 重みを 1 - weight と weight に分けて掛けるので、戻しの最後のフレームで倍率 1・傾き 0 ちょうどになる
            const float t = static_cast<float>(frame - m_holdFrames + 1) / static_cast<float>(m_returnFrames);
            const float weight = NS::SmoothStep(t);
            return CameraZoomRoll{
                .zoom = m_full.zoom * (1.0f - weight) + weight,
                .rollDegrees = m_full.rollDegrees * (1.0f - weight),
            };
        }

        void CameraZoomRollModifier::Modify(NS::CameraPose& pose, const NS::CameraAxes& axes) const noexcept
        {
            const CameraZoomRoll zoomRoll = Current();
            if (zoomRoll.zoom != 1.0f)
            {
                pose.fovY = NS::Radians{2.0f * std::atan(std::tan(pose.fovY.value * 0.5f) / zoomRoll.zoom)};
            }
            if (zoomRoll.rollDegrees != 0.0f)
            {
                const float roll = NS::DegreesToRadians(zoomRoll.rollDegrees);
                pose.up = axes.up * std::cos(roll) + axes.right * std::sin(roll);
            }
        }

        bool CameraZoomRollModifier::IsFinished() const noexcept
        {
            return ZoomFrame() >= m_holdFrames + m_returnFrames;
        }

        std::unique_ptr<CameraNudgeModifier> CameraNudgeModifier::Create(const CameraNudgeDesc& desc)
        {
            // 壊れた値が姿へ流れると画面が消える。入口で捨てる
            const NS::Vector3& d = desc.direction;
            if (!NS::IsFinite(d) || !NS::IsNearlyEqual(d.Length(), 1.0f) || desc.frames <= 0 ||
                desc.distance.count > NS::Curve::k_MaxKeys || !std::isfinite(desc.minScreenShare))
            {
                return nullptr;
            }
            // 下限が 0 以下だと、向きが画面の奥を真っすぐ指した時に Modify が 0 で割る
            if (desc.onScreen && !NS::IsPositive(desc.minScreenShare))
            {
                return nullptr;
            }
            for (NS::uint32 i = 0; i < desc.distance.count; ++i)
            {
                const NS::Curve::Key& key = desc.distance.keys[i];
                if (!std::isfinite(key.x) || !std::isfinite(key.y) || !std::isfinite(key.inTangent) ||
                    !std::isfinite(key.outTangent))
                {
                    return nullptr;
                }
            }
            std::unique_ptr<CameraNudgeModifier> nudge{new CameraNudgeModifier()};
            nudge->m_desc = desc;
            return nudge;
        }

        const void* CameraNudgeModifier::KindFor(bool onScreen) noexcept
        {
            static const char world = 0;
            static const char screen = 0;
            if (onScreen)
            {
                return &screen;
            }
            return &world;
        }

        float CameraNudgeModifier::Distance() const noexcept
        {
            if (Frame() >= m_desc.frames)
            {
                return 0.0f;
            }
            return m_desc.distance.Evaluate(static_cast<float>(Frame()));
        }

        void CameraNudgeModifier::Modify(NS::CameraPose& pose, const NS::CameraAxes& axes) const noexcept
        {
            NS::Vector3 direction = m_desc.direction;
            if (m_desc.onScreen)
            {
                // 奥へ向かう分を捨て、画面の右と上の成分だけを長さ 1 にする
                const float right = NS::Dot(direction, axes.right);
                const float up = NS::Dot(direction, axes.up);
                const float onScreen = std::sqrt(right * right + up * up);
                if (!NS::IsGreaterThan(onScreen, m_desc.minScreenShare))
                {
                    return;
                }
                direction = axes.right * (right / onScreen) + axes.up * (up / onScreen);
            }
            const NS::Vector3 shift = direction * Distance();
            pose.position += shift;
            pose.target += shift;
        }

        bool CameraNudgeModifier::IsFinished() const noexcept { return Frame() >= m_desc.frames; }

        bool StartCameraZoomRoll(const NS::IGetCamera* user, const CameraZoomRollDesc& desc)
        {
            NS_ASSERT(Game, user != nullptr, "StartCameraZoomRoll: user が null");
            const NS::CameraManager* cameras = user->GetCameraManager();
            if (cameras == nullptr)
            {
                return false;
            }
            return NS::AddCameraModifier(user, CameraZoomRollModifier::Create(desc, cameras->SideSignOf(desc.rollDirection)));
        }

        CameraZoomRoll CurrentCameraZoomRoll(const NS::CameraManager& cameras) noexcept
        {
            if (const CameraZoomRollModifier* zoomRoll = cameras.FindModifier<CameraZoomRollModifier>())
            {
                return zoomRoll->Current();
            }
            return CameraZoomRoll{};
        }

        void ReleaseCameraSink(const NS::IGetCamera* user) noexcept
        {
            NS_ASSERT(Game, user != nullptr, "ReleaseCameraSink: user が null");
            NS::CameraManager* cameras = user->GetCameraManager();
            if (cameras == nullptr)
            {
                return;
            }
            if (CameraSinkModifier* sink = cameras->FindModifierOf<CameraSinkModifier>(user))
            {
                sink->Release();
            }
        }
    } // namespace Level
} // namespace GL
