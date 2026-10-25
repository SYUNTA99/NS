#include "Game/Player/PlayerParams.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Core/Windows/Clock.h"

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    // 欄の秒を固定ステップのフレーム数にする。PlayerActor::AdvanceCharge と同じ丸め
    int FramesFor(float seconds)
    {
        return static_cast<int>(std::lround(seconds / NS::OS::FrameTimer::FixedDelta()));
    }

    // しきい値 0.2 秒・満タン 1 秒・溜めすぎ 0.5 秒。強制で出るのは押し始めから 1.5 秒のフレーム
    void UseShortOvercharge(PlayerActor* player)
    {
        NS_ASSERT(Game, player != nullptr, "UseShortOvercharge: 渡したアクターか部品が無い");
        ASSERT_EQ(
            NS::ApplyJsonFields(&player->Params(),
                                {{"チャージしきい値秒", 0.2f}, {"チャージ満タン秒", 1.0f}, {"溜めすぎの秒数", 0.5f}}),
            0u);
    }
} // namespace

