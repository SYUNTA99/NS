#include <Runtime/Physics/detail/JoltRuntime.h>

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>

#include <gtest/gtest.h>

// PhysicsScene と CreateMeshShape の両方から呼ばれるので、 2 度目の呼び出しは登録済みの Factory を差し替えない
TEST(JoltRuntimeTest, SecondInitializationKeepsTheFactory)
{
    NS::Physics::detail::InitializeJoltRuntime();
    JPH::Factory* first = JPH::Factory::sInstance;
    NS::Physics::detail::InitializeJoltRuntime();

    ASSERT_NE(first, nullptr);
    EXPECT_EQ(JPH::Factory::sInstance, first);
}
