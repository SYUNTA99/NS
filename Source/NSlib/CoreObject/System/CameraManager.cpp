#include "NSlib/CoreObject/System/CameraManager.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/VirtualCamera.h"
#include "NSlib/CoreObject/Scene/SceneCamera.h"
#include "NSlib/Core/Windows/Clock.h"

#include <algorithm>
#include <cmath>

namespace NS
{
    namespace
    {
        // 減る速さが 0 以下だと揺れが終わらず、非数は姿勢を非数にして画面を消す
        [[nodiscard]] bool IsValidTraumaShape(const CameraTraumaShape& shape) noexcept
        {
            return IsPositiveFinite(shape.decayPerSecond) && std::isfinite(shape.yawDegrees) &&
                   std::isfinite(shape.pitchDegrees) && std::isfinite(shape.rollDegrees) &&
                   std::isfinite(shape.frequency) && std::isfinite(shape.exponent);
        }

        // 姿勢の視線の水平から右、視線と右から上を作る。どちらも長さ 1
        [[nodiscard]] CameraAxes ViewAxes(const CameraPose& pose) noexcept
        {
            CameraAxes axes{};
            const Vector3 look = pose.target - pose.position;
            Vector3 forward{};
            if (!TryNormalizeHorizontal(look, forward))
            {
                forward = Vector3{0.0f, 0.0f, 1.0f};
            }
            axes.right = Vector3{forward.z, 0.0f, -forward.x};

            Vector3 up = Cross(look, axes.right);
            if (IsNearlyZero(up))
            {
                up = Vector3{0.0f, 1.0f, 0.0f};
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
        if (std::any_of(m_vcams.begin(), m_vcams.end(), [vcam](const VirtualCameraEntry& entry) {
                return entry.vcam == vcam;
            }))
        {
            return;
        }
        m_vcams.push_back(VirtualCameraEntry{.vcam = vcam});
    }

    void CameraManager::RemoveVirtualCamera(const VirtualCamera* vcam) noexcept
    {
        if (vcam == nullptr)
        {
            return;
        }
        std::erase_if(m_vcams, [vcam](const VirtualCameraEntry& entry) { return entry.vcam == vcam; });
        if (m_active == vcam)
        {
            m_active = nullptr; // 次の OnTick / Evaluate で選び直す
        }
    }

    void CameraManager::SetVirtualCameraEnabled(const VirtualCamera* vcam, bool enabled) noexcept
    {
        NS_ASSERT(Scene, vcam != nullptr, "SetVirtualCameraEnabled: 仮想カメラが null");
        for (VirtualCameraEntry& entry : m_vcams)
        {
            if (entry.vcam == vcam)
            {
                entry.enabled = enabled;
            }
        }
    }

    std::size_t CameraManager::EnabledVirtualCameraCount() const noexcept
    {
        return static_cast<std::size_t>(std::count_if(
            m_vcams.begin(), m_vcams.end(), [](const VirtualCameraEntry& entry) { return entry.enabled; }));
    }

    bool CameraManager::IsVirtualCameraEnabled(const VirtualCamera* vcam) const noexcept
    {
        return std::any_of(m_vcams.begin(), m_vcams.end(), [vcam](const VirtualCameraEntry& entry) {
            return entry.vcam == vcam && entry.enabled;
        });
    }

    void CameraManager::SetBlendDuration(float seconds) noexcept
    {
        // 引数の順を変えない。非数は 0 になる
        m_blendDuration = std::max(0.0f, seconds);
    }

    bool CameraManager::AddModifier(std::unique_ptr<CameraModifier> modifier)
    {
        if (modifier == nullptr)
        {
            return false;
        }
        // 頼んだ物ごとに同じ種類の効果は 1 つだけ残す。積み直すと前の物の途中から始め直さず、新しい設定の頭から描く
        if (const void* kind = modifier->Kind(); kind != nullptr)
        {
            RemoveModifiersOf(modifier->Requester(), kind);
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

    void CameraManager::RemoveModifiersOf(const IGetCamera* requester, const void* kind) noexcept
    {
        std::erase_if(m_modifiers, [requester, kind](const std::unique_ptr<CameraModifier>& m) {
            return m->Requester() == requester && m->Kind() == kind;
        });
    }

    void CameraManager::RemoveModifiersOf(const IGetCamera* requester) noexcept
    {
        std::erase_if(m_modifiers,
                      [requester](const std::unique_ptr<CameraModifier>& m) { return m->Requester() == requester; });
    }

    void CameraManager::ReleaseSink(const IGetCamera* requester) noexcept
    {
        for (const std::unique_ptr<CameraModifier>& modifier : m_modifiers)
        {
            if (modifier->Kind() == CameraSinkModifier::StaticKind() && modifier->Requester() == requester)
            {
                static_cast<CameraSinkModifier*>(modifier.get())->Release();
            }
        }
    }

    void CameraManager::ClearModifiers() noexcept
    {
        m_modifiers.clear();
    }

    bool CameraManager::StartZoomRoll(const CameraZoomRollDesc& desc, const IGetCamera* requester)
    {
        std::unique_ptr<CameraZoomRollModifier> zoomRoll =
            CameraZoomRollModifier::Create(desc, SideSignOf(desc.rollDirection));
        if (zoomRoll != nullptr)
        {
            zoomRoll->SetRequester(requester);
        }
        return AddModifier(std::move(zoomRoll));
    }

    CameraZoomRoll CameraManager::ZoomRoll() const noexcept
    {
        if (const CameraZoomRollModifier* zoomRoll = FindModifier<CameraZoomRollModifier>())
        {
            return zoomRoll->Current();
        }
        return CameraZoomRoll{};
    }

    CameraTraumaModifier* CameraManager::TraumaModifier()
    {
        for (const std::unique_ptr<CameraModifier>& modifier : m_modifiers)
        {
            if (modifier->Kind() == CameraTraumaModifier::StaticKind())
            {
                return static_cast<CameraTraumaModifier*>(modifier.get());
            }
        }
        std::unique_ptr<CameraTraumaModifier> created = std::make_unique<CameraTraumaModifier>();
        CameraTraumaModifier* raw = created.get();
        (void)AddModifier(std::move(created));
        return raw;
    }

    bool CameraManager::AddTrauma(const CameraTraumaDesc& desc, const IGetCamera* requester)
    {
        if (!IsNonNegativeFinite(desc.trauma) || !IsValidTraumaShape(desc.shape) ||
            !std::isfinite(desc.kick.degrees))
        {
            return false;
        }
        CameraTraumaModifier* trauma = TraumaModifier();
        trauma->AddTrauma(desc);
        trauma->SetRequester(requester);
        return true;
    }

    bool CameraManager::HoldTrauma(float level, const CameraTraumaShape& shape)
    {
        if (!IsNonNegativeFinite(level) || !IsValidTraumaShape(shape))
        {
            return false;
        }
        TraumaModifier()->HoldTrauma(level, shape);
        return true;
    }

    float CameraManager::Trauma() const noexcept
    {
        if (const CameraTraumaModifier* trauma = FindModifier<CameraTraumaModifier>())
        {
            return trauma->Trauma();
        }
        return 0.0f;
    }

    void CameraManager::SetShakeScale(float scale) noexcept
    {
        // 非数は Clamp を素通りする
        if (!std::isfinite(scale))
        {
            return;
        }
        m_shakeScale = Clamp(scale, 0.0f, 1.0f);
    }

    float CameraManager::SideSignOf(const Vector3& direction) const noexcept
    {
        const Vector3 forwardHorizontal = ForwardHorizontal();
        const Vector3 right{forwardHorizontal.z, 0.0f, -forwardHorizontal.x};
        if (Dot(right, direction) < 0.0f)
        {
            return -1.0f;
        }
        return 1.0f;
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
        for (const VirtualCameraEntry& entry : m_vcams)
        {
            if (!entry.enabled)
            {
                continue;
            }
            if (best == nullptr || entry.vcam->VcamPriority() > best->VcamPriority())
            {
                best = entry.vcam;
            }
        }
        return best;
    }

    VirtualCamera* CameraManager::SelectHighest() const noexcept
    {
        VirtualCamera* best = nullptr;
        for (const VirtualCameraEntry& entry : m_vcams)
        {
            if (best == nullptr || entry.vcam->VcamPriority() > best->VcamPriority())
            {
                best = entry.vcam;
            }
        }
        return best;
    }

    std::optional<CameraPose> CameraManager::EvaluateTopPose(float alpha) const noexcept
    {
        VirtualCamera* best = SelectHighest();
        if (best == nullptr)
        {
            return std::nullopt;
        }
        return best->EvaluatePose(alpha);
    }

    void CameraManager::OnTick()
    {
        // 進めた後の姿勢で選び直す。進めている間に足した効果は、この後の効果の歩で積んだ直後として扱う
        if (m_collision != nullptr)
        {
            for (const VirtualCameraEntry& entry : m_vcams)
            {
                if (entry.enabled)
                {
                    entry.vcam->Advance(this, m_collision);
                }
            }
        }
        VirtualCamera* next = SelectActive();
        if (next != m_active)
        {
            // 直前まで写していた pose から新 仮想カメラへ繋ぐ。旧 pose が無い初回 active 化はカットする
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
            m_blendElapsed += OS::FrameTimer::FixedDelta();
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
        if (!IsVirtualCameraEnabled(active))
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
            const float t = Clamp(m_blendElapsed / m_blendDuration, 0.0f, 1.0f);
            pose = CameraPose::Lerp(m_blendFrom, pose, SmoothStep(t));
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

        // 効果はブレンドの後に掛ける。どの 仮想カメラが選ばれていてもブレンド中でも一様に掛かる
        // 軸は効果を掛ける前の姿勢から 1 度だけ作る。揺れは平行移動なので、後の傾きの軸は変わらない
        const CameraAxes axes = ViewAxes(pose);
        for (const std::unique_ptr<CameraModifier>& modifier : m_modifiers)
        {
            if (modifier->IsShake() && m_shakeScale < 1.0f)
            {
                // 揺れを掛けた姿勢へ倍率の分だけ寄せる。揺れの角度とずれは小さいので、寄せた量が振れ幅の倍率になる
                CameraPose shaken = pose;
                modifier->Modify(shaken, axes);
                pose = CameraPose::Lerp(pose, shaken, m_shakeScale);
                continue;
            }
            modifier->Modify(pose, axes);
        }
        return pose;
    }

    void CameraManager::Evaluate(float alpha) noexcept
    {
        // 非 active になった 仮想カメラの pose は書かない。一時停止で OnTick が回らない間も選び直す
        if (!IsVirtualCameraEnabled(m_active))
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

    Matrix CameraManager::ViewProjection() const noexcept
    {
        if (m_camera != nullptr)
        {
            return m_camera->ViewProjection();
        }
        return Matrix::Identity;
    }

    Vector3 CameraManager::ForwardHorizontal() const noexcept
    {
        const std::optional<CameraPose> view = ViewPose();
        if (!view.has_value())
        {
            return Vector3{0.0f, 0.0f, 1.0f};
        }
        Vector3 forward{};
        if (!TryNormalizeHorizontal(view->target - view->position, forward))
        {
            return Vector3{0.0f, 0.0f, 1.0f};
        }
        return forward;
    }

} // namespace NS
