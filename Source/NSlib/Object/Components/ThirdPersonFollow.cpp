#include "NSlib/Object/Components/ThirdPersonFollow.h"

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/IUse/IUseCamera.h"
#include "NSlib/Object/ObjectList.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Transform.h"
#include "NSlib/Windows/Clock.h"
#include "NSlib/Windows/Gamepad.h"
#include "NSlib/Windows/Input.h"
#include "NSlib/Windows/Mouse.h"

#include <algorithm>
#include <cmath>

namespace
{
    [[nodiscard]] float SpringApproach(float curr, float target, float omega, float dt) noexcept
    {
        if (omega <= 0.0f || dt <= 0.0f)
        {
            return target;
        }
        const float a = 1.0f - std::exp(-omega * dt);
        return curr + (target - curr) * a;
    }

    // 構図の視野の縦横比。ThirdPersonFollow は窓の縦横比を知らないので 16 : 9 と決める
    constexpr float k_ChargeFrameAspect = 16.0f / 9.0f;
    // これより小さい構図のずらしは 0 にする (m)。画素の 1/100 未満
    constexpr float k_ChargeFrameSnap = 0.0001f;

    // 臨界減衰のバネを dt 進める。止まった所から一定の目標へは、τ 秒で差の 1 − (1 + ωτ) e^(−ωτ) が詰まる
    void CriticalSpringStep(float& value, float& velocity, float target, float omega, float dt) noexcept
    {
        if (omega <= 0.0f || dt <= 0.0f)
        {
            value = target;
            velocity = 0.0f;
            return;
        }
        const float decay = std::exp(-omega * dt);
        const float gap = value - target;
        const float push = (velocity + omega * gap) * dt;
        velocity = (velocity - omega * push) * decay;
        value = target + (gap + push) * decay;
    }

    // 右は水平で、上は視線と右に直交する
    struct ViewAxes
    {
        NS::Vector3 forward;
        NS::Vector3 right;
        NS::Vector3 up;
    };

    [[nodiscard]] ViewAxes ViewAxesFromYawPitch(float yaw, float pitch) noexcept
    {
        const float cy = std::cos(yaw);
        const float sy = std::sin(yaw);
        const float cp = std::cos(pitch);
        const float sp = std::sin(pitch);
        return ViewAxes{.forward = NS::Vector3{sy * cp, sp, cy * cp},
                        .right = NS::Vector3{cy, 0.0f, -sy},
                        .up = NS::Vector3{-sp * sy, cp, -sp * cy}};
    }

    // カメラから見た 1 点の、右と上の向きの位置と、枠の半分の幅
    struct FramePoint
    {
        bool usable = false; // カメラの前にあるか。後ろの点は枠の決まりから外す
        float right = 0.0f;
        float up = 0.0f;
        float halfWidth = 0.0f;
        float halfHeight = 0.0f;
    };

    [[nodiscard]] FramePoint ToFramePoint(const NS::Vector3& point,
                                          const NS::Vector3& cameraPosition,
                                          const ViewAxes& axes,
                                          float frameTanHalf) noexcept
    {
        const NS::Vector3 offset = point - cameraPosition;
        const float depth = NS::Dot(offset, axes.forward);
        FramePoint framePoint{};
        if (depth <= 0.0f)
        {
            return framePoint;
        }
        framePoint.usable = true;
        framePoint.right = NS::Dot(offset, axes.right);
        framePoint.up = NS::Dot(offset, axes.up);
        framePoint.halfHeight = depth * frameTanHalf;
        framePoint.halfWidth = framePoint.halfHeight * k_ChargeFrameAspect;
        return framePoint;
    }

    // 1 つの軸で、自機と相手を枠の内に入れるずらし
    // 相手を入れる範囲のうち 0 に一番近い値を、自機を入れる範囲へ丸める。両方は入らない時は自機を枠に残す
    [[nodiscard]] float FrameAxisShift(
        bool selfUsable, float self, float selfHalf, bool targetUsable, float target, float targetHalf) noexcept
    {
        float shift = 0.0f;
        if (targetUsable)
        {
            shift = NS::Clamp(0.0f, target - targetHalf, target + targetHalf);
        }
        if (selfUsable)
        {
            shift = NS::Clamp(shift, self - selfHalf, self + selfHalf);
        }
        return shift;
    }
} // namespace

namespace NS::Obj
{

    ThirdPersonFollow::ThirdPersonFollow() noexcept : VirtualCamera()
    {
        // 追う相手を中心に見るので遠景は要らない。編集カメラの 5000 と違いプレイ視点は 100 で足りる
        SetFarPlane(100.0f);
    }

