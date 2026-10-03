#include "Runtime/Object/Components/CameraManager.h"

#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/Components/VirtualCamera.h"
#include "Runtime/Platform/Clock.h"

#include <algorithm>
#include <cmath>

namespace NS::Obj
{
    namespace
    {
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
        [[nodiscard]] CameraAxes ViewAxes(const CameraPose& pose) noexcept
        {
            CameraAxes axes{};
            const NS::Core::Vector3 look = pose.target - pose.position;
            NS::Core::Vector3 forward{};
            if (!NS::Core::TryNormalizeHorizontal(look, forward))
            {
                forward = NS::Core::Vector3{0.0f, 0.0f, 1.0f};
            }
            axes.right = NS::Core::Vector3{forward.z, 0.0f, -forward.x};

            NS::Core::Vector3 up = NS::Core::Cross(look, axes.right);
            if (up.LengthSquared() < NS::Core::k_Epsilon * NS::Core::k_Epsilon)
            {
                up = NS::Core::Vector3{0.0f, 1.0f, 0.0f};
            }
            up.Normalize();
            axes.up = up;
            return axes;
        }

    } // namespace

    CameraManager::CameraManager() noexcept = default;

    void CameraManager::AddVirtualCamera(VirtualCamera* vcam)
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

    void CameraManager::RemoveVirtualCamera(VirtualCamera* vcam) noexcept
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

    void CameraManager::SetBlendDuration(float seconds) noexcept
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

    bool CameraManager::AddModifier(std::unique_ptr<CameraModifier> modifier)
    {
        if (modifier == nullptr)
        {
            return false;
        }
        // 同じ種類の効果は 1 つだけ残す。積み直すと前の物の途中から始め直さず、新しい設定の頭から描く
        if (const void* kind = modifier->Kind(); kind != nullptr)
        {
            RemoveModifiers(kind);
        }
        const int order = modifier->Order();
        const std::vector<std::unique_ptr<CameraModifier>>::iterator at =
            std::find_if(m_modifiers.begin(), m_modifiers.end(), [order](const std::unique_ptr<CameraModifier>& m) {
                return m->Order() > order;
            });
        m_modifiers.insert(at, std::move(modifier));
        return true;
    }

    void CameraManager::RemoveModifiers(const void* kind) noexcept
    {
        std::erase_if(m_modifiers, [kind](const std::unique_ptr<CameraModifier>& m) { return m->Kind() == kind; });
    }

    void CameraManager::ClearModifiers() noexcept
    {
        m_modifiers.clear();
    }

    bool CameraManager::StartShake(const CameraShakeDesc& desc)
    {
        return AddModifier(CameraShakeModifier::Create(desc, SideSignOf(desc.firstSideDirection)));
    }

    NS::Core::Vector2 CameraManager::ShakeOffset() const noexcept
    {
        if (const CameraShakeModifier* shake = FindModifier<CameraShakeModifier>())
        {
            return shake->Offset();
        }
        return NS::Core::Vector2{0.0f, 0.0f};
    }

    bool CameraManager::StartZoomRoll(const CameraZoomRollDesc& desc)
    {
        return AddModifier(CameraZoomRollModifier::Create(desc, SideSignOf(desc.rollDirection)));
    }

    CameraZoomRoll CameraManager::ZoomRoll() const noexcept
    {
        if (const CameraZoomRollModifier* zoomRoll = FindModifier<CameraZoomRollModifier>())
        {
            return zoomRoll->Current();
        }
        return CameraZoomRoll{};
    }

    float CameraManager::SideSignOf(const NS::Core::Vector3& direction) const noexcept
    {
        return SideSign(ForwardHorizontal(), direction);
    }

    void CameraManager::BeginBlendFrom(const CameraPose& pose) noexcept
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

    VirtualCamera* CameraManager::SelectActive() const noexcept
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

    std::optional<CameraPose> CameraManager::EvaluateTopPose(float alpha) const noexcept
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

    void CameraManager::OnUpdate()
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

        // 効果を 1 フレーム進め、描き終えた物を外す。積んだ直後の物は進めない
        for (const std::unique_ptr<CameraModifier>& modifier : m_modifiers)
        {
            modifier->Tick();
        }
        std::erase_if(m_modifiers, [](const std::unique_ptr<CameraModifier>& m) { return m->IsFinished(); });
    }

    std::optional<CameraPose> CameraManager::ComposeBeforeEffects(float alpha) const noexcept
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
            pose = CameraPose::Lerp(m_blendFrom, pose, NS::Core::SmoothStep(t));
        }
        return pose;
    }

    std::optional<CameraPose> CameraManager::ViewPose() const noexcept
    {
        // 割合 1 は今の固定ステップの状態そのもの。描画が何枚・どの割合で描いても同じ値になる
        return ComposeBeforeEffects(1.0f);
    }

    std::optional<CameraPose> CameraManager::ComposePose(float alpha) const noexcept
    {
        std::optional<CameraPose> composed = ComposeBeforeEffects(alpha);
        if (!composed.has_value())
        {
            return std::nullopt;
        }
        CameraPose pose = *composed;

        // 効果はブレンドの後に掛ける。どの vcam が選ばれていてもブレンド中でも一様に掛かる
        // 軸は効果を掛ける前の姿勢から 1 度だけ作る。揺れは平行移動なので、後の傾きの軸は変わらない
        const CameraAxes axes = ViewAxes(pose);
        for (const std::unique_ptr<CameraModifier>& modifier : m_modifiers)
        {
            modifier->Modify(pose, axes);
        }
        return pose;
    }

    void CameraManager::Evaluate(float alpha) noexcept
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

    NS::Core::Matrix CameraManager::ViewProjection() const noexcept
    {
        if (m_camera != nullptr)
        {
            return m_camera->ViewProjection();
        }
        return NS::Core::Matrix::Identity;
    }

    NS::Core::Vector3 CameraManager::ForwardHorizontal() const noexcept
    {
        const std::optional<CameraPose> view = ViewPose();
        if (!view.has_value())
        {
            return NS::Core::Vector3{0.0f, 0.0f, 1.0f};
        }
        NS::Core::Vector3 forward{};
        if (!NS::Core::TryNormalizeHorizontal(view->target - view->position, forward))
        {
            return NS::Core::Vector3{0.0f, 0.0f, 1.0f};
        }
        return forward;
    }

} // namespace NS::Obj
