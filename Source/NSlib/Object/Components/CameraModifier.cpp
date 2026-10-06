#include "NSlib/Object/Components/CameraModifier.h"

#include "NSlib/Windows/Clock.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

namespace
{
    // 一撃を終えるフレームを、山のフレームの何倍にするか
    constexpr int k_KickEndPeakMultiple = 8;
    // 沈む揺れの画素を測る画面の高さ。画面の座標 (縦 -1〜1) へ直す時の割る数で、解像度が違っても同じ割合だけ動く
    constexpr float k_SinkReferenceHeightPixels = 1080.0f;
    // ずれの向きを長さ 1 と見なす誤差。設定の数字の丸めで外れない幅
    constexpr float k_UnitLengthTolerance = 1.0e-3f;
    // 画面へ写した向きの長さがこれ未満なら、向きが画面の奥をほぼ真っすぐ指していて画面の上の向きが決まらないので動かさない
    constexpr float k_MinScreenShare = 0.2f;
} // namespace

namespace NS::Obj
{
    namespace
    {
        [[nodiscard]] bool IsFiniteVector(const NS::Vector3& v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        // 向きが入れ替わるまでのフレーム数を 1〜longest から選ぶ。longest が 2 以上なら previous と同じ数を選ばない
        // 分布は実装ごとに結果が違うので通さず、生成器の出力の余りから選ぶ
        [[nodiscard]] int PickFlipFrames(std::mt19937& generator, int longest, int previous) noexcept
        {
            if (longest <= 1)
            {
                return 1;
            }
            if (previous < 1)
            {
                return 1 + static_cast<int>(generator() % static_cast<std::uint32_t>(longest));
            }
            // previous を除いた longest - 1 通りから選び、previous 以上は 1 つ後ろへずらす
            int picked = 1 + static_cast<int>(generator() % static_cast<std::uint32_t>(longest - 1));
            if (picked >= previous)
            {
                ++picked;
            }
            return picked;
        }

        // offsets の各フレームの axis の成分へ向きの符号を書く。最初は firstSign
        void FillFlipSigns(std::mt19937& generator,
                           int longest,
                           float firstSign,
                           float NS::Vector2::* axis,
                           std::vector<NS::Vector2>& offsets) noexcept
        {
            float sign = firstSign;
            int previous = 0;
            std::size_t frame = 0;
            while (frame < offsets.size())
            {
                const int run = PickFlipFrames(generator, longest, previous);
                for (int step = 0; step < run && frame < offsets.size(); ++step)
                {
                    offsets[frame].*axis = sign;
                    ++frame;
                }
                sign = -sign;
                previous = run;
            }
        }
    } // namespace

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
            Advance();
        }
    }

    std::unique_ptr<CameraShakeModifier> CameraShakeModifier::Create(const CameraShakeDesc& desc, float firstSideSign)
    {
        // 壊れた値が pose へ流れると視点が消える。入口で捨てる
        const bool finite = std::isfinite(desc.sideAmplitude) && std::isfinite(desc.upAmplitude) &&
                            IsFiniteVector(desc.firstSideDirection);
        if (!finite || desc.sideAmplitude < 0.0f || desc.upAmplitude < 0.0f || desc.frames <= 0 ||
            desc.longestFlipFrames < 1)
        {
            return nullptr;
        }

        std::unique_ptr<CameraShakeModifier> shake{new CameraShakeModifier()};
        // seed_seq と mt19937 は手順が規格で決まっているので、同じ種なら実装によらず同じ並びになる
        std::seed_seq seeds{desc.seed};
        std::mt19937 generator(seeds);
        shake->m_offsets.assign(static_cast<std::size_t>(desc.frames), NS::Vector2{0.0f, 0.0f});
        float sideSign = 1.0f;
        if (firstSideSign < 0.0f)
        {
            sideSign = -1.0f;
        }
        FillFlipSigns(generator, desc.longestFlipFrames, sideSign, &NS::Vector2::x, shake->m_offsets);
        FillFlipSigns(generator, desc.longestFlipFrames, -1.0f, &NS::Vector2::y, shake->m_offsets);

        // 始めたフレームが最大で、残りのフレーム数に比例して減る
        for (int frame = 0; frame < desc.frames; ++frame)
        {
            const float decay = static_cast<float>(desc.frames - frame) / static_cast<float>(desc.frames);
            NS::Vector2& offset = shake->m_offsets[static_cast<std::size_t>(frame)];
            offset.x *= desc.sideAmplitude * decay;
            offset.y *= desc.upAmplitude * decay;
        }
        return shake;
    }

    const void* CameraShakeModifier::StaticKind() noexcept
    {
        static const char kind = 0;
        return &kind;
    }

    NS::Vector2 CameraShakeModifier::Offset() const noexcept
    {
        if (m_frame < static_cast<int>(m_offsets.size()))
        {
            return m_offsets[static_cast<std::size_t>(m_frame)];
        }
        return NS::Vector2{0.0f, 0.0f};
    }

    void CameraShakeModifier::Modify(CameraPose& pose, const CameraAxes& axes) const noexcept
    {
        const NS::Vector2 shake = Offset();
        if (shake.x == 0.0f && shake.y == 0.0f)
        {
            return;
        }
        const NS::Vector3 offset = axes.right * shake.x + axes.up * shake.y;
        pose.position += offset;
        pose.target += offset;
    }

    bool CameraShakeModifier::IsFinished() const noexcept
    {
        return m_frame >= static_cast<int>(m_offsets.size());
    }

    void CameraShakeModifier::Advance() noexcept
    {
        ++m_frame;
    }

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
            return bottom * std::sin(pi * 0.5f * static_cast<float>(frame + 1) / static_cast<float>(desc.sinkFrames));
        }
        const int trembleFrame = frame - desc.sinkFrames;
        if (trembleFrame < desc.trembleFrames)
        {
            const float t = static_cast<float>(trembleFrame);
            const float fade = 1.0f - t / static_cast<float>(desc.trembleFrames);
            return bottom +
                   desc.tremblePixels * fade * std::cos(2.0f * pi * t / static_cast<float>(desc.tremblePeriodFrames));
        }
        return bottom;
    }

    std::unique_ptr<CameraSinkModifier> CameraSinkModifier::Create(const CameraSinkDesc& desc)
    {
        // 壊れた値が姿へ流れると画面が消える。入口で捨てる
        const bool finite =
            std::isfinite(desc.bottomPixels) && std::isfinite(desc.tremblePixels) && std::isfinite(desc.overshootRatio);
        if (!finite || desc.bottomPixels < 0.0f || desc.tremblePixels < 0.0f || desc.frames <= 0 ||
            desc.sinkFrames < 1 || desc.trembleFrames < 0 || desc.tremblePeriodFrames < 1 ||
            desc.bouncePeriodFrames < 1 || !(desc.overshootRatio > 0.0f) || !(desc.overshootRatio < 1.0f))
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

    float CameraSinkModifier::Pixels() const noexcept
    {
        return CameraSinkPixelsAt(m_desc, m_frame);
    }

    void CameraSinkModifier::Modify(CameraPose& pose, const CameraAxes& axes) const noexcept
    {
        (void)axes;
        pose.screenOffset.y += Pixels() * 2.0f / k_SinkReferenceHeightPixels;
    }

    bool CameraSinkModifier::IsFinished() const noexcept
    {
        return m_frame >= m_desc.frames;
    }

    void CameraSinkModifier::Advance() noexcept
    {
        ++m_frame;
    }

    std::unique_ptr<CameraZoomRollModifier> CameraZoomRollModifier::Create(const CameraZoomRollDesc& desc,
                                                                           float rollSign)
    {
        const bool finite =
            std::isfinite(desc.zoom) && std::isfinite(desc.rollDegrees) && IsFiniteVector(desc.rollDirection);
        if (!finite || desc.zoom < 1.0f || desc.holdFrames < 0 || desc.returnFrames < 0)
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
        return zoomRoll;
    }

    const void* CameraZoomRollModifier::StaticKind() noexcept
    {
        static const char kind = 0;
        return &kind;
    }

    CameraZoomRoll CameraZoomRollModifier::Current() const noexcept
    {
        if (m_frame >= m_holdFrames + m_returnFrames)
        {
            return CameraZoomRoll{};
        }
        if (m_frame < m_holdFrames)
        {
            return m_full;
        }

        // 重みを 1 - weight と weight に分けて掛けるので、戻しの最後のフレームで倍率 1・傾き 0 ちょうどになる
        const float t = static_cast<float>(m_frame - m_holdFrames + 1) / static_cast<float>(m_returnFrames);
        const float weight = NS::SmoothStep(t);
        return CameraZoomRoll{
            .zoom = m_full.zoom * (1.0f - weight) + weight,
            .rollDegrees = m_full.rollDegrees * (1.0f - weight),
        };
    }

    void CameraZoomRollModifier::Modify(CameraPose& pose, const CameraAxes& axes) const noexcept
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
        return m_frame >= m_holdFrames + m_returnFrames;
    }

    void CameraZoomRollModifier::Advance() noexcept
    {
        ++m_frame;
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

    void CameraTraumaModifier::HoldTrauma(float level, const CameraTraumaShape& shape) noexcept
    {
        const float clamped = NS::Clamp(level, 0.0f, 1.0f);
        // 弱い保ちは強い揺れの形を奪わない。外れの揺れの途中に溜め始めても、外れの揺れのまま減る
        if (clamped >= m_trauma)
        {
            m_shape = shape;
        }
        m_trauma = std::max(m_trauma, clamped);
        m_held = std::max(m_held, clamped);
    }

    float CameraTraumaModifier::ShakeAmount() const noexcept
    {
        return std::pow(m_trauma, m_shape.exponent);
    }

    NS::Vector3 CameraTraumaModifier::Angles() const noexcept
    {
        // 時刻はフレーム数から出す。種と時刻だけで決まり、下見で途中のフレームへ飛んでもその場で引ける
        const float time = static_cast<float>(m_frame) * NS::OS::FrameTimer::FixedDelta() * m_shape.frequency;
        const float amount = ShakeAmount();
        NS::Vector3 angles{m_shape.yawDegrees * amount * NS::ValueNoise1D(time, m_seed),
                                 m_shape.pitchDegrees * amount * NS::ValueNoise1D(time, m_seed + 1u),
                                 m_shape.rollDegrees * amount * NS::ValueNoise1D(time, m_seed + 2u)};
        if (m_kickFrame > 0)
        {
            const float ratio = static_cast<float>(m_kickFrame) / static_cast<float>(m_kick.peakFrames);
            const float kick = m_kick.degrees * ratio * std::exp(1.0f - ratio);
            NS::Vector2 direction = m_kick.direction;
            direction.Normalize();
            angles.x += kick * direction.x;
            angles.y += kick * direction.y;
        }
        return angles;
    }

    void CameraTraumaModifier::Modify(CameraPose& pose, const CameraAxes& axes) const noexcept
    {
        const NS::Vector3 angles = Angles();
        NS::Vector3 look = pose.target - pose.position;
        const float distance = look.Length();
        if (distance <= NS::k_Epsilon)
        {
            return;
        }
        look /= distance;
        // 右手まわりの符号に合わせる。横は上の軸まわりの正で右を向き、縦と傾きは軸まわりの負で上・右へ倒れる
        const NS::Quaternion yaw =
            NS::Quaternion::CreateFromAxisAngle(axes.up, NS::DegreesToRadians(angles.x));
        const NS::Quaternion pitch =
            NS::Quaternion::CreateFromAxisAngle(axes.right, NS::DegreesToRadians(-angles.y));
        const NS::Quaternion turn = yaw * pitch;
        const NS::Vector3 turnedLook = NS::Vector3::Transform(look, turn);
        const NS::Quaternion roll =
            NS::Quaternion::CreateFromAxisAngle(turnedLook, NS::DegreesToRadians(-angles.z));
        NS::Vector3 up = NS::Vector3::Transform(NS::Vector3::Transform(pose.up, turn), roll);
        up.Normalize();
        pose.target = pose.position + turnedLook * distance;
        pose.up = up;
    }

    bool CameraTraumaModifier::IsFinished() const noexcept
    {
        return m_trauma <= 0.0f && m_held <= 0.0f && m_kickFrame == 0;
    }

    void CameraTraumaModifier::Advance() noexcept
    {
        ++m_frame;
        m_trauma = std::max(m_trauma - m_shape.decayPerSecond * NS::OS::FrameTimer::FixedDelta(), 0.0f);
        // 保たれたフレームは減らさない。頼みは 1 フレームだけ効く
        m_trauma = std::max(m_trauma, m_held);
        m_held = 0.0f;
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

    std::unique_ptr<CameraNudgeModifier> CameraNudgeModifier::Create(const CameraNudgeDesc& desc)
    {
        // 壊れた値が姿へ流れると画面が消える。入口で捨てる
        const NS::Vector3& d = desc.direction;
        if (!std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z) ||
            !(std::abs(d.Length() - 1.0f) < k_UnitLengthTolerance) || desc.frames <= 0 ||
            desc.distance.count > Curve::k_MaxKeys)
        {
            return nullptr;
        }
        for (std::uint32_t i = 0; i < desc.distance.count; ++i)
        {
            const Curve::Key& key = desc.distance.keys[i];
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
        if (m_frame >= m_desc.frames)
        {
            return 0.0f;
        }
        return m_desc.distance.Evaluate(static_cast<float>(m_frame));
    }

    void CameraNudgeModifier::Modify(CameraPose& pose, const CameraAxes& axes) const noexcept
    {
        NS::Vector3 direction = m_desc.direction;
        if (m_desc.onScreen)
        {
            // 奥へ向かう分を捨て、画面の右と上の成分だけを長さ 1 にする
            const float right = NS::Dot(direction, axes.right);
            const float up = NS::Dot(direction, axes.up);
            const float onScreen = std::sqrt(right * right + up * up);
            if (!(onScreen > k_MinScreenShare))
            {
                return;
            }
            direction = axes.right * (right / onScreen) + axes.up * (up / onScreen);
        }
        const NS::Vector3 shift = direction * Distance();
        pose.position += shift;
        pose.target += shift;
    }

    bool CameraNudgeModifier::IsFinished() const noexcept
    {
        return m_frame >= m_desc.frames;
    }

    void CameraNudgeModifier::Advance() noexcept
    {
        ++m_frame;
    }
} // namespace NS::Obj
