#include "NSlib/CoreObject/ClassObjs/HitReaction.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/IUse/IUseCamera.h"
#include "NSlib/CoreObject/IUse/IUseSceneSystem.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace NS
{
    namespace
    {
        // 揺れのフレーム数の上限。揺れの並びは始める時に全フレームぶんの領域を取るので、欄の打ち間違いの大きな値を止める
        // 1 秒を超える揺れは当たりの返りではなく、画面が揺れ続けている状態
        constexpr int k_MaxShakeFrames = 60;

        // 上限を超えた長さは警告して断る。カメラの無い場面は黙って断る
        bool CanStartCameraMotion(const SceneActor* owner, int frames)
        {
            if (frames > k_MaxShakeFrames)
            {
                NS_LOG_WARN(
                    Scene, "揺れのフレーム数 {} が上限 {} を超えていて、揺らさなかった", frames, k_MaxShakeFrames);
                return false;
            }
            return owner != nullptr && owner->GetCameraManager() != nullptr;
        }

        // 持ち主が nullptr の間は引かない。FindSceneSystem に nullptr を渡すと NS_ASSERT
        // で止まり、出荷版では検査されない
        template <class T> T* FindDirector(SceneActor* owner)
        {
            if (owner == nullptr)
            {
                return nullptr;
            }
            return FindSceneSystem<T>(owner);
        }
    } // namespace

    void HitReaction::CreateDirectors()
    {
        if (m_owner == nullptr)
        {
            return;
        }
        (void)GetOrCreateSceneSystem<HitScreenDirector>(m_owner);
        (void)GetOrCreateSceneSystem<PadRumbleDirector>(m_owner);
    }

    void HitReaction::StartFlash(int frames, float alpha) noexcept
    {
        if (HitScreenDirector* screen = FindDirector<HitScreenDirector>(m_owner))
        {
            screen->StartFlash(*m_owner, frames, alpha);
        }
    }

    bool HitReaction::StartShake(const CameraShakeDesc& desc)
    {
        if (desc.frames <= 0)
        {
            return true;
        }
        if (!CanStartCameraMotion(m_owner, desc.frames))
        {
            return false;
        }
        if (!StartCameraShake(m_owner, desc))
        {
            NS_LOG_WARN(Scene,
                        "揺れの設定が壊れていて、揺らさなかった: 横 {} 縦 {} フレーム数 {} 最長 {}",
                        desc.sideAmplitude,
                        desc.upAmplitude,
                        desc.frames,
                        desc.longestFlipFrames);
            return false;
        }
        return true;
    }

    bool HitReaction::StartSink(const CameraSinkDesc& desc)
    {
        if (desc.frames <= 0)
        {
            return true;
        }
        if (!CanStartCameraMotion(m_owner, desc.frames))
        {
            return false;
        }
        if (!AddCameraModifier(m_owner, CameraSinkModifier::Create(desc)))
        {
            NS_LOG_WARN(Scene,
                        "沈む揺れの設定が壊れていて、揺らさなかった: 深さ {} 震え {} 行き過ぎ {} フレーム数 {}",
                        desc.bottomPixels,
                        desc.tremblePixels,
                        desc.overshootRatio,
                        desc.frames);
            return false;
        }
        return true;
    }

    bool HitReaction::StartNudge(const CameraNudgeDesc& desc)
    {
        if (desc.frames <= 0)
        {
            return true;
        }
        if (!CanStartCameraMotion(m_owner, desc.frames))
        {
            return false;
        }
        if (!AddCameraModifier(m_owner, CameraNudgeModifier::Create(desc)))
        {
            NS_LOG_WARN(Scene,
                        "ずれの設定が壊れていて、ずらさなかった: 向き ({}, {}, {}) フレーム数 {}",
                        desc.direction.x,
                        desc.direction.y,
                        desc.direction.z,
                        desc.frames);
            return false;
        }
        return true;
    }

    bool HitReaction::StartZoomRoll(const CameraZoomRollDesc& desc)
    {
        if (m_owner == nullptr || m_owner->GetCameraManager() == nullptr)
        {
            return false;
        }
        if (!StartCameraZoomRoll(m_owner, desc))
        {
            NS_LOG_WARN(Scene,
                        "寄りと傾きの設定が壊れていて、寄せなかった: 倍率 {} 傾き {} 保つ {} 戻す {}",
                        desc.zoom,
                        desc.rollDegrees,
                        desc.holdFrames,
                        desc.returnFrames);
            return false;
        }
        return true;
    }

    void HitReaction::StartShakeLines(const HitShakeLinesDesc& desc) noexcept
    {
        if (HitScreenDirector* screen = FindDirector<HitScreenDirector>(m_owner))
        {
            screen->StartShakeLines(*m_owner, desc);
        }
    }

    void HitReaction::StartDistortionRing(const HitDistortionRingDesc& desc) noexcept
    {
        if (HitScreenDirector* screen = FindDirector<HitScreenDirector>(m_owner))
        {
            screen->StartDistortionRing(*m_owner, desc);
        }
    }

    void HitReaction::StopDistortionRing() noexcept
    {
        if (HitScreenDirector* screen = FindDirector<HitScreenDirector>(m_owner))
        {
            screen->StopDistortionRing(*m_owner);
        }
    }

    void HitReaction::StartPadVibration(const HitPadVibration& pad)
    {
        if (PadRumbleDirector* rumble = FindDirector<PadRumbleDirector>(m_owner))
        {
            rumble->Start(*m_owner, pad);
        }
    }

    void HitReaction::BlendPadVibration(const HitPadVibration& pad)
    {
        if (PadRumbleDirector* rumble = FindDirector<PadRumbleDirector>(m_owner))
        {
            rumble->Blend(*m_owner, pad);
        }
    }

    void HitReaction::Stop()
    {
        if (m_owner == nullptr)
        {
            return;
        }
        if (HitScreenDirector* screen = FindSceneSystem<HitScreenDirector>(m_owner))
        {
            screen->Stop(*m_owner);
        }
        if (PadRumbleDirector* rumble = FindSceneSystem<PadRumbleDirector>(m_owner))
        {
            rumble->Stop(*m_owner);
        }
        StopCameraHitEffects(m_owner);
    }

    bool HitReaction::AddTrauma(const CameraTraumaDesc& desc)
    {
        // カメラの無い場面 (試しの台) では揺らす先が無い。設定の誤りではないので黙って返す
        if (m_owner == nullptr || m_owner->GetCameraManager() == nullptr)
        {
            return false;
        }
        if (!AddCameraTrauma(m_owner, desc))
        {
            NS_LOG_WARN(Scene,
                        "トラウマの量か形が壊れていて、揺らさなかった: 量 {} 減る速さ {}",
                        desc.trauma,
                        desc.shape.decayPerSecond);
            return false;
        }
        return true;
    }

    void HitReaction::StopAll()
    {
        Stop();
        // プレイを終えた後の視点にトラウマを残さない
        if (m_owner != nullptr)
        {
            StopCameraEffects(m_owner);
        }
    }
} // namespace NS
