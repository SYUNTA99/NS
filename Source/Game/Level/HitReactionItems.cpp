#include "Game/Level/HitReactionItems.h"

#include "Game/Level/BodyShake.h"
#include "Game/Level/CameraHitModifiers.h"
#include "Game/Level/GameScene.h"
#include "Game/Level/HitScreenDirector.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/IGet/IGetCamera.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

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
                NS_ASSERT(Game, owner != nullptr, "StartCameraNudge: owner が null");
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

        void HitShapeInfo::Start(const NS::HitReactionStart& start) const
        {
            if (start.owner.model == nullptr || start.length <= 0)
            {
                return;
            }
            // 向きが決まらない時は水平の倍率を掛けず、高さだけを変える
            NS::Vector3 alongDirection{};
            if (start.hasToward)
            {
                alongDirection = start.towardOther;
            }
            start.reaction->AddRun(std::make_unique<BodyShapeRun>(BodyShapeDesc{.along = along,
                                                                                .height = height,
                                                                                .side = side,
                                                                                .alongDirection = alongDirection,
                                                                                .length = start.length}));
        }

        void HitBodyShakeInfo::Start(const NS::HitReactionStart& start) const
        {
            const NS::SceneActor* actor = start.owner.actor;
            if (start.owner.model == nullptr || start.length <= 0 || !std::isfinite(amplitudePixels))
            {
                return;
            }
            const std::optional<NS::CameraPose> pose = NS::GetCameraViewPose(actor);
            if (!pose.has_value())
            {
                return;
            }
            // 画面の横を床に沿わせた向きで揺らし、最初は相手と逆の側へ振る。正面の相手は右から
            float firstSign = 1.0f;
            if (NS::HasPositiveMagnitude(NS::Dot(start.toOther, start.cameraRight)))
            {
                firstSign = -1.0f;
            }
            start.reaction->AddRun(std::make_unique<BodyShakeRun>(
                BodyShakeDesc{.axis = start.cameraRight,
                              .amplitude = NS::ScreenPixelsToMeters(amplitudePixels, *pose, actor->Root().Position()),
                              .length = start.length,
                              .seed = start.seed,
                              .firstSign = firstSign,
                              .flipFrames = flipFrames,
                              .amplitudeSpread = amplitudeSpread}));
        }

        void HitBodyTremorInfo::Start(const NS::HitReactionStart& start) const
        {
            if (start.owner.model == nullptr || start.length <= 0)
            {
                return;
            }
            // 裏まで届くのに長さを使い切ると震えが残らないので、届くのは長さの半分まで
            start.reaction->AddRun(std::make_unique<BodyTremorRun>(
                BodyTremorDesc{.contactOffset = start.position - start.owner.actor->Root().Position(),
                               .amplitudePixels = amplitudePixels,
                               .amplitudeRatio = radiusRatio,
                               .reachFrames = std::min(reachFrames, start.length / 2),
                               .length = start.length,
                               .referenceHeight = referenceHeight,
                               .bodyLength = start.bodyLength}));
        }

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

        void HitFlashInfo::Start(const NS::HitReactionStart& start) const
        {
            if (NS::Scene* scene = start.owner.actor->OwningScene())
            {
                scene->ScreenFlash().Start(start.owner.actor, start.length, alpha);
            }
        }

        void HitDistortionRingInfo::Start(const NS::HitReactionStart& start) const
        {
            HitScreenDirector* screen = HitScreenOf(start.owner.actor);
            if (screen == nullptr)
            {
                return;
            }
            screen->StartDistortionRing(start.owner.actor,
                                        HitDistortionRingDesc{.center = start.position,
                                                              .radius = radius,
                                                              .push = push,
                                                              .halfWidth = halfWidth,
                                                              .frames = start.length});
        }

        void HitGroundWaveInfo::Start(const NS::HitReactionStart& start) const
        {
            HitScreenDirector* screen = HitScreenOf(start.owner.actor);
            if (screen == nullptr)
            {
                return;
            }
            screen->StartGroundWave(start.owner.actor,
                                    HitGroundWaveDesc{.center = start.position,
                                                      .radius = radius,
                                                      .strength = strength,
                                                      .frames = start.length});
        }

        void HitOthersStopInfo::Start(const NS::HitReactionStart& start) const
        {
            if (NS::Scene* scene = start.owner.actor->OwningScene())
            {
                scene->RequestStopScene(start.owner.actor, start.length);
            }
        }

        NS_HIT_REACTION_ITEM(HitShapeInfo)
        NS_HIT_REACTION_ITEM(HitBodyShakeInfo)
        NS_HIT_REACTION_ITEM(HitBodyTremorInfo)
        NS_HIT_REACTION_ITEM(HitCameraSinkInfo)
        NS_HIT_REACTION_ITEM(HitCameraSinkReleaseInfo)
        NS_HIT_REACTION_ITEM(HitZoomRollInfo)
        NS_HIT_REACTION_ITEM(HitCameraLurchInfo)
        NS_HIT_REACTION_ITEM(HitCameraAwaySwayInfo)
        NS_HIT_REACTION_ITEM(HitFlashInfo)
        NS_HIT_REACTION_ITEM(HitDistortionRingInfo)
        NS_HIT_REACTION_ITEM(HitGroundWaveInfo)
        NS_HIT_REACTION_ITEM(HitOthersStopInfo)
    } // namespace Level
} // namespace GL
