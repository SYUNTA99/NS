#include <cstddef>
#include <gtest/gtest.h>
#include <Runtime/Math/Math.h>
#include <Runtime/Object/Component.h>
#include <Runtime/Object/Components/TransformComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Reflection/ObjectBuilder.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Scene/SceneData.h>

namespace
{
    using NS::Object::FieldDesc;
    using NS::Object::FindField;
    using NS::Object::GameObject;
    using NS::Object::ObjectData;
    using NS::Object::ReflectionInfo;
    using NS::Object::TransformComponent;

    // live 側の TransformComponent の数をリフレクション型名で数える
    [[nodiscard]] std::size_t CountTransformComponents(const GameObject& obj)
    {
        std::size_t count = 0;
        for (const NS::Object::Component* comp : obj.Components())
        {
            if (comp == nullptr)
                continue;
            const ReflectionInfo* info = comp->GetReflection();
            if (info != nullptr && NS::Object::k_TransformTypeName == info->typeName)
                ++count;
        }
        return count;
    }

    // データ側の TransformComponent エントリの数
    [[nodiscard]] std::size_t CountTransformEntries(const ObjectData& object)
    {
        std::size_t count = 0;
        for (const nlohmann::json& entry : object.components)
        {
            if (NS::Object::ComponentEntryType(entry) == NS::Object::k_TransformTypeName)
                ++count;
        }
        return count;
    }

    // components から TransformComponent のエントリを全て取り除く
    void EraseTransformEntries(ObjectData& object)
    {
        nlohmann::json kept = nlohmann::json::array();
        for (const nlohmann::json& entry : object.components)
        {
            if (NS::Object::ComponentEntryType(entry) != NS::Object::k_TransformTypeName)
                kept.push_back(entry);
        }
        object.components = std::move(kept);
    }

    // 型名だけの component 1 件を持つ最小のデータ
    [[nodiscard]] ObjectData MakeMinimalObject()
    {
        ObjectData object{};
        object.components = nlohmann::json::array();
        object.components.push_back(NS::Object::MakeComponentEntry("MeshRendererComponent"));
        return object;
    }
} // namespace

TEST(TransformComponentTest, ReflectsPositionRotationScale)
{
    GameObject obj;
    auto* tc = obj.FindComponent<TransformComponent>();
    ASSERT_NE(tc, nullptr);
    const ReflectionInfo* info = tc->GetReflection();
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->fieldCount, 3u);
    EXPECT_NE(FindField(info, "Position"), nullptr);
    EXPECT_NE(FindField(info, "Rotation (deg)"), nullptr);
    EXPECT_NE(FindField(info, "Scale"), nullptr);
}

// リフレクション set が owner の root Transform を動かし、 root を直接動かすとリフレクション get が追う
TEST(TransformComponentTest, PositionReflectionBridgesOwnerRootTransform)
{
    GameObject obj;
    auto* tc = obj.FindComponent<TransformComponent>();
    ASSERT_NE(tc, nullptr);
    const FieldDesc* pos = FindField(tc->GetReflection(), "Position");
    ASSERT_NE(pos, nullptr);

    NS::Math::Vector3 set{3.0f, 4.0f, 5.0f};
    pos->set(tc, &set);
    EXPECT_FLOAT_EQ(obj.Root().Position().x, 3.0f);
    EXPECT_FLOAT_EQ(obj.Root().Position().y, 4.0f);
    EXPECT_FLOAT_EQ(obj.Root().Position().z, 5.0f);

    obj.Root().SetPosition(NS::Math::Vector3{-1.0f, -2.0f, -3.0f});
    NS::Math::Vector3 got{};
    pos->get(tc, &got);
    EXPECT_FLOAT_EQ(got.x, -1.0f);
    EXPECT_FLOAT_EQ(got.y, -2.0f);
    EXPECT_FLOAT_EQ(got.z, -3.0f);
}

TEST(TransformComponentTest, ScaleReflectionRoundTrips)
{
    GameObject obj;
    auto* tc = obj.FindComponent<TransformComponent>();
    ASSERT_NE(tc, nullptr);
    const FieldDesc* scale = FindField(tc->GetReflection(), "Scale");
    ASSERT_NE(scale, nullptr);

    NS::Math::Vector3 set{2.0f, 3.0f, 4.0f};
    scale->set(tc, &set);
    NS::Math::Vector3 got{};
    scale->get(tc, &got);
    EXPECT_FLOAT_EQ(got.x, 2.0f);
    EXPECT_FLOAT_EQ(got.y, 3.0f);
    EXPECT_FLOAT_EQ(got.z, 4.0f);
    EXPECT_FLOAT_EQ(obj.Root().Scale().y, 3.0f);
}

// 回転は Euler 度で読み書きし、 往復で一致する (BoxCollider と同じ変換)
TEST(TransformComponentTest, RotationEulerDegreesRoundTrips)
{
    GameObject obj;
    auto* tc = obj.FindComponent<TransformComponent>();
    ASSERT_NE(tc, nullptr);
    const FieldDesc* rot = FindField(tc->GetReflection(), "Rotation (deg)");
    ASSERT_NE(rot, nullptr);

    NS::Math::Vector3 set{0.0f, 45.0f, 0.0f};
    rot->set(tc, &set);
    NS::Math::Vector3 got{};
    rot->get(tc, &got);
    EXPECT_NEAR(got.x, 0.0f, 1e-2f);
    EXPECT_NEAR(got.y, 45.0f, 1e-2f);
    EXPECT_NEAR(got.z, 0.0f, 1e-2f);
}

