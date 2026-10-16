#include "NSlib/CoreObject/Reflection/TypeRegistry.h"

#include <gtest/gtest.h>

TEST(NSlibLink, UnreferencedActorsRemainRegistered)
{
    const NS::TypeRegistry::Entry* entry = NS::TypeRegistry::Get().Find("LightActor");
    ASSERT_NE(entry, nullptr);
    EXPECT_NE(entry->create, nullptr);
}
