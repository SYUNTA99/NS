#include <Game/Level/BreakableComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Reflection/TypeRegistry.h>
#include <gtest/gtest.h>
#include <limits>
#include <string_view>

namespace
{
    using NS::Game::Level::BreakableComponent;
    using NS::Object::Component;
    using NS::Object::FieldDesc;
    using NS::Object::GameObject;
    using NS::Object::ReflectionInfo;

    // 欄名から 1 欄を引く。Inspector と .scene が値を書き込むのと同じ経路
    const FieldDesc* FindLabel(const Component& comp, std::string_view label)
    {
        const ReflectionInfo* info = comp.GetReflection();
        if (info == nullptr)
            return nullptr;
        return NS::Object::FindField(info, label);
    }
} // namespace

TEST(BreakableComponentTest, DefaultsAreOne)
{
    GameObject obj;
    BreakableComponent* breakable = obj.AddComponent<BreakableComponent>();
    ASSERT_NE(breakable, nullptr);

    EXPECT_FLOAT_EQ(breakable->Mass(), 1.0f);
    EXPECT_FLOAT_EQ(breakable->Toughness(), 1.0f);
}

TEST(BreakableComponentTest, SetterStoresValue)
{
    GameObject obj;
    BreakableComponent* breakable = obj.AddComponent<BreakableComponent>();
    ASSERT_NE(breakable, nullptr);

    breakable->SetMass(3.0f);
    breakable->SetToughness(2.5f);
    EXPECT_FLOAT_EQ(breakable->Mass(), 3.0f);
    EXPECT_FLOAT_EQ(breakable->Toughness(), 2.5f);
}

// 質量 0 は飛距離の計算で割れなくなる。手編集の .scene から来ても下限で止める
TEST(BreakableComponentTest, MassClampsToLowerBound)
{
    GameObject obj;
    BreakableComponent* breakable = obj.AddComponent<BreakableComponent>();
    ASSERT_NE(breakable, nullptr);

    breakable->SetMass(-5.0f);
    EXPECT_FLOAT_EQ(breakable->Mass(), 0.01f);

    breakable->SetMass(0.0f);
    EXPECT_FLOAT_EQ(breakable->Mass(), 0.01f);
}

// 耐久 0 は最低の勢いでも壊れる的を置くための値なので許す。負だけ 0 へ丸める
TEST(BreakableComponentTest, ToughnessClampsNegativeToZero)
{
    GameObject obj;
    BreakableComponent* breakable = obj.AddComponent<BreakableComponent>();
    ASSERT_NE(breakable, nullptr);

    breakable->SetToughness(-1.0f);
    EXPECT_FLOAT_EQ(breakable->Toughness(), 0.0f);

    breakable->SetToughness(0.0f);
    EXPECT_FLOAT_EQ(breakable->Toughness(), 0.0f);
}

// 非有限値は丸めずに捨てる。前の値が残る
TEST(BreakableComponentTest, NonFiniteValuesAreIgnored)
{
    GameObject obj;
    BreakableComponent* breakable = obj.AddComponent<BreakableComponent>();
    ASSERT_NE(breakable, nullptr);

    breakable->SetMass(4.0f);
    breakable->SetToughness(6.0f);

    breakable->SetMass(std::numeric_limits<float>::quiet_NaN());
    breakable->SetMass(std::numeric_limits<float>::infinity());
    breakable->SetToughness(std::numeric_limits<float>::quiet_NaN());
    breakable->SetToughness(-std::numeric_limits<float>::infinity());

    EXPECT_FLOAT_EQ(breakable->Mass(), 4.0f);
    EXPECT_FLOAT_EQ(breakable->Toughness(), 6.0f);
}

// 登録が落ちると Inspector の追加一覧に並ばず、.scene からも戻らない
TEST(BreakableComponentTest, IsCreatableFromTypeName)
{
    GameObject obj;
    Component* comp = NS::Object::CreateComponent("BreakableComponent", obj);
    ASSERT_NE(comp, nullptr);
    ASSERT_NE(comp->GetReflection(), nullptr);
    EXPECT_STREQ(comp->GetReflection()->typeName, "BreakableComponent");
    EXPECT_EQ(obj.FindComponent<BreakableComponent>(), comp);
}

// 欄名は .scene のキーそのもの。変えると保存済みレベルのその値だけが静かに既定へ戻る
TEST(BreakableComponentTest, ReflectionLabelsAreMassAndToughness)
{
    GameObject obj;
    BreakableComponent* breakable = obj.AddComponent<BreakableComponent>();
    ASSERT_NE(breakable, nullptr);

    const FieldDesc* mass = FindLabel(*breakable, "質量");
    const FieldDesc* toughness = FindLabel(*breakable, "耐久");
    ASSERT_NE(mass, nullptr);
    ASSERT_NE(toughness, nullptr);
    EXPECT_EQ(mass->type, NS::Object::FieldType::Float);
    EXPECT_EQ(toughness->type, NS::Object::FieldType::Float);

    // 欄は setter 経由。Inspector のドラッグで下限を越えて引いても JSON と同じ検証を通る
    const float belowBound = -2.0f;
    mass->set(breakable, &belowBound);
    toughness->set(breakable, &belowBound);
    EXPECT_FLOAT_EQ(breakable->Mass(), 0.01f);
    EXPECT_FLOAT_EQ(breakable->Toughness(), 0.0f);
}
