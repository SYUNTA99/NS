#include "Runtime/Object/Components/CameraBrain.h"

#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/Components/VirtualCamera.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Platform/Clock.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace NS::Obj
{
    namespace
    {
        // 滑らかに始まって終わる補間の重み。t が 0 で 0、1 で 1 ちょうど
        [[nodiscard]] float SmoothStep(float t) noexcept
        {
            return t * t * (3.0f - 2.0f * t);
        }

        [[nodiscard]] bool IsFiniteVector(const NS::Core::Vector3& v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        // 水平の前から作った右と direction の内積が負なら -1、それ以外は 1
        [[nodiscard]] float SideSign(const NS::Core::Vector3& forwardHorizontal,
                                     const NS::Core::Vector3& direction) noexcept
        {
            const NS::Core::Vector3 right{forwardHorizontal.z, 0.0f, -forwardHorizontal.x};
            if (NS::Core::Dot(right, direction) < 0.0f)
            {
                return -1.0f;
            }
            return 1.0f;
        }

        // 姿勢の視線の水平から右、視線と右から上を作る。どちらも長さ 1
        void ViewAxes(const CameraPose& pose, NS::Core::Vector3& outRight, NS::Core::Vector3& outUp) noexcept
        {
            const NS::Core::Vector3 look = pose.target - pose.position;
            NS::Core::Vector3 forward{};
            if (!NS::Core::TryNormalizeHorizontal(look, forward))
            {
                forward = NS::Core::Vector3{0.0f, 0.0f, 1.0f};
            }
            outRight = NS::Core::Vector3{forward.z, 0.0f, -forward.x};

            NS::Core::Vector3 up = NS::Core::Cross(look, outRight);
            if (up.LengthSquared() < NS::Core::k_Epsilon * NS::Core::k_Epsilon)
            {
                up = NS::Core::Vector3{0.0f, 1.0f, 0.0f};
            }
            up.Normalize();
            outUp = up;
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
                           float NS::Core::Vector2::* axis,
                           std::vector<NS::Core::Vector2>& offsets) noexcept
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

    // vcam を供給する追従カメラが LateUpdate + 50 なので、選び直しはその後ろに置く
    CameraBrain::CameraBrain() noexcept : Component(TickPriority::LateUpdate + 60) {}

    void CameraBrain::OnStart()
    {
        if (Owner() != nullptr)
        {
            m_camera = Owner()->FindComponent<CameraComponent>();
        }
        else
        {
            m_camera = nullptr;
        }
    }

    void CameraBrain::AddVirtualCamera(VirtualCamera* vcam)
    {
        if (vcam == nullptr)
        {
            return;
        }
        if (std::find(m_vcams.begin(), m_vcams.end(), vcam) != m_vcams.end())
        {
            return;
        }
        m_vcams.push_back(vcam);
    }

    void CameraBrain::RemoveVirtualCamera(VirtualCamera* vcam) noexcept
    {
        if (vcam == nullptr)
        {
            return;
        }
        m_vcams.erase(std::remove(m_vcams.begin(), m_vcams.end(), vcam), m_vcams.end());
        if (m_active == vcam)
        {
            m_active = nullptr; // 次の OnUpdate / Evaluate で選び直す
        }
    }

    void CameraBrain::SetBlendDuration(float seconds) noexcept
    {
        if (seconds > 0.0f)
        {
            m_blendDuration = seconds;
        }
        else
        {
            m_blendDuration = 0.0f;
        }
    }

    bool CameraBrain::StartShake(const CameraShakeDesc& desc) noexcept
    {
        // 壊れた値が pose へ流れると視点が消える。入口で捨てる
        const bool finite = std::isfinite(desc.sideAmplitude) && std::isfinite(desc.upAmplitude) &&
                            IsFiniteVector(desc.firstSideDirection);
        if (!finite || desc.sideAmplitude < 0.0f || desc.upAmplitude < 0.0f || desc.frames <= 0 ||
            desc.longestFlipFrames < 1)
        {
            return false;
        }

        // seed_seq と mt19937 は手順が規格で決まっているので、同じ種なら実装によらず同じ並びになる
        std::seed_seq seeds{desc.seed};
        std::mt19937 generator(seeds);
        m_shakeOffsets.assign(static_cast<std::size_t>(desc.frames), NS::Core::Vector2{0.0f, 0.0f});
        FillFlipSigns(generator,
                      desc.longestFlipFrames,
                      SideSignOf(desc.firstSideDirection),
                      &NS::Core::Vector2::x,
                      m_shakeOffsets);
        FillFlipSigns(generator, desc.longestFlipFrames, -1.0f, &NS::Core::Vector2::y, m_shakeOffsets);

        // 始めたフレームが最大で、残りのフレーム数に比例して減る
        for (int frame = 0; frame < desc.frames; ++frame)
        {
            const float decay = static_cast<float>(desc.frames - frame) / static_cast<float>(desc.frames);
            NS::Core::Vector2& offset = m_shakeOffsets[static_cast<std::size_t>(frame)];
            offset.x *= desc.sideAmplitude * decay;
            offset.y *= desc.upAmplitude * decay;
        }
        m_shakeFrame = 0;
        m_shakeStartedThisFrame = true;
        return true;
    }

    NS::Core::Vector2 CameraBrain::ShakeOffset() const noexcept
    {
        if (m_shakeFrame < static_cast<int>(m_shakeOffsets.size()))
        {
            return m_shakeOffsets[static_cast<std::size_t>(m_shakeFrame)];
        }
        return NS::Core::Vector2{0.0f, 0.0f};
    }

    bool CameraBrain::StartZoomRoll(const CameraZoomRollDesc& desc) noexcept
    {
        const bool finite =
            std::isfinite(desc.zoom) && std::isfinite(desc.rollDegrees) && IsFiniteVector(desc.rollDirection);
        if (!finite || desc.zoom < 1.0f || desc.holdFrames < 0 || desc.returnFrames < 0)
        {
            return false;
        }

        m_zoomRollFull = CameraZoomRoll{
            .zoom = desc.zoom,
            .rollDegrees = desc.rollDegrees * SideSignOf(desc.rollDirection),
        };
        m_zoomRollHoldFrames = desc.holdFrames;
        m_zoomRollReturnFrames = desc.returnFrames;
        m_zoomRollFrame = 0;
        m_zoomRollStartedThisFrame = true;
        return true;
    }

    CameraZoomRoll CameraBrain::ZoomRoll() const noexcept
    {
        if (m_zoomRollFrame >= m_zoomRollHoldFrames + m_zoomRollReturnFrames)
        {
            return CameraZoomRoll{};
        }
        if (m_zoomRollFrame < m_zoomRollHoldFrames)
        {
            return m_zoomRollFull;
        }

        // 重みを 1 - weight と weight に分けて掛けるので、戻しの最後のフレームで倍率 1・傾き 0 ちょうどになる
        const float t =
            static_cast<float>(m_zoomRollFrame - m_zoomRollHoldFrames + 1) / static_cast<float>(m_zoomRollReturnFrames);
        const float weight = SmoothStep(t);
        return CameraZoomRoll{
            .zoom = m_zoomRollFull.zoom * (1.0f - weight) + weight,
            .rollDegrees = m_zoomRollFull.rollDegrees * (1.0f - weight),
        };
    }

    void CameraBrain::StopShakeAndZoomRoll() noexcept
    {
        m_shakeOffsets.clear();
        m_shakeFrame = 0;
        m_shakeStartedThisFrame = false;

        m_zoomRollFull = CameraZoomRoll{};
        m_zoomRollHoldFrames = 0;
        m_zoomRollReturnFrames = 0;
        m_zoomRollFrame = 0;
        m_zoomRollStartedThisFrame = false;
    }

    float CameraBrain::SideSignOf(const NS::Core::Vector3& direction) const noexcept
    {
        return SideSign(ForwardHorizontal(), direction);
    }

    void CameraBrain::BeginBlendFrom(const CameraPose& pose) noexcept
    {
        if (m_blendDuration <= 0.0f)
        {
            return;
        }

        m_lastPose = pose;
        m_blendFrom = pose;
        m_blendElapsed = 0.0f;
        m_blending = true;
    }

    VirtualCamera* CameraBrain::SelectActive() const noexcept
    {
        VirtualCamera* best = nullptr;
        for (VirtualCamera* vcam : m_vcams)
        {
            if (vcam == nullptr || !vcam->IsActive())
            {
                continue;
            }
            if (best == nullptr || vcam->VcamPriority() > best->VcamPriority())
            {
                best = vcam;
            }
        }
        return best;
    }

    std::optional<CameraPose> CameraBrain::EvaluateTopPose(float alpha) const noexcept
    {
        VirtualCamera* best = nullptr;
        for (VirtualCamera* vcam : m_vcams)
        {
            if (vcam == nullptr)
            {
                continue;
            }
            if (best == nullptr || vcam->VcamPriority() > best->VcamPriority())
            {
                best = vcam;
            }
        }
        if (best == nullptr)
        {
            return std::nullopt;
        }
        return best->EvaluatePose(alpha);
    }

    void CameraBrain::OnUpdate()
    {
        VirtualCamera* next = SelectActive();
        if (next != m_active)
        {
            // 直前まで写していた pose から新 vcam へ繋ぐ。旧 pose が無い初回 active 化はカットする
            if (m_active != nullptr && m_blendDuration > 0.0f)
            {
                m_blendFrom = m_lastPose;
                m_blendElapsed = 0.0f;
                m_blending = true;
            }
            m_active = next;
        }

        if (m_blending)
        {
            m_blendElapsed += NS::Platform::FrameTimer::FixedDelta();
            if (m_blendElapsed >= m_blendDuration)
            {
                m_blending = false;
            }
        }

        // 始めた後の最初の OnUpdate では進めない。始めたフレームに最初の振れと寄りを描く
        if (m_shakeStartedThisFrame)
        {
            m_shakeStartedThisFrame = false;
        }
        else if (m_shakeFrame < static_cast<int>(m_shakeOffsets.size()))
        {
            ++m_shakeFrame;
        }

        if (m_zoomRollStartedThisFrame)
        {
            m_zoomRollStartedThisFrame = false;
        }
        else if (m_zoomRollFrame < m_zoomRollHoldFrames + m_zoomRollReturnFrames)
        {
            ++m_zoomRollFrame;
        }
    }

    std::optional<CameraPose> CameraBrain::ComposePose(float alpha) const noexcept
    {
        VirtualCamera* active = m_active;
        if (active == nullptr || !active->IsActive())
        {
            active = SelectActive();
        }
        if (active == nullptr)
        {
            return std::nullopt;
        }

        CameraPose pose = active->EvaluatePose(alpha);
        if (m_blending && m_blendDuration > 0.0f)
        {
            const float t = NS::Core::Clamp(m_blendElapsed / m_blendDuration, 0.0f, 1.0f);
            pose = CameraPose::Lerp(m_blendFrom, pose, SmoothStep(t));
        }

        NS::Core::Vector3 right{};
        NS::Core::Vector3 up{};
        ViewAxes(pose, right, up);

        // 揺れはブレンドの後に足す。どの vcam が選ばれていてもブレンド中でも一様に掛かる
        // position と target を同じだけ動かす平行移動なので視線方向が回らず、
        // ForwardHorizontal を基準にする camera 相対入力に波及しない
        const NS::Core::Vector2 shake = ShakeOffset();
        if (shake.x != 0.0f || shake.y != 0.0f)
        {
            const NS::Core::Vector3 offset = right * shake.x + up * shake.y;
            pose.position += offset;
            pose.target += offset;
        }

        // 寄りは視野角、傾きは視線の軸まわりの上の向きで掛ける。どちらも注視点 - 位置を変えない
        const CameraZoomRoll zoomRoll = ZoomRoll();
        if (zoomRoll.zoom != 1.0f)
        {
            pose.fovY = NS::Core::Radians{2.0f * std::atan(std::tan(pose.fovY.value * 0.5f) / zoomRoll.zoom)};
        }
        if (zoomRoll.rollDegrees != 0.0f)
        {
            const float roll = NS::Core::DegreesToRadians(zoomRoll.rollDegrees);
            pose.up = up * std::cos(roll) + right * std::sin(roll);
        }
        return pose;
    }

    void CameraBrain::Evaluate(float alpha) noexcept
    {
        // 非 active になった vcam の pose は書かない。編集モードのように OnUpdate が回らない間も選び直す
        if (m_active == nullptr || !m_active->IsActive())
        {
            m_active = SelectActive();
        }
        if (m_active == nullptr || m_camera == nullptr)
        {
            return;
        }

        const std::optional<CameraPose> pose = ComposePose(alpha);
        if (!pose.has_value())
        {
            return;
        }
        m_lastPose = *pose;
        m_camera->ApplyPose(*pose);
    }

    NS::Core::Matrix CameraBrain::ViewProjection() const noexcept
    {
        if (m_camera != nullptr)
        {
            return m_camera->ViewProjection();
        }
        return NS::Core::Matrix::Identity;
    }

    NS::Core::Vector3 CameraBrain::ForwardHorizontal() const noexcept
    {
        if (m_camera != nullptr)
        {
            return m_camera->ForwardHorizontal();
        }
        return NS::Core::Vector3{0.0f, 0.0f, 1.0f};
    }

    NS_CLASS(CameraBrain)
} // namespace NS::Obj