    Transform* ThirdPersonFollow::Target() const noexcept
    {
        // 追う相手は控えず使うたびに引く。控えると、消された相手を指したまま次のフレームへ持ち越す
        if (!m_targetRef.IsSet() || Owner() == nullptr || Owner()->OwningScene() == nullptr)
        {
            return nullptr;
        }
        Actor* target = Owner()->OwningScene()->Objects().FindObject(m_targetRef);
        if (target == nullptr)
        {
            return nullptr;
        }
        return &target->Root();
    }

    void ThirdPersonFollow::SetFollowMotion(bool grounded, const NS::Vector3& velocity) noexcept
    {
        m_followGrounded = grounded;
        const float horiz = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
        // 非数を持つと走り判定の比較が常に偽になり、走っても待機時距離のまま
        if (std::isfinite(horiz))
        {
            m_followHorizontalSpeed = horiz;
        }
        else
        {
            m_followHorizontalSpeed = 0.0f;
        }
        m_hasFollowMotion = true;
    }

    void ThirdPersonFollow::SetTargetHeightOffset(float offset) noexcept
    {
        m_targetHeightOffset = offset;
    }

    bool ThirdPersonFollow::SetFollowCharge(const FollowChargeDesc& desc) noexcept
    {
        if (!std::isfinite(desc.charge01) || desc.charge01 < 0.0f || desc.charge01 > 1.0f)
        {
            return false;
        }
        if (desc.hasAimTarget && !NS::IsFinite(desc.aimTargetCenter))
        {
            return false;
        }
        if (desc.hasAimTarget && !NS::IsNonNegativeFinite(desc.aimTargetRadius))
        {
            return false;
        }
        m_charge = desc;
        return true;
    }

    void ThirdPersonFollow::ClearCharge() noexcept
    {
        m_charge = FollowChargeDesc{};
        m_chargeHoldNarrowDegrees = 0.0f;
        m_chargeNarrowDegrees = 0.0f;
        m_chargeReturnWaiting = false;
        m_chargeReturnFromDegrees = 0.0f;
        m_chargeReturnFrame = 0;
        m_chargeFrameOffset = NS::Vector2{0.0f, 0.0f};
        m_chargeFrameVelocity = NS::Vector2{0.0f, 0.0f};
        m_chargeCenterTilt = 0.0f;
        m_chargeCenterTiltVelocity = 0.0f;
    }

    bool ThirdPersonFollow::SetFollowRebound(const FollowReboundDesc& desc) noexcept
    {
        // 非数の向きを持つと、回す角度が非数になって向きごと壊れる
        if (!NS::IsFinite(desc.slamDirection))
        {
            return false;
        }
        m_rebound = desc;
        return true;
    }

    void ThirdPersonFollow::ClearRebound() noexcept
    {
        m_rebound = FollowReboundDesc{};
        m_wasRebounding = false;
        m_wasForcedSlamming = false;
        m_reboundLagCap = 0.0f;
        m_reboundLookHeld = false;
        m_reboundTurnAngle = 0.0f;
        m_reboundTurnFrame = 0;
        m_reboundPhase = ReboundPhase::None;
        m_reboundAnchor = NS::Vector3{0.0f, 0.0f, 0.0f};
        m_reboundAnchorVelocity = NS::Vector3{0.0f, 0.0f, 0.0f};
        m_reboundReturnOffset = NS::Vector3{0.0f, 0.0f, 0.0f};
        m_reboundReturnFrame = 0;
        m_hasLook = false;
        m_reboundBaseDistance = 0.0f;
        m_partnerLean = NS::Vector3{0.0f, 0.0f, 0.0f};
        m_partnerLeanVelocity = NS::Vector3{0.0f, 0.0f, 0.0f};
        m_partnerPull = 0.0f;
        m_partnerPullVelocity = 0.0f;
    }

