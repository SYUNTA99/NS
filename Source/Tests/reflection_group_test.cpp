#include "Game/Player/PlayerParams.h"
#include "NSlib/Object/Reflection/Reflection.h"

#include <gtest/gtest.h>

#include <string_view>

namespace
{
    // 見出しの前の欄・空の見出し・続く見出しを 1 つずつ持つ値型
    struct GroupedValue
    {
        float loose = 0.0f;
        float jumpSpeed = 1.0f;
        float jumpGravity = 2.0f;
        int runSpeed = 3;

        NS_REFLECT_BEGIN(GroupedValue, void)
        NS_REFLECT_FIELD(loose, "見出しの前の欄")
        NS_REFLECT_GROUP("ジャンプ")
        NS_REFLECT_FIELD(jumpSpeed, "跳ぶ速さ")
        NS_REFLECT_FIELD(jumpGravity, "跳ぶ重力")
        NS_REFLECT_GROUP("空の見出し")
        NS_REFLECT_GROUP("走り")
        NS_REFLECT_FIELD(runSpeed, "走る速さ")
        NS_REFLECT_END_VALUE()
    };
} // namespace

// 欄の配列には見出しが混ざらない。保存と読み込みは欄の配列だけを回すので、見出しが保存の鍵にならない
TEST(ReflectionGroup, FieldsExcludeGroups)
{
    const NS::Obj::ReflectionInfo* info = GroupedValue::StaticReflection();
    ASSERT_EQ(info->fieldCount, 4u);
    EXPECT_EQ(std::string_view{info->fields[0].name}, "見出しの前の欄");
    EXPECT_EQ(std::string_view{info->fields[1].name}, "跳ぶ速さ");
    EXPECT_EQ(std::string_view{info->fields[2].name}, "跳ぶ重力");
    EXPECT_EQ(std::string_view{info->fields[3].name}, "走る速さ");
}

// 見出しは宣言の並びのまま、次に来る欄の番号から始まる。欄を持たない見出しも並びに残る
TEST(ReflectionGroup, GroupsStartAtTheFollowingField)
{
    const NS::Obj::ReflectionInfo* info = GroupedValue::StaticReflection();
    ASSERT_EQ(info->groupCount, 3u);
    EXPECT_EQ(std::string_view{info->groups[0].name}, "ジャンプ");
    EXPECT_EQ(info->groups[0].firstField, 1u);
    EXPECT_EQ(std::string_view{info->groups[1].name}, "空の見出し");
    EXPECT_EQ(info->groups[1].firstField, 3u);
    EXPECT_EQ(std::string_view{info->groups[2].name}, "走り");
    EXPECT_EQ(info->groups[2].firstField, 3u);
}

// 見出しの名前では欄を引けず、欄は見出しを挟んでも名前で引けて読み書きできる
TEST(ReflectionGroup, FindFieldSeesOnlyFields)
{
    const NS::Obj::ReflectionInfo* info = GroupedValue::StaticReflection();
    EXPECT_EQ(NS::Obj::FindField(info, "ジャンプ"), nullptr);

    const NS::Obj::FieldDesc* field = NS::Obj::FindField(info, "走る速さ");
    ASSERT_NE(field, nullptr);
    GroupedValue value;
    int read = 0;
    field->get(&value, &read);
    EXPECT_EQ(read, 3);
    const int written = 7;
    field->set(&value, &written);
    EXPECT_EQ(value.runSpeed, 7);
}

// 自機の調整値は先頭の欄から見出しの下にあり、どの見出しも欄を 1 つ以上持つ
TEST(ReflectionGroup, PlayerParamsFieldsAllSitUnderAGroup)
{
    const NS::Obj::ReflectionInfo* info = GL::Player::PlayerParams::StaticReflection();
    ASSERT_GE(info->groupCount, 2u);
    EXPECT_EQ(info->groups[0].firstField, 0u);
    for (std::size_t i = 0; i < info->groupCount; ++i)
    {
        std::size_t end = info->fieldCount;
        if (i + 1 < info->groupCount)
        {
            end = info->groups[i + 1].firstField;
        }
        EXPECT_LT(info->groups[i].firstField, end) << info->groups[i].name;
    }
}
