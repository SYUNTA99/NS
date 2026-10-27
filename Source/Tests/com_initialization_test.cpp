#include "NSlib/Core/Windows/ComInitialization.h"

#include <gtest/gtest.h>

#include <objbase.h>

namespace
{
    //! このスレッドで COM の呼び出しが通るか
    [[nodiscard]] bool IsComUsable() noexcept
    {
        APTTYPE type = APTTYPE_CURRENT;
        APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
        return SUCCEEDED(::CoGetApartmentType(&type, &qualifier));
    }
} // namespace

// test_main が先に初期化したスレッドで作っても、初期化できた扱いになる
TEST(ComInitializationTest, ValidOnAlreadyInitializedThread)
{
    const NS::OS::ComInitialization com;
    EXPECT_TRUE(com.IsValid());
}

// 入れ子で作って内側を壊しても、外側の初期化が残る
TEST(ComInitializationTest, NestedDestructionKeepsOuterInitialization)
{
    const NS::OS::ComInitialization outer;
    {
        const NS::OS::ComInitialization inner;
        EXPECT_TRUE(inner.IsValid());
    }
    EXPECT_TRUE(outer.IsValid());
    EXPECT_TRUE(IsComUsable());
}