    void ThirdPersonFollow::UpdateReboundTurn(bool began, const FollowReboundDesc& rebound) noexcept
    {
        // 空中の 1 発が当たって反動になり直した時も、その 1 発を出した向きで回し直す
        if (began)
        {
            m_reboundLookHeld = true;
            m_reboundTurnAngle = 0.0f;
            m_reboundTurnFrame = 0;
            NS::Vector3 direction{};
            if (NS::TryNormalizeHorizontal(rebound.slamDirection, direction))
            {
                // 差を −π〜π へ丸めて近い側へ回す
                // 丸めないと、向きの角度が積もった分だけ余計に回る
                const float goal = std::atan2(direction.x, direction.z);
                m_reboundTurnAngle = std::remainder(goal - m_yaw, 2.0f * NS::k_Pi);
            }
            if (m_reboundTurnFrames <= 0)
            {
                m_yaw += m_reboundTurnAngle;
                m_reboundTurnAngle = 0.0f;
            }
        }
        else if (m_reboundLookHeld && !rebound.rebounding && (m_followGrounded || !m_hasFollowMotion))
        {
            // 反動の状態が外れても空中の間は受けない
            // 空中の 1 発の後と縁を掴んだ後も、接地までは向きを変えない
            m_reboundLookHeld = false;
        }

        // 回しは接地しても最後まで進める
        // 途中で止めると、回る速さがそのフレームで 0 に落ちる
        // 重みの差を足していくので、回す入力を受けるようになった後はその入力と重なる
        if (m_reboundTurnAngle != 0.0f && m_reboundTurnFrame < m_reboundTurnFrames)
        {
            const float frames = static_cast<float>(m_reboundTurnFrames);
            const float before = NS::SmoothStep(static_cast<float>(m_reboundTurnFrame) / frames);
            ++m_reboundTurnFrame;
            const float after = NS::SmoothStep(static_cast<float>(m_reboundTurnFrame) / frames);
            m_yaw += m_reboundTurnAngle * (after - before);
        }
    }

    void ThirdPersonFollow::UpdateReboundPhase(
        bool began, bool rebounding, bool launchBegan, bool launching, const NS::Vector3& head) noexcept
    {
        const auto anchorAtLastLook = [this, &head]() {
            m_reboundAnchor = head;
            if (m_hasLook)
            {
                m_reboundAnchor = m_look;
            }
            m_reboundAnchorVelocity = NS::Vector3{0.0f, 0.0f, 0.0f};
        };

        // 反動の状態になったフレームに、前のフレームに見ていた所から留める
        // 空中で続けて当てた時も留め直す。勝手に出た突進の途中で当たった時も、遅れた注視点から続けるので跳ばない
        if (began)
        {
            m_reboundPhase = ReboundPhase::Following;
            anchorAtLastLook();
            // 勝手に出た突進の遅れは反動の上限より大きいことがある。入った時の遅れを上限の初めにして、切って跳ばさない
            const float lagX = m_reboundAnchor.x - head.x;
            const float lagZ = m_reboundAnchor.z - head.z;
            m_reboundLagCap = std::max(m_reboundMaxLag, std::sqrt(lagX * lagX + lagZ * lagZ));
            return;
        }

        // 勝手に出た突進になったフレームに、前のフレームに見ていた所へ取り残す。バネは止まった所から動き出すので、
        // 出た最初の数フレームは注視点がほぼ動かない
        if (launchBegan)
        {
            m_reboundPhase = ReboundPhase::Launching;
            anchorAtLastLook();
            return;
        }

        // 反動の状態か勝手に出た突進が外れたフレームから寄せ戻す
        // 着地のほか、空中の 1 発と縁を掴んだ時も外れる。突進は当たらずに止まった時に外れる
        if ((m_reboundPhase == ReboundPhase::Following && !rebounding) ||
            (m_reboundPhase == ReboundPhase::Launching && !launching))
        {
            if (m_reboundReturnFrames > 0 && m_hasLook)
            {
                m_reboundPhase = ReboundPhase::Returning;
                m_reboundReturnOffset = m_look - head;
                m_reboundReturnFrame = 0;
            }
            else
            {
                m_reboundPhase = ReboundPhase::None;
            }
            return;
        }

        // 寄せ戻しの最後のフレームは注視点が頭と同じ所にある
        // その次のフレームから普通の追い方の式へ移す
        if (m_reboundPhase == ReboundPhase::Returning && m_reboundReturnFrame >= m_reboundReturnFrames)
        {
            m_reboundPhase = ReboundPhase::None;
        }
    }

    void ThirdPersonFollow::UpdateReboundPartner(const FollowReboundDesc& rebound,
                                                 const NS::Vector3& head,
                                                 float dt) noexcept
    {
        NS::Vector3 leanTarget{0.0f, 0.0f, 0.0f};
        float pullTarget = 0.0f;
        const float release = std::max(m_reboundPartnerReleaseDistance, 0.0f);
        if (m_reboundPhase == ReboundPhase::Following && rebound.partnerPosition.has_value() && release > 0.0f)
        {
            const NS::Vector3 gap = rebound.partnerPosition.value() - head;
            const float distance = gap.Length();
            // 見送った後は引きも戻す。目標は 0 へ跳ぶが、ばねがなめらかに戻す
            if (std::isfinite(distance) && distance < release)
            {
                const float weight = NS::Clamp(m_reboundPartnerWeight, 0.0f, 1.0f) * (1.0f - distance / release);
                leanTarget = gap * weight;
                pullTarget = NS::Clamp(m_reboundPullPerMeter * (distance - m_reboundPullStartDistance),
                                       0.0f,
                                       std::max(m_reboundPullMax, 0.0f));
            }
        }
        // 臨界減衰のバネが止まった所から差の半分まで詰めるのは ωτ ≈ 1.678 の時。半分の秒から角速度を出す
        float omega = 0.0f;
        if (m_reboundPartnerHalfSeconds > 0.0f)
        {
            omega = 1.678f / m_reboundPartnerHalfSeconds;
        }
        CriticalSpringStep(m_partnerLean.x, m_partnerLeanVelocity.x, leanTarget.x, omega, dt);
        CriticalSpringStep(m_partnerLean.y, m_partnerLeanVelocity.y, leanTarget.y, omega, dt);
        CriticalSpringStep(m_partnerLean.z, m_partnerLeanVelocity.z, leanTarget.z, omega, dt);
        CriticalSpringStep(m_partnerPull, m_partnerPullVelocity, pullTarget, omega, dt);
    }

