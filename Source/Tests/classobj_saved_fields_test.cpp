#include "Editor/EditorActors.h"
#include "Game/Level/PropActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ActorBuilder.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>

namespace
{
    struct OptionalColorValue
    {
        std::optional<NS::Color> tint;

        NS_REFLECT_BEGIN(OptionalColorValue, void)
        NS_FIELD(tint, "色")
        NS_REFLECT_END_VALUE()
    };

    void ExpectNoEnabledKey(const nlohmann::json& object)
    {
        const nlohmann::json::const_iterator classObjs = object.find("classObjs");
        ASSERT_NE(classObjs, object.end());
        for (nlohmann::json::const_iterator part = classObjs->begin(); part != classObjs->end(); ++part)
        {
            EXPECT_FALSE(part.value().contains("enabled")) << part.key();
        }
    }
} // namespace

TEST(ClassObjSavedFields, SavedJsonHasNoEnabledKey)
{
    const std::unique_ptr<PlayerActor> player = NS::CreateActor<PlayerActor>();
    ExpectNoEnabledKey(NS::ActorToJson(*player));
    ExpectNoEnabledKey(*NS::ActorFileLibrary::Get().Values("PlayerActor"));

    const std::unique_ptr<GL::Level::PropActor> prop = NS::CreateActor<GL::Level::PropActor>();
    ExpectNoEnabledKey(NS::ActorToJson(*prop));
    ExpectNoEnabledKey(*NS::ActorFileLibrary::Get().Values("PropActor"));
}

TEST(ClassObjSavedFields, OptionalColorIsSavedAsNullOrFourNumbers)
{
    const NS::ReflectionInfo& info = *OptionalColorValue::StaticReflection();
    ASSERT_EQ(info.fieldCount, 1u);
    EXPECT_EQ(info.fields[0].type, NS::FieldType::OptionalColor);

    OptionalColorValue value;
    EXPECT_TRUE(NS::SerializeReflectedFields(&value, info)["色"].is_null());

    EXPECT_EQ(NS::ApplyReflectedFields(&value, info, nlohmann::json{{"色", {0.25f, 0.5f, 0.75f, 1.0f}}}), 0u);
    ASSERT_TRUE(value.tint.has_value());
    EXPECT_EQ(*value.tint, NS::Color(0.25f, 0.5f, 0.75f, 1.0f));
    EXPECT_EQ(NS::SerializeReflectedFields(&value, info)["色"], (nlohmann::json{0.25f, 0.5f, 0.75f, 1.0f}));

    // 長さ違いと数でない成分は、入っている値を変えない
    const nlohmann::json broken[] = {nlohmann::json{0.1f, 0.2f, 0.3f}, nlohmann::json{0.1f, "a", 0.3f, 1.0f},
                                     nlohmann::json("red")};
    for (const nlohmann::json& entry : broken)
    {
        NS::ApplyReflectedFields(&value, info, nlohmann::json{{"色", entry}});
        ASSERT_TRUE(value.tint.has_value()) << entry.dump();
        EXPECT_EQ(*value.tint, NS::Color(0.25f, 0.5f, 0.75f, 1.0f)) << entry.dump();
    }

    NS::ApplyReflectedFields(&value, info, nlohmann::json{{"色", nullptr}});
    EXPECT_FALSE(value.tint.has_value());

    // 空の間も壊れた値は空のまま残す
    NS::ApplyReflectedFields(&value, info, nlohmann::json{{"色", {0.1f, 0.2f, 0.3f}}});
    EXPECT_FALSE(value.tint.has_value());
}

TEST(ClassObjSavedFields, EnabledKeyIsReadLikeAnyUnknownField)
{
    const std::unique_ptr<PlayerActor> player = NS::CreateActor<PlayerActor>();
    EXPECT_EQ(NS::ApplyJsonFields(player->Input(), nlohmann::json{{"enabled", false}}), 1u);
}
