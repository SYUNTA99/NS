#include "Game/Level/PropActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/Reflection/Archetype.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <gtest/gtest.h>

#include <memory>

namespace
{
    void ExpectNoEnabledKey(const nlohmann::json& object)
    {
        const nlohmann::json::const_iterator subObjects = object.find("subObjects");
        ASSERT_NE(subObjects, object.end());
        for (nlohmann::json::const_iterator part = subObjects->begin(); part != subObjects->end(); ++part)
        {
            EXPECT_FALSE(part.value().contains("enabled")) << part.key();
        }
    }
} // namespace

TEST(SubObjectSavedFields, SavedJsonHasNoEnabledKey)
{
    const std::unique_ptr<PlayerActor> player = NS::CreateActor<PlayerActor>();
    ExpectNoEnabledKey(NS::ObjectToJson(*player));
    ExpectNoEnabledKey(NS::ExpandObjectJson(NS::MakePrototypeJson<PlayerActor>()));

    const std::unique_ptr<GL::Level::PropActor> prop = NS::CreateActor<GL::Level::PropActor>();
    ExpectNoEnabledKey(NS::ObjectToJson(*prop));
    ExpectNoEnabledKey(NS::ExpandObjectJson(NS::MakePrototypeJson<GL::Level::PropActor>()));
}

TEST(SubObjectSavedFields, EnabledKeyIsReadLikeAnyUnknownField)
{
    const std::unique_ptr<PlayerActor> player = NS::CreateActor<PlayerActor>();
    EXPECT_EQ(NS::ApplyJsonFields(player->Input(), nlohmann::json{{"enabled", false}}), 1u);
}