    NS::Vector3 ThirdPersonFollow::UpdateReboundLook(const NS::Vector3& head,
                                                     const NS::Vector3& ball,
                                                     float dt) noexcept
    {
        m_previousLook = m_look;
        m_hasLook = true;
        if (m_reboundPhase == ReboundPhase::None)
        {
            m_look = head;
            return m_look;
        }

        if (m_reboundPhase == ReboundPhase::Returning)
        {
            ++m_reboundReturnFrame;
            const float t = static_cast<float>(m_reboundReturnFrame) / static_cast<float>(m_reboundReturnFrames);
            if (t >= 1.0f)
            {
                m_look = head;
            }
            else
            {
                m_look = head + m_reboundReturnOffset * (1.0f - NS::SmoothStep(t));
            }
            return m_look;
        }

        if (m_reboundPhase == ReboundPhase::Launching)
        {
            // 横と前後は臨界減衰のバネで追う。バネの値は切らず、見せる遅れだけを 上限 × tanh(遅れ ÷ 上限) で丸める
            // 届いたフレームに切ると、カメラの速さが急に突進と同じになって引っかかったように見える
            // 高さは追う相手の頭のまま
            CriticalSpringStep(m_reboundAnchor.x, m_reboundAnchorVelocity.x, head.x, m_forcedLaunchFollowOmega, dt);
            CriticalSpringStep(m_reboundAnchor.z, m_reboundAnchorVelocity.z, head.z, m_forcedLaunchFollowOmega, dt);
            const float lagX = m_reboundAnchor.x - head.x;
            const float lagZ = m_reboundAnchor.z - head.z;
            const float lag = std::sqrt(lagX * lagX + lagZ * lagZ);
            const float maxLag = std::max(m_forcedLaunchMaxLag, 0.0f);
            m_look = head;
            if (lag > 0.0f && maxLag > 0.0f)
            {
                const float shown = maxLag * std::tanh(lag / maxLag);
                m_look.x = head.x + lagX * (shown / lag);
                m_look.z = head.z + lagZ * (shown / lag);
            }
            return m_look;
        }

        // 横と前後は臨界減衰のバネで遅れて付いていき、遅れは上限の内に留める
        // 高さは留めたまま
        CriticalSpringStep(m_reboundAnchor.x, m_reboundAnchorVelocity.x, head.x, m_reboundFollowOmega, dt);
        CriticalSpringStep(m_reboundAnchor.z, m_reboundAnchorVelocity.z, head.z, m_reboundFollowOmega, dt);
        const float lagX = m_reboundAnchor.x - head.x;
        const float lagZ = m_reboundAnchor.z - head.z;
        const float lag = std::sqrt(lagX * lagX + lagZ * lagZ);
        const float maxLag = std::max(m_reboundLagCap, 0.0f);
        if (lag > maxLag)
        {
            const float scale = maxLag / lag;
            m_reboundAnchor.x = head.x + lagX * scale;
            m_reboundAnchor.z = head.z + lagZ * scale;
        }
        // 上限は欄の値より上にある間だけ、今の遅れまで縮めていく。増やさないので注視点は跳ばない
        m_reboundLagCap = std::max(m_reboundMaxLag, std::min(m_reboundLagCap, std::min(lag, maxLag)));
        m_look = m_reboundAnchor + m_partnerLean;

        // 追う相手が画面の上下の帯を越えそうな時だけ、
        // 越えない所まで位置と注視点をカメラの上の向きへ動かす
        // 動かした量は留めた注視点へ戻さない。帯の内へ戻れば留めた高さへ戻る
        const float viewFov = FovY().value - NS::DegreesToRadians(m_chargeNarrowDegrees);
        if (m_reboundScreenBand > 0.0f && viewFov > 0.0f)
        {
            const ViewAxes axes = ViewAxesFromYawPitch(m_yaw, m_pitch);
            const NS::Vector3 toBall = ball - (m_look - axes.forward * m_distance);
            const float depth = NS::Dot(toBall, axes.forward);
            if (depth > 0.0f)
            {
                const float limit = m_reboundScreenBand * depth * std::tan(viewFov * 0.5f);
                const float height = NS::Dot(toBall, axes.up);
                if (height > limit)
                {
                    m_look += axes.up * (height - limit);
                }
                else if (height < -limit)
                {
                    m_look += axes.up * (height + limit);
                }
            }
        }
        return m_look;
    }

