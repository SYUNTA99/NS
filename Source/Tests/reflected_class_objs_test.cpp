#include "Game/Level/GoalActor.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

// 保存・Inspector・.actor は、この並びで欄を書き読みする。基底の枠を先に、ゴールの部品を後に並べる
TEST(ReflectedClassObjs, ListsBaseFramePartsBeforeTheClassPartsWithTheirSaveKeys)
{
    const std::unique_ptr<GL::Level::GoalActor> actor = NS::CreateActor<GL::Level::GoalActor>();
    std::vector<std::string> keys;
    NS::ForEachReflectedClassObj(actor.get(),
                                 [&](const NS::ClassObjInfo& field, NS::Object*) { keys.emplace_back(field.key); });
    ASSERT_GE(keys.size(), 2u);
    EXPECT_EQ(keys.front(), "Transform");
    EXPECT_EQ((std::vector<std::string>(keys.end() - 1, keys.end())), (std::vector<std::string>{"Params"}));
}
