#include "Editor/PaletteTemplates.h"
#include "Editor/PlacementCatalog.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <gtest/gtest.h>

#include <array>
#include <string_view>

// エディタで置ける物の一覧と、パレットのブラシがそこから組まれることを縛る

TEST(PlacementCatalog, EveryPlaceableClassAppears)
{
    // 地形の部品以外の置けるクラスは、表示名そのままで 1 つずつ並ぶ
    for (const NS::Obj::TypeRegistry::Entry* entry : NS::Obj::PlaceableEntries())
    {
        if (std::string_view{entry->className} == "MapParts")
            continue;
        const NS::Editor::PlacementItem* item = NS::Editor::FindPlacementItem(entry->label);
        ASSERT_NE(item, nullptr) << entry->label;
        EXPECT_EQ(NS::Obj::ObjectJsonClass(item->prototype), std::string_view{entry->className});
    }
    // プレイヤーは置く一覧に出ない
    for (const NS::Editor::PlacementItem& item : NS::Editor::PlacementItems())
    {
        EXPECT_NE(NS::Obj::ObjectJsonClass(item.prototype), "Player") << item.label;
    }
}

TEST(PlacementCatalog, MapPartsIsSplitByCollisionShape)
{
    // 地形の部品はクラスとしては 1 つで、当たりの形ごとに並ぶ
    const NS::Editor::PlacementItem* cube = NS::Editor::FindPlacementItem(NS::Editor::k_PartsCubeLabel);
    const NS::Editor::PlacementItem* sphere = NS::Editor::FindPlacementItem("地形の部品 (球)");
    const NS::Editor::PlacementItem* slope = NS::Editor::FindPlacementItem(NS::Editor::k_PartsSlopeLabel);
    ASSERT_NE(cube, nullptr);
    ASSERT_NE(sphere, nullptr);
    ASSERT_NE(slope, nullptr);
    EXPECT_EQ(NS::Obj::ObjectJsonClass(cube->prototype), "MapParts");
    EXPECT_EQ(NS::Obj::ObjectJsonClass(sphere->prototype), "MapParts");
    EXPECT_EQ(NS::Obj::ObjectJsonClass(slope->prototype), "MapParts");
    EXPECT_NE(NS::Obj::FindComponentEntry(cube->prototype, "BoxCollider"), nullptr);
    EXPECT_NE(NS::Obj::FindComponentEntry(sphere->prototype, "SphereCollider"), nullptr);
    EXPECT_NE(NS::Obj::FindComponentEntry(slope->prototype, "SlopeCollider"), nullptr);
    // 90 度ずつ回して意味があるのは箱と坂だけ
    EXPECT_TRUE(cube->rotatable);
    EXPECT_FALSE(sphere->rotatable);
    EXPECT_TRUE(slope->rotatable);
    // 生の「地形の部品」は当たりが無いので並べない
    EXPECT_EQ(NS::Editor::FindPlacementItem("地形の部品"), nullptr);
}

TEST(PlacementCatalog, PaletteSlotsComeFromCatalog)
{
    const std::array<NS::Editor::PaletteTemplate, NS::Editor::k_PaletteSlotCount>& slots =
        NS::Editor::PaletteTemplateSlots();
    const NS::Editor::PlacementItem* cube = NS::Editor::FindPlacementItem(NS::Editor::k_PartsCubeLabel);
    const NS::Editor::PlacementItem* slope = NS::Editor::FindPlacementItem(NS::Editor::k_PartsSlopeLabel);
    const NS::Editor::PlacementItem* goal = NS::Editor::FindPlacementItem("ゴール");
    ASSERT_NE(cube, nullptr);
    ASSERT_NE(slope, nullptr);
    ASSERT_NE(goal, nullptr);
    EXPECT_EQ(slots[0].prototype, cube->prototype);
    EXPECT_EQ(slots[1].prototype, slope->prototype);
    EXPECT_EQ(slots[2].prototype, goal->prototype);
    EXPECT_TRUE(slots[0].rotatable);
    EXPECT_FALSE(slots[2].rotatable);
}

TEST(PlacementCatalog, MeshPartsCollidesWithItsOwnTriangles)
{
    // メッシュ資産から置く部品は、描いた三角形そのもので当たる
    const nlohmann::json prototype = NS::Editor::MakeMeshPartsPrototype("Assets/Models/terrain.glb");
    EXPECT_EQ(NS::Obj::ObjectJsonClass(prototype), "MapParts");
    const nlohmann::json* renderer = NS::Obj::FindComponentEntry(prototype, "MeshRenderer");
    ASSERT_NE(renderer, nullptr);
    EXPECT_EQ(NS::Obj::FieldString(*renderer, "メッシュ", ""), "Assets/Models/terrain.glb");
    EXPECT_NE(NS::Obj::FindComponentEntry(prototype, "MeshCollider"), nullptr);
    EXPECT_EQ(NS::Obj::FindComponentEntry(prototype, "BoxCollider"), nullptr);
}