    void ThirdPersonFollow::UpdateCharge(const FollowChargeDesc& charge,
                                         bool framingHeld,
                                         const Transform& target,
                                         const NS::Vector3& look,
                                         float dt) noexcept
    {
        // 溜め量は放した後も残るので、押していないフレームは 0 として読む
        float holdCharge = 0.0f;
        if (charge.held)
        {
            holdCharge = charge.charge01;
        }
        // 寄り (締めと真ん中への下向き) は溜めの量の指数乗で効かせる。溜めの終わりにかけて速まり、もうすぐ弾ける事を
        // 見せる。溜めの揺れのトラウマも 2 乗で効くので、揺れと寄りが一緒に高まる。指数が正でなければ比例にする
        float approachExponent = 1.0f;
        if (NS::IsPositiveFinite(m_chargeApproachExponent))
        {
            approachExponent = m_chargeApproachExponent;
        }
        const float approach = std::pow(holdCharge, approachExponent);
        const float holdNarrow = m_chargeNarrowMaxDegrees * approach;

        // 押している間の締めが下がったフレームを戻しの始まりにする。放したフレームがこれに当たる
        // 体当たりから止めの明けまでは戻しの 0 フレーム目に留め、明けたフレームを 1 フレーム目にする
        if (holdNarrow < m_chargeHoldNarrowDegrees)
        {
            m_chargeReturnFromDegrees = m_chargeNarrowDegrees;
            m_chargeReturnFrame = 1;
            m_chargeReturnWaiting = framingHeld;
        }
        else if (m_chargeReturnWaiting)
        {
            m_chargeReturnWaiting = framingHeld;
        }
        else if (m_chargeReturnFrame > 0)
        {
            ++m_chargeReturnFrame;
        }
        m_chargeHoldNarrowDegrees = holdNarrow;

        float returning = 0.0f;
        if (m_chargeReturnWaiting)
        {
            returning = m_chargeReturnFromDegrees;
        }
        else if (m_chargeReturnFrame > 0)
        {
            if (m_chargeReturnFrame >= m_chargeNarrowReturnFrames)
            {
                m_chargeReturnFrame = 0;
            }
            else
            {
                const float t =
                    static_cast<float>(m_chargeReturnFrame) / static_cast<float>(m_chargeNarrowReturnFrames);
                returning = m_chargeReturnFromDegrees * (1.0f - NS::SmoothStep(t));
            }
        }
        m_chargeNarrowDegrees = std::max(holdNarrow, returning);

        // 溜めの揺れはカメラの管理役のトラウマに保たせる。揺れの持ち主を当たりの揺れと 1 つにする
        // 締めと同じ溜め量から作り、変わり始めのフレームを締めと揃える。トラウマの 2 乗で、溜めきりに近いほど強まる
        if (holdCharge > 0.0f && Owner() != nullptr)
        {
            NS::Obj::CameraTraumaShape shape;
            shape.yawDegrees = m_chargeShakeYawDegrees;
            shape.pitchDegrees = m_chargeShakePitchDegrees;
            shape.frequency = m_chargeShakeFrequency;
            shape.decayPerSecond = m_chargeShakeDecayPerSecond;
            (void)HoldCameraTrauma(*Owner(), holdCharge, shape);
        }

        // 構図は押したフレームから動かし、溜めに入った時には相手を枠へ入れておく
        NS::Vector2 wanted{0.0f, 0.0f};
        float wantedTilt = 0.0f;
        const float frameFov = FovY().value - NS::DegreesToRadians(m_chargeNarrowDegrees);
        const bool framing = charge.held && charge.hasAimTarget && m_chargeFrameRatio > 0.0f && frameFov > 0.0f;
        if (framing)
        {
            const ViewAxes axes = ViewAxesFromYawPitch(m_yaw, m_pitch);
            const NS::Vector3 root = target.Position();
            const NS::Vector3 camPos = look - axes.forward * m_distance;
            const float frameTanHalf = m_chargeFrameRatio * std::tan(frameFov * 0.5f);

            const FramePoint self = ToFramePoint(root, camPos, axes, frameTanHalf);
            const FramePoint aim = ToFramePoint(charge.aimTargetCenter, camPos, axes, frameTanHalf);
            // 相手は中心でなく中心 ± 半径を枠に入れる
            // 締めた視野では玉が大きく写り、中心だけ入れると縁が画面の端で切れる
            const float aimHalfWidth = std::max(aim.halfWidth - charge.aimTargetRadius, 0.0f);
            const float aimHalfHeight = std::max(aim.halfHeight - charge.aimTargetRadius, 0.0f);
            wanted.x = FrameAxisShift(self.usable, self.right, self.halfWidth, aim.usable, aim.right, aimHalfWidth);
            wanted.y = FrameAxisShift(self.usable, self.up, self.halfHeight, aim.usable, aim.up, aimHalfHeight);
            // 縦は自機と相手の真ん中を画面の中心へ寄せる。注視点は頭の高さなので、枠に入れるだけだと二人は画面の
            // 下の方に写り、当たりの揺れが小さく見える。寄せは注視点だけを下げて下を向かせ、カメラの位置は下げない。
            // 位置を下げると見下ろす角が浅くなり、奥の相手が自機の真後ろに重なる
            // 寄せる量は締めと同じ寄りの量で増やし、溜めるにつれて速まりながら下を向く。相手を見付けたフレームに
            // 一気に動かさない
            if (self.usable && aim.usable)
            {
                const NS::Vector3 middle = (root + charge.aimTargetCenter) * 0.5f;
                const NS::Vector3 toMiddle = middle - (camPos + axes.up * wanted.y);
                const float depth = NS::Dot(toMiddle, axes.forward);
                if (depth > 0.0f)
                {
                    // 注視点を上の向きに s 下げると、視線は s ÷ 距離 の傾きだけ下を向く。真ん中の傾きと等しくする
                    const float centerRatio = NS::Clamp(m_chargeCenterRatio, 0.0f, 1.0f);
                    wantedTilt = NS::Dot(toMiddle, axes.up) / depth * m_distance * centerRatio * approach;
                }
            }
        }

        // 体当たりから止めの明けまでは構図のずらしも動かさない。当たる瞬間に二人が画面の上で滑らない
        if (framingHeld && !charge.held)
        {
            m_chargeFrameVelocity = NS::Vector2{0.0f, 0.0f};
            return;
        }
        CriticalSpringStep(m_chargeFrameOffset.x, m_chargeFrameVelocity.x, wanted.x, m_chargeFrameOmega, dt);
        CriticalSpringStep(m_chargeFrameOffset.y, m_chargeFrameVelocity.y, wanted.y, m_chargeFrameOmega, dt);
        CriticalSpringStep(m_chargeCenterTilt, m_chargeCenterTiltVelocity, wantedTilt, m_chargeFrameOmega, dt);
        // 0 へは限りなく近づくだけなので、k_ChargeFrameSnap を切ったら 0 にして溜めを受けていない時の式へ戻す
        if (!framing && m_chargeFrameOffset.Length() < k_ChargeFrameSnap)
        {
            m_chargeFrameOffset = NS::Vector2{0.0f, 0.0f};
            m_chargeFrameVelocity = NS::Vector2{0.0f, 0.0f};
        }
        if (!framing && std::abs(m_chargeCenterTilt) < k_ChargeFrameSnap)
        {
            m_chargeCenterTilt = 0.0f;
            m_chargeCenterTiltVelocity = 0.0f;
        }
    }

