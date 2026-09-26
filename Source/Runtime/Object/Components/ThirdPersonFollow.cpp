#include "Runtime/Object/Components/ThirdPersonFollow.h"

#include "Runtime/Platform/Clock.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Platform/Gamepad.h"
#include "Runtime/Platform/Input.h"
#include "Runtime/Platform/Mouse.h"

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

    // 滑らかに始まって終わる補間の重み。t が 0 で 0、1 で 1 ちょうど
    [[nodiscard]] float SmoothStep(float t) noexcept
    {
        return t * t * (3.0f - 2.0f * t);
    }

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

    // カメラから見た 1 点の、右と上の向きの位置と、枠の半分の幅
    struct FramePoint
    {
        bool usable = false; // カメラの前にあるか。後ろの点は枠の決まりから外す
        float right = 0.0f;
        float up = 0.0f;
        float halfWidth = 0.0f;
        float halfHeight = 0.0f;
    };

    [[nodiscard]] FramePoint ToFramePoint(const NS::Core::Vector3& point,
                                          const NS::Core::Vector3& cameraPosition,
                                          const NS::Core::Vector3& forward,
                                          const NS::Core::Vector3& right,
                                          const NS::Core::Vector3& up,
                                          float frameTanHalf) noexcept
    {
        const NS::Core::Vector3 offset = point - cameraPosition;
        const float depth = NS::Core::Dot(offset, forward);
        FramePoint framePoint{};
        if (depth <= 0.0f)
        {
            return framePoint;
        }
        framePoint.usable = true;
        framePoint.right = NS::Core::Dot(offset, right);
        framePoint.up = NS::Core::Dot(offset, up);
        framePoint.halfHeight = depth * frameTanHalf;
        framePoint.halfWidth = framePoint.halfHeight * k_ChargeFrameAspect;
        return framePoint;
    }

    // 1 つの軸で、自機と相手を枠の内に入れるずらし
    // 相手を入れる範囲のうち 0 に一番近い値を、自機を入れる範囲へ丸める。両方は入らない時は自機を枠に残す
    [[nodiscard]] float FrameAxisShift(bool selfUsable,
                                       float self,
                                       float selfHalf,
                                       bool targetUsable,
                                       float target,
                                       float targetHalf) noexcept
    {
        float shift = 0.0f;
        if (targetUsable)
        {
            shift = NS::Core::Clamp(0.0f, target - targetHalf, target + targetHalf);
        }
        if (selfUsable)
        {
            shift = NS::Core::Clamp(shift, self - selfHalf, self + selfHalf);
        }
        return shift;
    }
} // namespace

namespace NS::Obj
{

    ThirdPersonFollow::ThirdPersonFollow() noexcept
        : VirtualCamera(NS::Obj::TickPriority::LateUpdate + 50)
    {
        // 生成直後は非 active でプレイ突入時に有効化される。編集中は free-fly が active のまま
        SetActive(false);
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
        GameObject* target = Owner()->OwningScene()->Objects().FindObject(m_targetRef);
        if (target == nullptr)
        {
            return nullptr;
        }
        return &target->Root();
    }

