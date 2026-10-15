#include "NSlib/CoreObject/Reflection/TypeRegistry.h"

#include <gtest/gtest.h>

TEST(NSlibLink, UnreferencedSubObjectsRemainRegistered)
{
    const NS::TypeRegistry::Entry* entry = NS::TypeRegistry::Get().Find("Model");
    ASSERT_NE(entry, nullptr);
    EXPECT_NE(entry->createDefault, nullptr);
}