    void ThirdPersonFollow::OnStart()
    {
        // 基底が brain へ自分を登録する
        VirtualCamera::OnStart();

        // プレイ開始 / rebuild ごとに初期姿勢へ戻す。editor で置いた向きからプレイを始め、手動回転はここから積む
        m_yaw = m_initialYaw;
        m_pitch = m_initialPitch;
        if (!m_manualDistance)
        {
            m_distance = m_idleDistance;
            m_desiredDistance = m_idleDistance;
        }
        ClearCharge();
        ClearRebound();
    }

    void ThirdPersonFollow::SetSensX(float radPerPixel) noexcept
    {
        m_sensX = radPerPixel;
    }
    void ThirdPersonFollow::SetSensY(float radPerPixel) noexcept
    {
        m_sensY = radPerPixel;
    }
    void ThirdPersonFollow::SetInvertX(bool invert) noexcept
    {
        m_invertX = invert;
    }
    void ThirdPersonFollow::SetInvertY(bool invert) noexcept
    {
        m_invertY = invert;
    }

    void ThirdPersonFollow::SetAutoDistances(float idle, float run, float jump) noexcept
    {
        if (idle > 0.0f)
        {
            m_idleDistance = idle;
        }
        if (run > 0.0f)
        {
            m_runDistance = run;
        }
        if (jump > 0.0f)
        {
            m_jumpDistance = jump;
        }
    }

