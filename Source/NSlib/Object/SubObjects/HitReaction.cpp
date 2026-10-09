#include "NSlib/Object/SubObjects/HitReaction.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/IUse/IUseCamera.h"
#include "NSlib/Object/IUse/IUseSceneObj.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"

namespace NS::Obj
{
    namespace
    {
        // 揺れのフレーム数の上限。揺れの並びは始める時に全フレームぶんの領域を取るので、欄の打ち間違いの大きな値を止める
        // 1 秒を超える揺れは当たりの返りではなく、画面が揺れ続けている状態
        constexpr int k_MaxShakeFrames = 60;

        // 上限を超えた長さは警告して断る。カメラの無い場面は黙って断る
        bool CanStartCameraMotion(const Actor* owner, int frames)
        {
            if (frames > k_MaxShakeFrames)
            {
                NS_LOG_WARN(
                    Scene, "揺れのフレーム数 {} が上限 {} を超えていて、揺らさなかった", frames, k_MaxShakeFrames);
                return false;
            }
            return owner != nullptr && owner->GetCameraManager() != nullptr;
        }
    } // namespace

    void HitReaction::OnStart()
    {
        if (Owner() == nullptr)
        {
            return;
        }
        (void)GetOrCreateSceneObj<HitScreenDirector>(*Owner());
        (void)GetOrCreateSceneObj<PadRumbleDirector>(*Owner());
    }

    void HitReaction::StartFlash(int frames, float alpha) noexcept
    {
        if (Owner() == nullptr)
        {
            return;
        }
        if (HitScreenDirector* screen = FindSceneObj<HitScreenDirector>(*Owner()))
        {
            screen->StartFlash(*Owner(), frames, alpha);
        }
    }

    bool HitReaction::StartShake(const CameraShakeDesc& desc)
    {
        if (desc.frames <= 0)
        {
            return true;
        }
        if (!CanStartCameraMotion(Owner(), desc.frames))
        {
            return false;
        }
        if (!StartCameraShake(*Owner(), desc))
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
        if (!CanStartCameraMotion(Owner(), desc.frames))
        {
            return false;
        }
        if (!AddCameraModifier(*Owner(), CameraSinkModifier::Create(desc)))
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
        if (!CanStartCameraMotion(Owner(), desc.frames))
        {
            return false;
        }
        if (!AddCameraModifier(*Owner(), CameraNudgeModifier::Create(desc)))
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
        if (Owner() == nullptr || Owner()->GetCameraManager() == nullptr)
        {
            return false;
        }
        if (!StartCameraZoomRoll(*Owner(), desc))
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
        if (Owner() == nullptr)
        {
            return;
        }
        if (HitScreenDirector* screen = FindSceneObj<HitScreenDirector>(*Owner()))
        {
            screen->StartShakeLines(*Owner(), desc);
        }
    }

    void HitReaction::StartPadVibration(const HitPadVibration& pad)
    {
        if (Owner() == nullptr)
        {
            return;
        }
        if (PadRumbleDirector* rumble = FindSceneObj<PadRumbleDirector>(*Owner()))
        {
            rumble->Start(*Owner(), pad);
        }
    }

    void HitReaction::BlendPadVibration(const HitPadVibration& pad)
    {
        if (Owner() == nullptr)
        {
            return;
        }
        if (PadRumbleDirector* rumble = FindSceneObj<PadRumbleDirector>(*Owner()))
        {
            rumble->Blend(*Owner(), pad);
        }
    }

    void HitReaction::Stop()
    {
        if (Owner() == nullptr)
        {
            return;
        }
        if (HitScreenDirector* screen = FindSceneObj<HitScreenDirector>(*Owner()))
        {
            screen->Stop(*Owner());
        }
        if (PadRumbleDirector* rumble = FindSceneObj<PadRumbleDirector>(*Owner()))
        {
            rumble->Stop(*Owner());
        }
        StopCameraHitEffects(*Owner());
    }

    bool HitReaction::AddTrauma(const CameraTraumaDesc& desc)
    {
        // カメラの無い場面 (試しの台) では揺らす先が無い。設定の誤りではないので黙って返す
        if (Owner() == nullptr || Owner()->GetCameraManager() == nullptr)
        {
            return false;
        }
        if (!AddCameraTrauma(*Owner(), desc))
        {
            NS_LOG_WARN(Scene,
                        "トラウマの量か形が壊れていて、揺らさなかった: 量 {} 減る速さ {}",
                        desc.trauma,
                        desc.shape.decayPerSecond);
            return false;
        }
        return true;
    }

    void HitReaction::OnEndPlay()
    {
        Stop();
        // プレイを終えた後の視点にトラウマを残さない
        if (Owner() != nullptr)
        {
            StopCameraEffects(*Owner());
        }
    }

    NS_CLASS(HitReaction)
} // namespace NS::Obj
