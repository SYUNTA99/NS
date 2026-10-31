#include "Game/Level/LevelMessages.h"

#include "Game/Level/TackleGhost.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <cmath>
#include <functional>
#include <limits>
#include <map>

namespace GL
{
    namespace Level
    {
        bool SendMsgInstantDeath(NS::HitSensor* receiver, NS::HitSensor* sender)
        {
            NS_ASSERT(Game, receiver != nullptr && sender != nullptr, "知らせの受け手か送り手のセンサーが無い");
            return NS::SendMsg(MsgInstantDeath{}, receiver, sender);
        }

        bool SendMsgGoal(NS::HitSensor* receiver, NS::HitSensor* sender, float fadeOutSeconds, float fadeInSeconds)
        {
            NS_ASSERT(Game, receiver != nullptr && sender != nullptr, "知らせの受け手か送り手のセンサーが無い");
            return NS::SendMsg(MsgGoal{fadeOutSeconds, fadeInSeconds}, receiver, sender);
        }

        bool SendMsgCourseStart(NS::SceneActor* receiver)
        {
            NS_ASSERT(Game, receiver != nullptr, "知らせの受け手が無い");
            return NS::SendMsgToActor(MsgCourseStart{}, receiver);
        }

        bool SendMsgCourseRestart(NS::SceneActor* receiver)
        {
            NS_ASSERT(Game, receiver != nullptr, "知らせの受け手が無い");
            return NS::SendMsgToActor(MsgCourseRestart{}, receiver);
        }

        bool SendMsgInputLock(NS::SceneActor* receiver, bool locked)
        {
            NS_ASSERT(Game, receiver != nullptr, "知らせの受け手が無い");
            return NS::SendMsgToActor(MsgInputLock{locked}, receiver);
        }

        bool SendMsgAskTackleTarget(NS::HitSensor* receiver, TackleTargetAnswer& outAnswer)
        {
            NS_ASSERT(Game, receiver != nullptr, "知らせの受け手が無い");
            return NS::SendMsg(MsgAskTackleTarget{outAnswer}, receiver, nullptr);
        }

        bool SendMsgTackleHit(NS::HitSensor* receiver, NS::HitSensor* sender, const TackleHitDesc& desc)
        {
            NS_ASSERT(Game, receiver != nullptr && sender != nullptr, "知らせの受け手か送り手のセンサーが無い");
            return NS::SendMsg(MsgTackleHit{desc}, receiver, sender);
        }

        bool TackleGhostDesc::WriteTo(TackleGhost* ghost,
                                      int frame,
                                      const NS::Model* model,
                                      const NS::Matrix& root) const noexcept
        {
            NS_ASSERT(Game, ghost != nullptr, "残像が渡されていない");
            NS_ASSERT(Game, model != nullptr, "残像の元の Model が渡されていない");
            (void)ghost->SetSpread(axis * NS::BodyShakeReach(frame, stopLength, spread));
            (void)ghost->SetSideOpacity(sideOpacity);
            if (frame == stopLength)
            {
                ghost->CaptureAfterimage(model, root);
            }
            const int release = frame - stopLength - 1;
            if (release < 0 || release >= releaseFrames)
            {
                (void)ghost->SetAfterimageOpacity(0.0f);
                return release < releaseFrames;
            }
            const float left = 1.0f - static_cast<float>(release) / static_cast<float>(releaseFrames);
            (void)ghost->SetAfterimageOpacity(releaseOpacity * left);
            return true;
        }

        float HitOwnerValue(const NS::HitOwnerItem& item, std::string_view name, float fallback)
        {
            const std::map<std::string, float, std::less<>>::const_iterator found = item.values.find(name);
            if (found == item.values.end())
            {
                NS_LOG_WARN(Game, "束の項目 {} に値 {} が無いので {} を使う", item.name, name, fallback);
                return fallback;
            }
            return found->second;
        }

        int HitOwnerFrames(const NS::HitOwnerItem& item, std::string_view name, int fallback)
        {
            const float value = HitOwnerValue(item, name, static_cast<float>(fallback));
            // int の最大を float にすると 2^31 に丸まるので、その値は等号でも断る
            if (!std::isfinite(value) || value < 0.0f || value >= static_cast<float>(std::numeric_limits<int>::max()))
            {
                NS_LOG_WARN(
                    Game, "束の項目 {} の値 {} が 0 以上の int のフレーム数でないので {} を使う", item.name, name, fallback);
                return fallback;
            }
            return static_cast<int>(value);
        }

        std::string TackleSetName(std::string_view base, HitTier tier, std::string_view variant)
        {
            std::string_view label = "真ん中";
            switch (tier)
            {
            case HitTier::Wide:
                label = "外れ";
                break;
            case HitTier::Center:
                break;
            }
            std::string name{base};
            name += "[";
            name += label;
            name += variant;
            name += "]";
            return name;
        }

        std::string_view BeforeContactActionName(HitTier tier) noexcept
        {
            switch (tier)
            {
            case HitTier::Wide:
                return "BeforeContactMiss";
            case HitTier::Center:
                break;
            }
            return "BeforeContactCenter";
        }

        bool SendMsgTackleRelease(NS::SceneActor* receiver, const TackleReleaseDesc& desc)
        {
            NS_ASSERT(Game, receiver != nullptr, "知らせの受け手が無い");
            return NS::SendMsgToActor(MsgTackleRelease{desc}, receiver);
        }
    } // namespace Level
} // namespace GL
