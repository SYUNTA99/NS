#include "NSlib/Object/Reflection/TypeRegistry.h"

#include <gtest/gtest.h>

TEST(NSlibLink, UnreferencedComponentsRemainRegistered)
{
    const NS::Obj::TypeRegistry::Entry* entry = NS::Obj::TypeRegistry::Get().Find("Model");
    ASSERT_NE(entry, nullptr);
    EXPECT_NE(entry->createDefault, nullptr);
}