    void ThirdPersonFollow::SetRunSpeedThreshold(float speed) noexcept
    {
        m_runSpeedThreshold = speed;
    }

    void ThirdPersonFollow::SetDistance(float distance) noexcept
    {
        m_distance = distance;
        m_desiredDistance = distance;
        m_manualDistance = true;
    }

    void ThirdPersonFollow::ClearManualDistance() noexcept
    {
        m_manualDistance = false;
    }

    void ThirdPersonFollow::SetInitialPoseFromCameraPosition(const NS::Vector3& cameraPosition) noexcept
    {
        const Transform* target = Target();
        if (target == nullptr)
        {
            return;
        }

        const NS::Vector3 tgtPos = target->Position();
        const NS::Vector3 headPos{tgtPos.x, tgtPos.y + m_headHeight, tgtPos.z};
        const NS::Vector3 toHead = headPos - cameraPosition; // = forward * distance
        const float distance = toHead.Length();
        if (distance < 1e-3f)
        {
            return;
        }
        const NS::Vector3 forward = toHead * (1.0f / distance);

        // EvaluatePose の forward = (sin(yaw)cos(pitch), sin(pitch), cos(yaw)cos(pitch)) を解く
        // pitch は仰角の可動域に収める。clamp した分だけギズモ位置と厳密には一致しないが範囲外へは向けない
        const float pitch = NS::Clamp(std::asin(NS::Clamp(forward.y, -1.0f, 1.0f)), m_pitchMin, m_pitchMax);
        const float yaw = std::atan2(forward.x, forward.z);

        m_initialYaw = yaw;
        m_initialPitch = pitch;
        m_idleDistance = distance;

        // 編集中は OnUpdate が走らないので現在値も直接書き、EvaluatePose 表示をその場で追従させる
        m_yaw = yaw;
        m_pitch = pitch;
        m_distance = distance;
        m_desiredDistance = distance;
    }

    void ThirdPersonFollow::OnUpdate()
    {
        // 受けた溜めと反動の状態はこのフレームだけ使う。渡されなかったフレームは押していない・反動でないのと同じ
        const FollowChargeDesc charge = m_charge;
        m_charge = FollowChargeDesc{};
        const FollowReboundDesc rebound = m_rebound;
        m_rebound = FollowReboundDesc{};
        const bool framingHeld = m_framingHeld;
        m_framingHeld = false;

        const float dt = NS::OS::FrameTimer::FixedDelta();
        const Transform* target = Target();
        if (!IsActive() || target == nullptr || dt <= 0.0f)
        {
            // 休止の間は注視点を控えないので、反動の間の追い方も捨てる
            // 再開した時に古い注視点から留めない
            ClearRebound();
            return;
        }

        // 反動の状態になったフレーム
        // 空中で続けて当てた時も、間に反動でないフレームを挟むのでここを通る
        const bool reboundBegan = rebound.rebounding && !m_wasRebounding;
        m_wasRebounding = rebound.rebounding;
        const bool launchBegan = rebound.forcedSlamming && !m_wasForcedSlamming;
        m_wasForcedSlamming = rebound.forcedSlamming;
        UpdateReboundTurn(reboundBegan, rebound);

        // マウスと右スティックの手動回転。反動になってから接地するまでは受けない
        if (!m_reboundLookHeld)
        {
            NS::OS::Input& input = NS::OS::Input::Get();
            const NS::OS::Mouse& mouse = input.Mouse();
            float mxSign = 1.0f;
            if (m_invertX)
            {
                mxSign = -1.0f;
            }
            float mySign = 1.0f;
            if (m_invertY)
            {
                mySign = -1.0f;
            }
            m_yaw += static_cast<float>(mouse.GetDeltaX()) * m_sensX * mxSign;
            m_pitch += static_cast<float>(mouse.GetDeltaY()) * m_sensY * mySign;

            const NS::OS::Gamepad& pad = input.Gamepad();
            const NS::OS::Stick rstick = pad.RightStick();
            m_yaw += rstick.x * m_stickSensX * dt * mxSign;
            m_pitch += rstick.y * m_stickSensY * dt * mySign;
        }

        m_pitch = NS::Clamp(m_pitch, m_pitchMin, m_pitchMax);

        const NS::Vector3 root = target->Position();
        const NS::Vector3 head{root.x, root.y + m_targetHeightOffset + m_headHeight, root.z};
        UpdateReboundPhase(reboundBegan, rebound.rebounding, launchBegan, rebound.forcedSlamming, head);
        // 反動になったフレームは前の反動の寄せと引きを持ち越さない
        if (reboundBegan)
        {
            m_partnerLean = NS::Vector3{0.0f, 0.0f, 0.0f};
            m_partnerLeanVelocity = NS::Vector3{0.0f, 0.0f, 0.0f};
            m_partnerPull = 0.0f;
            m_partnerPullVelocity = 0.0f;
        }
        UpdateReboundPartner(rebound, head, dt);

        // 接地と速度で決める自動ズーム距離
        // 反動になったフレームに、目標を当たった瞬間の距離にする。下げる反動なら欄の分だけ伸ばし、
        // カメラを後ろへ下げる。反動の間は書き換えない
        // 今の目標でなく今の距離から測る
        // 目標へ寄っている途中に目標へ足すと、見えている距離より下がりすぎるか寄る
        if (!m_manualDistance)
        {
            if (reboundBegan)
            {
                m_reboundBaseDistance = m_distance;
                if (rebound.pullBack)
                {
                    m_reboundBaseDistance += std::max(m_reboundPullBack, 0.0f);
                }
                m_desiredDistance = m_reboundBaseDistance + m_partnerPull;
            }
            else if (m_reboundPhase == ReboundPhase::Following)
            {
                // 反動の間は当たった瞬間に決めた距離に、相手を収める引きだけを足す
                m_desiredDistance = m_reboundBaseDistance + m_partnerPull;
            }
            else if (framingHeld)
            {
                // 体当たりから止めの明けまでは今の距離のまま。突進で浮いても空中の距離へ引かない
                m_desiredDistance = m_distance;
            }
            else if (m_hasFollowMotion && !m_followGrounded)
            {
                m_desiredDistance = m_jumpDistance;
            }
            else if (m_hasFollowMotion && m_followHorizontalSpeed > m_runSpeedThreshold)
            {
                m_desiredDistance = m_runDistance;
            }
            else
            {
                m_desiredDistance = m_idleDistance;
            }
        }
        m_distance = SpringApproach(m_distance, m_desiredDistance, m_springOmega, dt);

        const NS::Vector3 look = UpdateReboundLook(head, root, dt);
        UpdateCharge(charge, framingHeld, *target, look, dt);
    }