// 実体を自分で持つので、 owner に着いていなくても読み書きできる
TEST(TransformComponentTest, WithoutOwnerReadsAndWritesOwnTransform)
{
    TransformComponent orphan;
    EXPECT_FLOAT_EQ(orphan.Position().x, 0.0f);
    EXPECT_FLOAT_EQ(orphan.Scale().x, 1.0f);
    orphan.SetPosition(NS::Math::Vector3{9.0f, 9.0f, 9.0f});
    EXPECT_FLOAT_EQ(orphan.Position().x, 9.0f);
    EXPECT_FLOAT_EQ(orphan.Root().Position().x, 9.0f);
}

// どの GameObject も TransformComponent をちょうど 1 つ持ち、 Root() はその実体を指す
TEST(TransformComponentTest, EveryObjectCarriesExactlyOne)
{
    GameObject obj;
    EXPECT_EQ(CountTransformComponents(obj), std::size_t{1});

    auto* tc = obj.FindComponent<TransformComponent>();
    ASSERT_NE(tc, nullptr);
    EXPECT_EQ(&obj.Root(), &tc->Root());
}

// データに transform エントリが無くても GameObject の 1 つは残り、 pose は既定のまま
TEST(TransformComponentTest, DataWithoutTransformEntryKeepsOneAtDefaults)
{
    ObjectData object = MakeMinimalObject();
    ASSERT_EQ(CountTransformEntries(object), std::size_t{0});

    GameObject obj;
    NS::Object::ApplyObjectComponents(obj, object, {});

    EXPECT_EQ(CountTransformComponents(obj), std::size_t{1});
    EXPECT_FLOAT_EQ(obj.Root().Position().x, 0.0f);
    EXPECT_FLOAT_EQ(obj.Root().Position().y, 0.0f);
    EXPECT_FLOAT_EQ(obj.Root().Position().z, 0.0f);
    EXPECT_FLOAT_EQ(obj.Root().Scale().x, 1.0f);
    EXPECT_FLOAT_EQ(obj.Root().Scale().y, 1.0f);
    EXPECT_FLOAT_EQ(obj.Root().Scale().z, 1.0f);
}

// エントリを消せば効果も消える。 書き込んだ位置は組み直しで原点へ戻る
TEST(TransformComponentTest, ErasingTransformEntryDropsThePose)
{
    ObjectData object = MakeMinimalObject();
    NS::Object::SetObjectPosition(object, NS::Math::Vector3{5.0f, 6.0f, 7.0f});
    ASSERT_EQ(CountTransformEntries(object), std::size_t{1});

    GameObject placed;
    NS::Object::ApplyObjectComponents(placed, object, {});
    ASSERT_FLOAT_EQ(placed.Root().Position().y, 6.0f);

    EraseTransformEntries(object);
    ASSERT_EQ(CountTransformEntries(object), std::size_t{0});

    GameObject rebuilt;
    NS::Object::ApplyObjectComponents(rebuilt, object, {});
    EXPECT_EQ(CountTransformComponents(rebuilt), std::size_t{1});
    EXPECT_FLOAT_EQ(rebuilt.Root().Position().x, 0.0f);
    EXPECT_FLOAT_EQ(rebuilt.Root().Position().y, 0.0f);
    EXPECT_FLOAT_EQ(rebuilt.Root().Position().z, 0.0f);
}

// エントリを 2 つ書いても live は 1 つ。 値は先に書かれた方が勝ち、 保存も 1 件に戻る
TEST(TransformComponentTest, DuplicateEntriesBuildOnlyOne)
{
    ObjectData object = MakeMinimalObject();
    nlohmann::json first = NS::Object::MakeComponentEntry(NS::Object::k_TransformTypeName);
    NS::Object::SetField(first, "Position", NS::Math::Vector3{1.0f, 2.0f, 3.0f});
    nlohmann::json second = NS::Object::MakeComponentEntry(NS::Object::k_TransformTypeName);
    NS::Object::SetField(second, "Position", NS::Math::Vector3{-8.0f, -8.0f, -8.0f});
    object.components.push_back(std::move(first));
    object.components.push_back(std::move(second));
    ASSERT_EQ(CountTransformEntries(object), std::size_t{2});

    GameObject obj;
    NS::Object::ApplyObjectComponents(obj, object, {});

    EXPECT_EQ(CountTransformComponents(obj), std::size_t{1});
    EXPECT_FLOAT_EQ(obj.Root().Position().x, 1.0f);
    EXPECT_FLOAT_EQ(obj.Root().Position().y, 2.0f);
    EXPECT_FLOAT_EQ(obj.Root().Position().z, 3.0f);

    const ObjectData captured = NS::Object::CaptureObjectData(obj);
    EXPECT_EQ(CountTransformEntries(captured), std::size_t{1});
}
