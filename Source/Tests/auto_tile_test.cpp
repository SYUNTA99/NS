#include "Game/Blocks/AutoTile.h"
#include "Game/Level/LevelData.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>

namespace LevelNs = NS::Game::Level;
namespace BlocksNs = NS::Game::Blocks;

namespace
{
    // 視覚キーは MeshRenderer の "Mesh" 値とマテリアル添字で決まる。 連結判定をその両者だけで検証する
    LevelNs::ObjectInstance MakeVisual(
        std::int16_t x, std::int16_t y, std::int16_t z, const char* mesh, int materialIndex = -1)
    {
        LevelNs::ObjectInstance object{};
        object.flags = LevelNs::kObjectFlagGridAligned;
        object.positionX = static_cast<float>(x);
        object.positionY = static_cast<float>(y);
        object.positionZ = static_cast<float>(z);
        object.materialIndex = static_cast<std::int16_t>(materialIndex);
        LevelNs::ComponentData renderer;
        renderer.typeName = "MeshRendererComponent";
        renderer.fields.push_back(LevelNs::FieldValue{"Mesh", std::string(mesh)});
        object.components.push_back(std::move(renderer));
        return object;
    }
} // namespace

TEST(AutoTileTest, IsolatedBlockHasZeroMask)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeVisual(0, 0, 0, "cube"));
    auto m = BlocksNs::ComputeNeighborMask(lv, 0, 0, 0);
    EXPECT_EQ(m, 0u);
}

TEST(AutoTileTest, AllSixNeighborsSetAllBits)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeVisual(0, 0, 0, "cube"));
    lv.objects.push_back(MakeVisual(+1, 0, 0, "cube"));
    lv.objects.push_back(MakeVisual(-1, 0, 0, "cube"));
    lv.objects.push_back(MakeVisual(0, +1, 0, "cube"));
    lv.objects.push_back(MakeVisual(0, -1, 0, "cube"));
    lv.objects.push_back(MakeVisual(0, 0, +1, "cube"));
    lv.objects.push_back(MakeVisual(0, 0, -1, "cube"));
    auto m = BlocksNs::ComputeNeighborMask(lv, 0, 0, 0);
    EXPECT_EQ(m, 0b00111111u);
}

TEST(AutoTileTest, OnlyPlusXNeighborSetsBit0)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeVisual(0, 0, 0, "cube"));
    lv.objects.push_back(MakeVisual(+1, 0, 0, "cube"));
    auto m = BlocksNs::ComputeNeighborMask(lv, 0, 0, 0);
    EXPECT_EQ(m, 0b00000001u);
}

TEST(AutoTileTest, SameVisualIdentityNeighborConnects)
{
    // 同じメッシュ参照 + マテリアルの隣接は連結する。 同メッシュ (wedge45) 同士で確認する
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeVisual(0, 0, 0, "wedge45"));
    lv.objects.push_back(MakeVisual(+1, 0, 0, "wedge45"));
    auto m = BlocksNs::ComputeNeighborMask(lv, 0, 0, 0);
    EXPECT_EQ(m, 0b00000001u);
}

TEST(AutoTileTest, DifferentMaterialNeighborDoesNotConnect)
{
    // メッシュは同じでもマテリアル添字が違えば視覚が異なるので連結しない
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeVisual(0, 0, 0, "cube"));
    lv.objects.push_back(MakeVisual(+1, 0, 0, "cube", 7));
    auto m = BlocksNs::ComputeNeighborMask(lv, 0, 0, 0);
    EXPECT_EQ(m, 0u);
}

TEST(AutoTileTest, DifferentVisualIdentityNeighborDoesNotSetBit)
{
    // cube の隣に wedge を置くとメッシュが異なるので連結しない
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeVisual(0, 0, 0, "cube"));
    lv.objects.push_back(MakeVisual(+1, 0, 0, "wedge45"));
    auto m = BlocksNs::ComputeNeighborMask(lv, 0, 0, 0);
    EXPECT_EQ(m, 0u);
}

TEST(AutoTileTest, MixedNeighborsConnectOnlySameVisual)
{
    // 視覚が一致する隣だけ bit が立つ。 別メッシュ (wedge) や別マテリアルの cube は連結しない
    LevelNs::LevelData lv;
    lv.objects.push_back(MakeVisual(0, 0, 0, "cube"));
    lv.objects.push_back(MakeVisual(+1, 0, 0, "cube"));    // 同一視覚 → bit0
    lv.objects.push_back(MakeVisual(-1, 0, 0, "wedge45")); // 別メッシュ → 立たない
    lv.objects.push_back(MakeVisual(0, +1, 0, "cube", 3)); // 別マテリアル → 立たない
    lv.objects.push_back(MakeVisual(0, 0, +1, "cube"));    // 同一視覚 → bit4
    auto m = BlocksNs::ComputeNeighborMask(lv, 0, 0, 0);
    EXPECT_EQ(m, 0b00010001u);
}

TEST(AutoTileTest, SetSpawnMarkerWritesCoordinates)
{
    LevelNs::LevelData lv;
    BlocksNs::SetSpawnMarker(lv, 10, 2, -5);
    EXPECT_EQ(lv.spawnX, 10);
    EXPECT_EQ(lv.spawnY, 2);
    EXPECT_EQ(lv.spawnZ, -5);
}

TEST(AutoTileTest, SetSpawnMarkerOverwritesPreviousValue)
{
    LevelNs::LevelData lv;
    BlocksNs::SetSpawnMarker(lv, 1, 1, 1);
    BlocksNs::SetSpawnMarker(lv, -3, 0, 7);
    EXPECT_EQ(lv.spawnX, -3);
    EXPECT_EQ(lv.spawnY, 0);
    EXPECT_EQ(lv.spawnZ, 7);
}
