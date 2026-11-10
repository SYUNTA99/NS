#include "NSlib/CoreObject/ClassObjs/CameraModifier.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Windows/Clock.h"

#include <algorithm>
#include <cmath>

namespace
{
    // 一撃を終えるフレームを、山のフレームの何倍にするか
    constexpr int k_KickEndPeakMultiple = 8;
} // namespace

namespace NS
{
    CameraModifier::~CameraModifier() noexcept = default;

    void CameraModifier::Tick() noexcept
    {
        // 積んだ後の最初の Tick では進めない。積んだフレームに最初の振れと寄りを描く
        if (m_justAdded)
        {
            m_justAdded = false;
            return;
        }
        if (!IsFinished())
        {
            ++m_frame;
            Advance();
        }
    }

    const void* CameraTraumaModifier::StaticKind() noexcept
    {
        static const char kind = 0;
        return &kind;
    }

    void CameraTraumaModifier::AddTrauma(const CameraTraumaDesc& desc) noexcept
    {
        m_trauma = std::min(m_trauma + std::max(desc.trauma, 0.0f), 1.0f);
        m_shape = desc.shape;
        m_seed = desc.seed;
        if (desc.kick.degrees != 0.0f && desc.kick.direction.LengthSquared() > 0.0f)
        {
            m_kick = desc.kick;
            m_kick.peakFrames = std::max(m_kick.peakFrames, 1);
            m_kickFrame = 1;
        }
    }

    float CameraTraumaModifier::ShakeAmount() const noexcept { return std::pow(m_trauma, m_shape.exponent); }

    Vector3 CameraTraumaModifier::Angles() const noexcept
    {
        // 時刻はフレーム数から出す。種と時刻だけで決まり、下見で途中のフレームへ飛んでもその場で引ける
        const float time = static_cast<float>(Frame()) * OS::FrameTimer::FixedDelta() * m_shape.frequency;
        const float amount = ShakeAmount();
        Vector3 angles{m_shape.yawDegrees * amount * ValueNoise1D(time, m_seed),
                           m_shape.pitchDegrees * amount * ValueNoise1D(time, m_seed + 1u),
                           m_shape.rollDegrees * amount * ValueNoise1D(time, m_seed + 2u)};
        if (m_kickFrame > 0)
        {
            const float ratio = static_cast<float>(m_kickFrame) / static_cast<float>(m_kick.peakFrames);
            const float kick = m_kick.degrees * ratio * std::exp(1.0f - ratio);
            Vector2 direction = m_kick.direction;
            direction.Normalize();
            angles.x += kick * direction.x;
            angles.y += kick * direction.y;
        }
        return angles;
    }

    void CameraTraumaModifier::Modify(CameraPose& pose, const CameraAxes& axes) const noexcept
    {
        const Vector3 angles = Angles();
        Vector3 look = pose.target - pose.position;
        const float distance = look.Length();
        if (IsNearlyZero(distance))
        {
            return;
        }
        look /= distance;
        // 右手まわりの符号に合わせる。横は上の軸まわりの正で右を向き、縦と傾きは軸まわりの負で上・右へ倒れる
        const Quaternion yaw = Quaternion::CreateFromAxisAngle(axes.up, DegreesToRadians(angles.x));
        const Quaternion pitch = Quaternion::CreateFromAxisAngle(axes.right, DegreesToRadians(-angles.y));
        const Quaternion turn = yaw * pitch;
        const Vector3 turnedLook = Vector3::Transform(look, turn);
        const Quaternion roll = Quaternion::CreateFromAxisAngle(turnedLook, DegreesToRadians(-angles.z));
        Vector3 up = Vector3::Transform(Vector3::Transform(pose.up, turn), roll);
        up.Normalize();
        pose.target = pose.position + turnedLook * distance;
        pose.up = up;
    }

    bool CameraTraumaModifier::IsFinished() const noexcept
    {
        return m_trauma <= 0.0f && m_kickFrame == 0;
    }

    void CameraTraumaModifier::Advance() noexcept
    {
        m_trauma = std::max(m_trauma - m_shape.decayPerSecond * OS::FrameTimer::FixedDelta(), 0.0f);
        if (m_kickFrame > 0)
        {
            ++m_kickFrame;
            // 山の 8 倍のフレームで e^-7 (山の約 0.6%)。見えなくなったので終える
            if (m_kickFrame > m_kick.peakFrames * k_KickEndPeakMultiple)
            {
                m_kickFrame = 0;
            }
        }
    }
} // namespace NS