    void ThirdPersonFollow::SetFollowMotion(bool grounded, const NS::Core::Vector3& velocity) noexcept
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
        const NS::Core::Vector3& center = desc.aimTargetCenter;
        const bool finiteCenter = std::isfinite(center.x) && std::isfinite(center.y) && std::isfinite(center.z);
        if (desc.hasAimTarget && !finiteCenter)
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
        m_chargeReturnFromDegrees = 0.0f;
        m_chargeReturnFrame = 0;
        m_chargeShake = 0.0f;
        m_chargeFrameOffset = NS::Core::Vector2{0.0f, 0.0f};
        m_chargeFrameVelocity = NS::Core::Vector2{0.0f, 0.0f};
    }

    void ThirdPersonFollow::UpdateCharge(const FollowChargeDesc& charge, const Transform& target, float dt) noexcept
    {
        // 溜め量は放した後も残るので、押していないフレームは 0 として読む
        float holdCharge = 0.0f;
        if (charge.held)
        {
            holdCharge = charge.charge01;
        }
        const float holdNarrow = m_chargeNarrowMaxDegrees * holdCharge;

        // 押している間の締めが下がったフレームを戻しの 1 フレーム目にする。放したフレームがこれに当たる
        if (holdNarrow < m_chargeHoldNarrowDegrees)
        {
            m_chargeReturnFromDegrees = m_chargeNarrowDegrees;
            m_chargeReturnFrame = 1;
        }
        else if (m_chargeReturnFrame > 0)
        {
            ++m_chargeReturnFrame;
        }
        m_chargeHoldNarrowDegrees = holdNarrow;

        float returning = 0.0f;
        if (m_chargeReturnFrame > 0)
        {
            if (m_chargeReturnFrame >= m_chargeNarrowReturnFrames)
            {
                m_chargeReturnFrame = 0;
            }
            else
            {
                const float t =
                    static_cast<float>(m_chargeReturnFrame) / static_cast<float>(m_chargeNarrowReturnFrames);
                returning = m_chargeReturnFromDegrees * (1.0f - SmoothStep(t));
            }
        }
        m_chargeNarrowDegrees = std::max(holdNarrow, returning);

        // 締めと同じ溜め量から作り、変わり始めと変わり終わりのフレームを締めと揃える
        if (holdCharge > 0.0f)
        {
            float sign = -1.0f;
            if (m_chargeShake < 0.0f)
            {
                sign = 1.0f;
            }
            m_chargeShake = sign * m_chargeShakeStrength * holdCharge;
        }
        else
        {
            m_chargeShake = 0.0f;
        }

        // 構図は押したフレームから動かし、溜めに入った時には相手を枠へ入れておく
        NS::Core::Vector2 wanted{0.0f, 0.0f};
        const float frameFov = FovY().value - NS::Core::DegreesToRadians(m_chargeNarrowDegrees);
        const bool framing = charge.held && charge.hasAimTarget && m_chargeFrameRatio > 0.0f && frameFov > 0.0f;
        if (framing)
        {
            const float cy = std::cos(m_yaw);
            const float sy = std::sin(m_yaw);
            const float cp = std::cos(m_pitch);
            const float sp = std::sin(m_pitch);
            const NS::Core::Vector3 forward{sy * cp, sp, cy * cp};
            const NS::Core::Vector3 right{cy, 0.0f, -sy};
            const NS::Core::Vector3 up{-sp * sy, cp, -sp * cy};
            const NS::Core::Vector3 root = target.Position();
            const NS::Core::Vector3 headPos{root.x, root.y + m_targetHeightOffset + m_headHeight, root.z};
            const NS::Core::Vector3 camPos = headPos - forward * m_distance;
            const float frameTanHalf = m_chargeFrameRatio * std::tan(frameFov * 0.5f);

            const FramePoint self = ToFramePoint(root, camPos, forward, right, up, frameTanHalf);
            const FramePoint aim = ToFramePoint(charge.aimTargetCenter, camPos, forward, right, up, frameTanHalf);
            wanted.x = FrameAxisShift(self.usable, self.right, self.halfWidth, aim.usable, aim.right, aim.halfWidth);
            wanted.y = FrameAxisShift(self.usable, self.up, self.halfHeight, aim.usable, aim.up, aim.halfHeight);
        }

        CriticalSpringStep(m_chargeFrameOffset.x, m_chargeFrameVelocity.x, wanted.x, m_chargeFrameOmega, dt);
        CriticalSpringStep(m_chargeFrameOffset.y, m_chargeFrameVelocity.y, wanted.y, m_chargeFrameOmega, dt);
        // 0 へは限りなく近づくだけなので、k_ChargeFrameSnap を切ったら 0 にして溜めを受けていない時の式へ戻す
        if (!framing && m_chargeFrameOffset.Length() < k_ChargeFrameSnap)
        {
            m_chargeFrameOffset = NS::Core::Vector2{0.0f, 0.0f};
            m_chargeFrameVelocity = NS::Core::Vector2{0.0f, 0.0f};
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

    void ThirdPersonFollow::SetInitialPoseFromCameraPosition(const NS::Core::Vector3& cameraPosition) noexcept
    {
        const Transform* target = Target();
        if (target == nullptr)
        {
            return;
        }

        const NS::Core::Vector3 tgtPos = target->Position();
        const NS::Core::Vector3 headPos{tgtPos.x, tgtPos.y + m_headHeight, tgtPos.z};
        const NS::Core::Vector3 toHead = headPos - cameraPosition; // = forward * distance
        const float distance = toHead.Length();
        if (distance < 1e-3f)
        {
            return;
        }
        const NS::Core::Vector3 forward = toHead * (1.0f / distance);

        // EvaluatePose の forward = (sin(yaw)cos(pitch), sin(pitch), cos(yaw)cos(pitch)) を解く
        // pitch は仰角の可動域に収める。clamp した分だけギズモ位置と厳密には一致しないが範囲外へは向けない
        const float pitch = NS::Core::Clamp(std::asin(NS::Core::Clamp(forward.y, -1.0f, 1.0f)), m_pitchMin, m_pitchMax);
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
        // 受けた溜めはこのフレームだけ使う。渡されなかったフレームは押していないのと同じ
        const FollowChargeDesc charge = m_charge;
        m_charge = FollowChargeDesc{};

        const float dt = NS::Platform::FrameTimer::FixedDelta();
        const Transform* target = Target();
        if (!IsActive() || target == nullptr || dt <= 0.0f)
        {
            return;
        }

        // マウスと右スティックの手動回転
        NS::Platform::Input& input = NS::Platform::Input::Get();
        const NS::Platform::Mouse& mouse = input.Mouse();
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

        const NS::Platform::Gamepad& pad = input.Gamepad(0);
        const NS::Platform::Stick rstick = pad.RightStick();
        m_yaw += rstick.x * m_stickSensX * dt * mxSign;
        m_pitch += rstick.y * m_stickSensY * dt * mySign;

        m_pitch = NS::Core::Clamp(m_pitch, m_pitchMin, m_pitchMax);

        // 接地と速度で決める自動ズーム距離
        if (!m_manualDistance)
        {
            float desired = m_idleDistance;
            if (m_hasFollowMotion)
            {
                if (!m_followGrounded)
                {
                    desired = m_jumpDistance;
                }
                else
                {
                    if (m_followHorizontalSpeed > m_runSpeedThreshold)
                    {
                        desired = m_runDistance;
                    }
                    else
                    {
                        desired = m_idleDistance;
                    }
                }
            }
            m_desiredDistance = desired;
        }
        m_distance = SpringApproach(m_distance, m_desiredDistance, m_springOmega, dt);

        UpdateCharge(charge, *target, dt);
    }

    CameraPose ThirdPersonFollow::EvaluatePose(float alpha) const noexcept
    {
        const Transform* target = Target();
        if (target == nullptr)
        {
            return MakePose(NS::Core::Vector3{0.0f, 0.0f, -5.0f},
                            NS::Core::Vector3{0.0f, 0.0f, 0.0f},
                            NS::Core::Vector3{0.0f, 1.0f, 0.0f});
        }

        const float cy = std::cos(m_yaw);
        const float sy = std::sin(m_yaw);
        const float cp = std::cos(m_pitch);
        const float sp = std::sin(m_pitch);
        const NS::Core::Vector3 forward{sy * cp, sp, cy * cp};

        // Player Mesh の補間と整合させ、相対位置のガタつきを防ぐ
        const NS::Core::Vector3 tgtPos = target->InterpolatedWorldMatrix(alpha).Translation();
        // ずれは補間しない。根を ShiftPosition で上げ下げしていれば、補間の途中でも 根 + ずれ は動かない
        NS::Core::Vector3 headPos{tgtPos.x, tgtPos.y + m_targetHeightOffset + m_headHeight, tgtPos.z};
        NS::Core::Vector3 camPos{
            headPos.x - forward.x * m_distance,
            headPos.y - forward.y * m_distance,
            headPos.z - forward.z * m_distance,
        };

        // 構図のずらしと溜めの揺れは位置と注視点を同じだけ動かし、視線の向きを変えない。どちらも 0 なら足さない
        const float upShift = m_chargeFrameOffset.y + m_chargeShake;
        if (m_chargeFrameOffset.x != 0.0f || upShift != 0.0f)
        {
            const NS::Core::Vector3 right{cy, 0.0f, -sy};
            const NS::Core::Vector3 up{-sp * sy, cp, -sp * cy};
            const NS::Core::Vector3 shift = right * m_chargeFrameOffset.x + up * upShift;
            camPos += shift;
            headPos += shift;
        }

        CameraPose pose = MakePose(camPos, headPos, NS::Core::Vector3{0.0f, 1.0f, 0.0f});
        if (m_chargeNarrowDegrees != 0.0f)
        {
            pose.fovY = NS::Core::Radians{pose.fovY.value - NS::Core::DegreesToRadians(m_chargeNarrowDegrees)};
        }
        return pose;
    }

    NS_CLASS(ThirdPersonFollow)
} // namespace NS::Obj