    CameraPose ThirdPersonFollow::EvaluatePose(float alpha) const noexcept
    {
        const Transform* target = Target();
        if (target == nullptr)
        {
            return MakePose(
                NS::Vector3{0.0f, 0.0f, -5.0f}, NS::Vector3{0.0f, 0.0f, 0.0f}, NS::Vector3{0.0f, 1.0f, 0.0f});
        }

        const ViewAxes axes = ViewAxesFromYawPitch(m_yaw, m_pitch);

        // Player Mesh の補間と整合させ、相対位置のガタつきを防ぐ
        const NS::Vector3 tgtPos = target->InterpolatedWorldMatrix(alpha).Translation();
        // ずれは補間しない。根を ShiftPosition で上げ下げしていれば、補間の途中でも 根 + ずれ は動かない
        NS::Vector3 headPos{tgtPos.x, tgtPos.y + m_targetHeightOffset + m_headHeight, tgtPos.z};
        // 反動の間と寄せ戻しの間の注視点は固定ステップごとに決めるので、
        // 前のフレームと今のフレームの間を補間する
        // 1 − alpha と alpha で重み付けし、alpha が 1 なら今のフレームの注視点そのものになる
        if (m_reboundPhase != ReboundPhase::None)
        {
            headPos = m_previousLook * (1.0f - alpha) + m_look * alpha;
        }
        NS::Vector3 camPos = headPos - axes.forward * m_distance;

        // 構図のずらしは位置と注視点を同じだけ動かし、視線の向きを変えない。0 なら足さない
        const float upShift = m_chargeFrameOffset.y;
        if (m_chargeFrameOffset.x != 0.0f || upShift != 0.0f)
        {
            const NS::Vector3 shift = axes.right * m_chargeFrameOffset.x + axes.up * upShift;
            camPos += shift;
            headPos += shift;
        }
        // 真ん中への寄せは注視点だけを下げ、カメラの位置は変えずに下を向かせる
        if (m_chargeCenterTilt != 0.0f)
        {
            headPos += axes.up * m_chargeCenterTilt;
        }

        CameraPose pose = MakePose(camPos, headPos, NS::Vector3{0.0f, 1.0f, 0.0f});
        if (m_chargeNarrowDegrees != 0.0f)
        {
            pose.fovY = NS::Radians{pose.fovY.value - NS::DegreesToRadians(m_chargeNarrowDegrees)};
        }
        return pose;
    }

    NS_CLASS(ThirdPersonFollow)
} // namespace NS::Obj
