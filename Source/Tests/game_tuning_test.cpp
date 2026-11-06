#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ClassObjs/EffectObj.h"

#include <gtest/gtest.h>

TEST(GameTuning, EffectHistoryRetentionBelongsToEachKeeper)
{
    NS::Effect asset{"11111111111111111111111111111111"};
    asset.Replace({}, NS::Gfx::EffectResourceDesc{asset.Id(), "test", {}, 1});
    NS::EffectObj shortHistory{2};
    NS::EffectObj longHistory{6};
    const NS::uint32 shortId = shortHistory.Play(nullptr, &asset, {});
    const NS::uint32 longId = longHistory.Play(nullptr, &asset, {});
    shortHistory.Stop(nullptr, shortId);
    longHistory.Stop(nullptr, longId);
    for (int frame = 0; frame < 3; ++frame)
    {
        shortHistory.BeginStep(nullptr);
        longHistory.BeginStep(nullptr);
    }
    EXPECT_TRUE(shortHistory.Records().empty());
    EXPECT_EQ(longHistory.Records().size(), 1u);
}
