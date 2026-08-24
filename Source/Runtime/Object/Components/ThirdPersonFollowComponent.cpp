#include "Runtime/Object/Components/ThirdPersonFollowComponent.h"

#include "Runtime/Core/Clock.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Object/World.h"
#include "Runtime/Platform/Gamepad.h"
#include "Runtime/Platform/Input.h"
#include "Runtime/Platform/Mouse.h"

#include <cmath>

namespace
{
    [[nodiscard]] float SpringApproach(float curr, float target, float omega, float dt) noexcept
    {
        if (omega <= 0.0f || dt <= 0.0f)
            return target;
        const float a = 1.0f - std::exp(-omega * dt);
        return curr + (target - curr) * a;
    }
} // namespace

namespace NS::Object
{

    ThirdPersonFollowComponent::ThirdPersonFollowComponent() noexcept
        : VirtualCameraComponent(NS::Object::TickPriority::LateUpdate + 50)
    {
        // 生成直後は非 active でプレイ突入時に有効化される。 編集中は free-fly が active のまま
        SetActive(false);
        // 追う相手を中心に見るので遠景は要らない。 編集カメラの 5000 と違いプレイ視点は 100 で足りる
        SetFarPlane(100.0f);
    }

    void ThirdPersonFollowComponent::SetTarget(Transform* target) noexcept
    {
        m_target = target;
    }

    void ThirdPersonFollowComponent::SetFollowMotion(bool grounded, const NS::Core::Vector3& velocity) noexcept
    {
        m_followGrounded = grounded;
        const float horiz = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
        // 非有限を通すと距離のばね補間が NaN に染まり、以後カメラが戻らなくなる
        if (std::isfinite(horiz))
            m_followHorizontalSpeed = horiz;
        else
            m_followHorizontalSpeed = 0.0f;
        m_hasFollowMotion = true;
    }

    void ThirdPersonFollowComponent::OnStart()
    {
        // 基底が brain へ自分を登録する
        VirtualCameraComponent::OnStart();

        // プレイ開始 / rebuild ごとに初期姿勢へ戻す。 editor で置いた向きからプレイを始め、 手動回転はここから積む
        m_yaw = m_initialYaw;
        m_pitch = m_initialPitch;
        if (!m_manualDistance)
        {
            m_distance = m_idleDistance;
            m_desiredDistance = m_idleDistance;
        }

        // 参照未設定はテスト / 直結線の構築なので触らない。解決不可も既存の結線を壊さず据え置く
        if (!m_targetRef.IsSet() || Owner() == nullptr || Owner()->OwningScene() == nullptr)
            return;
        GameObject* target = Owner()->OwningScene()->World().FindObject(m_targetRef);
        if (target == nullptr)
            return;
        m_target = &target->Root();
    }

    void ThirdPersonFollowComponent::SetSensX(float radPerPixel) noexcept
    {
        m_sensX = radPerPixel;
    }
    void ThirdPersonFollowComponent::SetSensY(float radPerPixel) noexcept
    {
        m_sensY = radPerPixel;
    }
    void ThirdPersonFollowComponent::SetInvertX(bool invert) noexcept
    {
        m_invertX = invert;
    }
    void ThirdPersonFollowComponent::SetInvertY(bool invert) noexcept
    {
        m_invertY = invert;
    }

    void ThirdPersonFollowComponent::SetAutoDistances(float idle, float run, float jump) noexcept
    {
        if (idle > 0.0f)
            m_idleDistance = idle;
        if (run > 0.0f)
            m_runDistance = run;
        if (jump > 0.0f)
            m_jumpDistance = jump;
    }

    void ThirdPersonFollowComponent::SetRunSpeedThreshold(float speed) noexcept
    {
        m_runSpeedThreshold = speed;
    }

    void ThirdPersonFollowComponent::SetDistance(float distance) noexcept
    {
        m_distance = distance;
        m_desiredDistance = distance;
        m_manualDistance = true;
    }

