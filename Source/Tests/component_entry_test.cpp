#include "NSlib/Object/Reflection/ComponentEntry.h"

#include <gtest/gtest.h>

TEST(ComponentEntry, ReadVector3RejectsBrokenArraysWithoutWriting)
{
    NS::Vector3 out{9.0f, 9.0f, 9.0f};
    EXPECT_FALSE(NS::Obj::ReadVector3(nlohmann::json::array({1.0f, 2.0f}), out));
    EXPECT_FALSE(NS::Obj::ReadVector3(nlohmann::json::array({1.0f, "a", 3.0f}), out));
    EXPECT_FALSE(NS::Obj::ReadVector3(nlohmann::json::object({{"x", 1.0f}}), out));
    EXPECT_EQ(out, (NS::Vector3{9.0f, 9.0f, 9.0f}));

    ASSERT_TRUE(NS::Obj::ReadVector3(nlohmann::json::array({1.0f, 2.0f, 3.0f}), out));
    EXPECT_EQ(out, (NS::Vector3{1.0f, 2.0f, 3.0f}));
}

TEST(ComponentEntry, ReadQuaternionRejectsBrokenArraysWithoutWriting)
{
    NS::Quaternion out{9.0f, 9.0f, 9.0f, 9.0f};
    EXPECT_FALSE(NS::Obj::ReadQuaternion(nlohmann::json::array({1.0f, 2.0f, 3.0f}), out));
    EXPECT_FALSE(NS::Obj::ReadQuaternion(nlohmann::json::array({1.0f, 2.0f, nullptr, 4.0f}), out));
    EXPECT_EQ(out, (NS::Quaternion{9.0f, 9.0f, 9.0f, 9.0f}));

    ASSERT_TRUE(NS::Obj::ReadQuaternion(nlohmann::json::array({0.0f, 0.0f, 0.0f, 1.0f}), out));
    EXPECT_EQ(out, NS::Quaternion::Identity);
}
