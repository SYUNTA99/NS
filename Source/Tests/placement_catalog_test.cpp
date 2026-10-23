#include "Editor/PaletteTemplates.h"
#include "Editor/PlacementCatalog.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "TestActorFile.h"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

// エディタで置ける物の一覧と、パレットのブラシがそこから組まれることを縛る

TEST(PlacementCatalog, ActorFilesOfPlaceableClassesAppearByTheirName)
{
    // 置けるクラスの .actor は、地形の部品の 1 本を除いて .actor の名前で 1 つずつ並び、その .actor を指す
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    library.Reload();
    std::size_t listed = 0;
    for (const std::string& name : library.Names())
    {
        const NS::TypeRegistry::Entry* entry = NS::TypeRegistry::Get().Find(library.ClassNameOf(name));
        if (entry == nullptr || entry->label == nullptr || name == "MapPartsActor")
        {
            continue;
        }
        const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem(name);
        ASSERT_TRUE(item.has_value()) << name;
        EXPECT_EQ(NS::ActorJsonFile(item->prototype), name);
        ++listed;
    }
    EXPECT_GE(listed, 6u);
}

TEST(PlacementCatalog, ActorFileAddedLaterAppearsUnderItsName)
{
    // クラスと違う名前の .actor も、敷いた後の一覧に並ぶ
    const ScopedActorFile heavy{"HeavyProp", CodeValuesActorFile("PropActor")};
    const std::optional<NS::Editor::PlacementItem> item = NS::Editor::FindPlacementItem("HeavyProp");
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(NS::ActorJsonFile(item->prototype), "HeavyProp");
}

TEST(PlacementCatalog, ItemFoundBeforeActorFileIsAddedStaysReadable)
{
    // 引いた物は、後で .actor が増えて一覧が組み直されても、引いた時の中身のまま読める
    const std::optional<NS::Editor::PlacementItem> prop = NS::Editor::FindPlacementItem("PropActor");
    ASSERT_TRUE(prop.has_value());
    const ScopedActorFile heavy{"HeavyProp", CodeValuesActorFile("PropActor")};
    ASSERT_TRUE(NS::Editor::FindPlacementItem("HeavyProp").has_value());
    EXPECT_EQ(prop->label, "PropActor");
    EXPECT_EQ(NS::ActorJsonFile(prop->prototype), "PropActor");
}

TEST(PlacementCatalog, MapPartsIsSplitByMesh)
{
    // 地形の部品はクラスとしては 1 つで、見た目のメッシュごとに並ぶ。当たりはメッシュに付いて来る
    const std::optional<NS::Editor::PlacementItem> cube = NS::Editor::FindPlacementItem(NS::Editor::k_PartsCubeLabel);
    const std::optional<NS::Editor::PlacementItem> sphere = NS::Editor::FindPlacementItem("地形の部品 (球)");
    const std::optional<NS::Editor::PlacementItem> slope = NS::Editor::FindPlacementItem(NS::Editor::k_PartsSlopeLabel);
    ASSERT_TRUE(cube.has_value());
    ASSERT_TRUE(sphere.has_value());
    ASSERT_TRUE(slope.has_value());
    for (const NS::Editor::PlacementItem& item : {*cube, *sphere, *slope})
    {
        EXPECT_EQ(NS::ActorJsonFile(item.prototype), "MapPartsActor");
        // 個体のデータは部品を足せないので、ひな形も当たりの件を持たない
        EXPECT_EQ(NS::ClassObjFields(item.prototype, "BoxCollision"), nullptr);
    }
    const nlohmann::json* cubeMesh = NS::ClassObjFields(cube->prototype, "Model");
    const nlohmann::json* sphereMesh = NS::ClassObjFields(sphere->prototype, "Model");
    const nlohmann::json* slopeMesh = NS::ClassObjFields(slope->prototype, "Model");
    ASSERT_NE(cubeMesh, nullptr);
    ASSERT_NE(sphereMesh, nullptr);
    ASSERT_NE(slopeMesh, nullptr);
    EXPECT_EQ(NS::FieldString(*cubeMesh, "メッシュ", ""), "cube");
    EXPECT_EQ(NS::FieldString(*sphereMesh, "メッシュ", ""), "sphere");
    EXPECT_EQ(NS::FieldString(*slopeMesh, "メッシュ", ""), "wedge45");
    // 90 度ずつ回して意味があるのは箱と坂だけ
    EXPECT_TRUE(cube->rotatable);
    EXPECT_FALSE(sphere->rotatable);
    EXPECT_TRUE(slope->rotatable);
    // 形の決まらない素の「地形の部品」は並べない
    EXPECT_FALSE(NS::Editor::FindPlacementItem("地形の部品").has_value());
}

TEST(PlacementCatalog, SlopeAngleComesFromMesh)
{
    const std::optional<NS::Editor::PlacementItem> cube = NS::Editor::FindPlacementItem(NS::Editor::k_PartsCubeLabel);
    const std::optional<NS::Editor::PlacementItem> slope = NS::Editor::FindPlacementItem(NS::Editor::k_PartsSlopeLabel);
    ASSERT_TRUE(cube.has_value());
    ASSERT_TRUE(slope.has_value());
    EXPECT_LT(NS::Editor::PartsSlopeAngleDegrees(cube->prototype), 0.0f);
    EXPECT_FLOAT_EQ(NS::Editor::PartsSlopeAngleDegrees(slope->prototype), 45.0f);
}

TEST(PlacementCatalog, PaletteSlotsComeFromCatalog)
{
    const std::array<NS::Editor::PaletteTemplate, NS::Editor::k_PaletteSlotCount>& slots =
        NS::Editor::PaletteTemplateSlots();
    const std::optional<NS::Editor::PlacementItem> cube = NS::Editor::FindPlacementItem(NS::Editor::k_PartsCubeLabel);
    const std::optional<NS::Editor::PlacementItem> slope = NS::Editor::FindPlacementItem(NS::Editor::k_PartsSlopeLabel);
    const std::optional<NS::Editor::PlacementItem> goal = NS::Editor::FindPlacementItem("GoalActor");
    ASSERT_TRUE(cube.has_value());
    ASSERT_TRUE(slope.has_value());
    ASSERT_TRUE(goal.has_value());
    // 3 つのブラシはどれも置く物を持つ
    for (const NS::Editor::PaletteTemplate& slot : slots)
    {
        EXPECT_FALSE(NS::ActorJsonFile(slot.prototype).empty()) << slot.name;
    }
    EXPECT_EQ(slots[0].prototype, cube->prototype);
    EXPECT_EQ(slots[1].prototype, slope->prototype);
    EXPECT_EQ(slots[2].prototype, goal->prototype);
    EXPECT_TRUE(slots[0].rotatable);
    EXPECT_FALSE(slots[2].rotatable);
}

TEST(PlacementCatalog, MeshPartsUsesTheDroppedMesh)
{
    // メッシュ資産から置く部品は、描いた三角形そのもので当たる
    const nlohmann::json prototype = NS::Editor::MakeMeshPartsPrototype("Assets/Models/terrain.glb");
    EXPECT_EQ(NS::ActorJsonFile(prototype), "MapPartsActor");
    const nlohmann::json* renderer = NS::ClassObjFields(prototype, "Model");
    ASSERT_NE(renderer, nullptr);
    EXPECT_EQ(NS::FieldString(*renderer, "メッシュ", ""), "Assets/Models/terrain.glb");
    // 当たりは地形の部品のクラスが持つ MeshCollision。ひな形は当たりの件を足さない
    EXPECT_EQ(NS::ClassObjFields(prototype, "BoxCollision"), nullptr);
}