    void ThirdPersonFollowComponent::ClearManualDistance() noexcept
    {
        m_manualDistance = false;
    }

    void ThirdPersonFollowComponent::SetInitialPoseFromCameraPosition(const NS::Core::Vector3& cameraPosition) noexcept
    {
        if (m_target == nullptr)
            return;
        const NS::Core::Vector3 tgtPos = m_target->Position();
        const NS::Core::Vector3 headPos{tgtPos.x, tgtPos.y + m_headHeight, tgtPos.z};
        const NS::Core::Vector3 toHead = headPos - cameraPosition; // = forward * distance
        const float distance = toHead.Length();
        if (distance < 1e-3f)
            return;
        const NS::Core::Vector3 forward = toHead * (1.0f / distance);

        // EvaluatePose の forward = (sin(yaw)cos(pitch), sin(pitch), cos(yaw)cos(pitch)) を解く
        // pitch は仰角の可動域に収める。 clamp した分だけギズモ位置と厳密には一致しないが範囲外へは向けない
        const float pitch = NS::Core::Clamp(std::asin(NS::Core::Clamp(forward.y, -1.0f, 1.0f)), m_pitchMin, m_pitchMax);
        const float yaw = std::atan2(forward.x, forward.z);

        m_initialYaw = yaw;
        m_initialPitch = pitch;
        m_idleDistance = distance;

        // 編集中は OnUpdate が走らないので現在値も直接書き、 EvaluatePose 表示をその場で追従させる
        m_yaw = yaw;
        m_pitch = pitch;
        m_distance = distance;
        m_desiredDistance = distance;
    }

    void ThirdPersonFollowComponent::OnUpdate()
    {
        const float dt = NS::Core::FrameTimer::FixedDelta();
        if (!IsActive() || m_target == nullptr || dt <= 0.0f)
            return;

        // マウスと右スティックの手動回転
        auto& input = NS::Platform::Input::Get();
        const auto& mouse = input.Mouse();
        float mxSign = 1.0f;
        if (m_invertX)
            mxSign = -1.0f;
        float mySign = 1.0f;
        if (m_invertY)
            mySign = -1.0f;
        m_yaw += static_cast<float>(mouse.GetDeltaX()) * m_sensX * mxSign;
        m_pitch += static_cast<float>(mouse.GetDeltaY()) * m_sensY * mySign;

        const auto& pad = input.Gamepad(0);
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
                        desired = m_runDistance;
                    else
                        desired = m_idleDistance;
                }
            }
            m_desiredDistance = desired;
        }
        m_distance = SpringApproach(m_distance, m_desiredDistance, m_springOmega, dt);
    }

    CameraPose ThirdPersonFollowComponent::EvaluatePose(float alpha) const noexcept
    {
        if (m_target == nullptr)
            return MakePose(NS::Core::Vector3{0.0f, 0.0f, -5.0f},
                            NS::Core::Vector3{0.0f, 0.0f, 0.0f},
                            NS::Core::Vector3{0.0f, 1.0f, 0.0f});

        const float cy = std::cos(m_yaw);
        const float sy = std::sin(m_yaw);
        const float cp = std::cos(m_pitch);
        const float sp = std::sin(m_pitch);
        const NS::Core::Vector3 forward{sy * cp, sp, cy * cp};

        // Player Mesh の補間と整合させ、相対位置のガタつきを防ぐ
        const NS::Core::Vector3 tgtPos = m_target->InterpolatedWorldMatrix(alpha).Translation();
        const NS::Core::Vector3 headPos{tgtPos.x, tgtPos.y + m_headHeight, tgtPos.z};
        const NS::Core::Vector3 camPos{
            headPos.x - forward.x * m_distance,
            headPos.y - forward.y * m_distance,
            headPos.z - forward.z * m_distance,
        };

        return MakePose(camPos, headPos, NS::Core::Vector3{0.0f, 1.0f, 0.0f});
    }

    NS_CLASS(ThirdPersonFollowComponent)
} // namespace NS::Object
