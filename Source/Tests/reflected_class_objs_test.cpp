#include "Game/Level/PropActor.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

// 保存・Inspector・種類の既定は、この並びで欄を書き読みする。基底の枠を先に、置物の部品を後に並べる
TEST(ReflectedClassObjs, ListsBaseFramePartsBeforeTheClassPartsWithTheirSaveKeys)
{
    const std::unique_ptr<GL::Level::PropActor> actor = NS::CreateActor<GL::Level::PropActor>();
    std::vector<std::string> keys;
    NS::ForEachReflectedClassObj(*actor,
                                 [&](const NS::ClassObjInfo& field, NS::Object*) { keys.emplace_back(field.key); });
    ASSERT_GE(keys.size(), 4u);
    EXPECT_EQ(keys.front(), "Transform");
    EXPECT_EQ((std::vector<std::string>(keys.end() - 3, keys.end())),
              (std::vector<std::string>{"Params", "HitZones", "LaunchEffects"}));
}
