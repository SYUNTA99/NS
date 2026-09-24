#include <Game/Level/Breakable.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Reflection/TypeRegistry.h>
#include <gtest/gtest.h>
#include <limits>
#include <string_view>

namespace
{
    using NS::Game::Level::Breakable;
    using NS::Obj::Component;
    using NS::Obj::FieldDesc;
    using NS::Obj::GameObject;
    using NS::Obj::ReflectionInfo;

    // 欄名から 1 欄を引く。Inspector と .scene が値を書き込むのと同じ経路
    const FieldDesc* FindLabel(const Component& comp, std::string_view label)
    {
        const ReflectionInfo* info = comp.GetReflection();
        if (info == nullptr)
            return nullptr;
        return NS::Obj::FindField(info, label);
    }
} // namespace

TEST(BreakableComponentTest, DefaultToughnessIsOne)
{
    GameObject obj;
    Breakable* breakable = obj.AddComponent<Breakable>();
    ASSERT_NE(breakable, nullptr);

    EXPECT_FLOAT_EQ(breakable->Toughness(), 1.0f);
}

TEST(BreakableComponentTest, SetterStoresValue)
{
    GameObject obj;
    Breakable* breakable = obj.AddComponent<Breakable>();
    ASSERT_NE(breakable, nullptr);

    breakable->SetToughness(2.5f);
    EXPECT_FLOAT_EQ(breakable->Toughness(), 2.5f);
}

// 非有限値は丸めずに捨てる。前の値が残る
TEST(BreakableComponentTest, NonFiniteValuesAreIgnored)
{
    GameObject obj;
    Breakable* breakable = obj.AddComponent<Breakable>();
    ASSERT_NE(breakable, nullptr);

    breakable->SetToughness(6.0f);

    breakable->SetToughness(std::numeric_limits<float>::quiet_NaN());
    breakable->SetToughness(-std::numeric_limits<float>::infinity());

    EXPECT_FLOAT_EQ(breakable->Toughness(), 6.0f);
}

// 登録が落ちると Inspector の追加一覧に並ばず、.scene からも戻らない
TEST(BreakableComponentTest, IsCreatableFromTypeName)
{
    GameObject obj;
    Component* comp = NS::Obj::CreateComponent("Breakable", obj);
    ASSERT_NE(comp, nullptr);
    ASSERT_NE(comp->GetReflection(), nullptr);
    EXPECT_STREQ(comp->GetReflection()->typeName, "Breakable");
    EXPECT_EQ(obj.FindComponent<Breakable>(), comp);
}

// 欄名は .scene のキーそのもの。変えると保存済みレベルのその値だけが静かに既定へ戻る
// 質量は RigidBody が持つ。Breakable にも欄があると、同じ重さを 2 か所で書くことになる
TEST(BreakableComponentTest, ReflectionLabelIsToughnessOnly)
{
    GameObject obj;
    Breakable* breakable = obj.AddComponent<Breakable>();
    ASSERT_NE(breakable, nullptr);

    const FieldDesc* toughness = FindLabel(*breakable, "耐久");
    ASSERT_NE(toughness, nullptr);
    EXPECT_EQ(toughness->type, NS::Obj::FieldType::Float);
    EXPECT_EQ(FindLabel(*breakable, "質量"), nullptr);

    // 欄は setter 経由。Inspector のドラッグも JSON の手編集も同じ検証を通る
    const float notANumber = std::numeric_limits<float>::quiet_NaN();
    toughness->set(breakable, &notANumber);
    EXPECT_FLOAT_EQ(breakable->Toughness(), 1.0f);
}
