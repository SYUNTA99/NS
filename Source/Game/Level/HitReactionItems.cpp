#include "Game/Level/HitReactionItems.h"

#include "Game/Level/CameraHitModifiers.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/IGet/IGetCamera.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>

namespace GL
{
    namespace Level
    {
        namespace
        {
            // カメラのずれを始める。位置と注視点を同じだけずらし、向きは変えない。フレーム数 0 以下ならずらさない
            // 設定が壊れていれば知らせる。カメラの無い場面 (試しの台) は黙って何もしない
            void StartCameraNudge(const NS::SceneActor* owner, const CameraNudgeDesc& desc)
            {
                if (desc.frames <= 0 || owner->GetCameraManager() == nullptr)
                {
                    return;
                }
                if (!NS::AddCameraModifier(owner, CameraNudgeModifier::Create(desc)))
                {
                    NS_LOG_WARN(Game,
                                "ずれの設定が壊れていて、ずらさなかった: 向き ({}, {}, {}) フレーム数 {}",
                                desc.direction.x,
                                desc.direction.y,
                                desc.direction.z,
                                desc.frames);
                }
            }
        } // namespace

        void HitCameraSinkInfo::Start(const NS::HitReactionStart& start) const
        {
            const NS::SceneActor* owner = start.owner.actor;
            // 跳ね返りは同じ持ち主の「沈みを戻す」で始まる。来なければ底のまま長さで終わる
            const CameraSinkDesc desc{.bottomPixels = bottomPixels,
                                      .sinkFrames = sinkFrames,
                                      .tremblePixels = tremblePixels,
                                      .trembleFrames = trembleFrames,
                                      .tremblePeriodFrames = tremblePeriodFrames,
                                      .bounceStartFrame = start.length,
                                      .overshootRatio = overshootRatio,
                                      .bouncePeriodFrames = bouncePeriodFrames,
                                      .frames = start.length,
                                      .referenceHeightPixels = referenceHeightPixels};
            if (desc.frames <= 0 || owner->GetCameraManager() == nullptr)
            {
                return;
            }
            if (!NS::AddCameraModifier(owner, CameraSinkModifier::Create(desc)))
            {
                NS_LOG_WARN(Game,
                            "沈む揺れの設定が壊れていて、揺らさなかった: 深さ {} 震え {} 行き過ぎ {} フレーム数 {}",
                            desc.bottomPixels,
                            desc.tremblePixels,
                            desc.overshootRatio,
                            desc.frames);
            }
        }

        void HitCameraSinkReleaseInfo::Start(const NS::HitReactionStart& start) const
        {
            ReleaseCameraSink(start.owner.actor);
        }

        void HitZoomRollInfo::Start(const NS::HitReactionStart& start) const
        {
            const NS::SceneActor* owner = start.owner.actor;
            const int frames = std::max(start.length, 0);
            CameraZoomRollDesc desc;
            desc.zoom = zoom;
            desc.rollDegrees = rollDegrees;
            if (start.hasToward)
            {
                desc.rollDirection = start.towardOther;
            }
            desc.returnFrames = std::clamp(returnFrames, 0, frames);
            desc.holdFrames = frames - desc.returnFrames;
            desc.delayFrames = delayFrames;
            if (owner->GetCameraManager() == nullptr)
            {
                return;
            }
            if (!StartCameraZoomRoll(owner, desc))
            {
                NS_LOG_WARN(Game,
                            "寄りと傾きの設定が壊れていて、寄せなかった: 倍率 {} 傾き {} 保つ {} 戻す {}",
                            desc.zoom,
                            desc.rollDegrees,
                            desc.holdFrames,
                            desc.returnFrames);
            }
        }

        void HitCameraLurchInfo::Start(const NS::HitReactionStart& start) const
        {
            if (!start.hasToward)
            {
                return;
            }
            StartCameraNudge(start.owner.actor,
                             CameraNudgeDesc{.direction = start.towardOther,
                                             .distance = distance,
                                             .frames = start.length,
                                             .onScreen = false});
        }

        void HitCameraAwaySwayInfo::Start(const NS::HitReactionStart& start) const
        {
            if (!start.hasToward)
            {
                return;
            }
            StartCameraNudge(start.owner.actor,
                             CameraNudgeDesc{.direction = -start.towardOther,
                                             .distance = distance,
                                             .frames = start.length,
                                             .onScreen = true,
                                             .minScreenShare = minScreenShare});
        }

        void HitOthersStopInfo::Start(const NS::HitReactionStart& start) const
        {
            if (NS::Scene* scene = start.owner.actor->OwningScene())
            {
                scene->RequestStopScene(start.owner.actor, start.length);
            }
        }

        NS_HIT_REACTION_ITEM(HitCameraSinkInfo)
        NS_HIT_REACTION_ITEM(HitCameraSinkReleaseInfo)
        NS_HIT_REACTION_ITEM(HitZoomRollInfo)
        NS_HIT_REACTION_ITEM(HitCameraLurchInfo)
        NS_HIT_REACTION_ITEM(HitCameraAwaySwayInfo)
        NS_HIT_REACTION_ITEM(HitOthersStopInfo)
    } // namespace Level
} // namespace GL
