#include "Game/Level/WaterActor.h"
#include "Game/Level/WaterSwell.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Gfx/Render/RenderBounds.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

// 使わない当たりを作らず、深度とキューブマップを読める段で描く
TEST(WaterActor, HasNoTerrainCollisionAndDrawsAfterTheSky)
{
    const std::unique_ptr<GL::Level::WaterActor> water = NS::CreateActor<GL::Level::WaterActor>();
    EXPECT_EQ(water->CollisionClassObj(), nullptr);
    ASSERT_NE(water->ModelClassObj(), nullptr);
    EXPECT_EQ(water->ModelClassObj()->Bounds(NS::Matrix::Identity).pass, NS::Gfx::RenderPass::AfterSky);
}

// 頂点シェーダーが動かす波の分をメッシュの範囲は含まないので、水面の近くで上を向いても間引かない
TEST(WaterActor, IsNeverCulled)
{
    const std::unique_ptr<GL::Level::WaterActor> water = NS::CreateActor<GL::Level::WaterActor>();
    const NS::AABB bounds = water->ModelClassObj()->Bounds(NS::Matrix::Identity).bounds;
    EXPECT_GE(bounds.Extents.x, 100000.0f);
    EXPECT_GE(bounds.Extents.y, 100000.0f);
    EXPECT_GE(bounds.Extents.z, 100000.0f);
}

// うねりの高さは、水のシェーダー (Shaders/water.hlsli の WaterSwell) と同じ式で出す。
// 波 1 本・向き +x・波長 4 m なら、x = 1 m で位相が 4 分の 1 周になり、高さはうねりの高さそのもの
TEST(WaterSwell, HeightFollowsTheSineOfThePhaseAlongTheDirection)
{
    GL::Level::WaterSwell swell;
    swell.height = 2.0f;
    swell.speed = 1.0f;
    swell.gravity = 9.8f;
    swell.directionX = 1.0f;
    swell.waves[0] = GL::Level::WaterSwellWave{.angle = 0.0f, .wavelength = 4.0f, .weight = 1.0f};

    EXPECT_NEAR(GL::Level::WaterSwellHeight(swell, 0.0f, 0.0f, 0.0f), 0.0f, 1e-4f);
    EXPECT_NEAR(GL::Level::WaterSwellHeight(swell, 1.0f, 0.0f, 0.0f), 2.0f, 1e-4f);
    EXPECT_NEAR(GL::Level::WaterSwellHeight(swell, 3.0f, 0.0f, 0.0f), -2.0f, 1e-4f);
    // 波の向きに直角な方向へ動いても、高さは変わらない
    EXPECT_NEAR(GL::Level::WaterSwellHeight(swell, 1.0f, 7.0f, 0.0f), 2.0f, 1e-4f);
    // 時間が進むと波が +x へ進む。角速度は sqrt(重力 × 波数) × 速さ
    const float omega = std::sqrt(9.8f * NS::k_Pi * 2.0f / 4.0f);
    EXPECT_NEAR(GL::Level::WaterSwellHeight(swell, 1.0f, 0.0f, NS::k_Pi * 0.5f / omega), 0.0f, 1e-3f);
}

// 値の無いうねりは平らな水面
TEST(WaterSwell, EmptySwellIsFlat)
{
    EXPECT_FLOAT_EQ(GL::Level::WaterSwellHeight(GL::Level::WaterSwell{}, 12.0f, -5.0f, 3.0f), 0.0f);
    EXPECT_FLOAT_EQ(GL::Level::ReadWaterSwell(nullptr).height, 0.0f);
}

// .mat に無い名前は 0 として読むので、出荷の水のマテリアルは、うねりの名前を全部書いている
TEST(WaterSwell, ShippedWaterMaterialWritesEverySwellValue)
{
    const NS::Material* material = NS::AssetManager::Get().LoadMaterial("Assets/Materials/water.mat");
    ASSERT_NE(material, nullptr);
    const NS::MaterialParamValues& params = material->File().params;
    for (const char* name :
         {"swellHeight", "swellSpeed", "swellGravity", "swellDirection", "swellWaveA", "swellWaveB", "swellWaveC"})
    {
        EXPECT_TRUE(params.contains(name)) << name;
    }
    const GL::Level::WaterSwell swell = GL::Level::ReadWaterSwell(material);
    EXPECT_FLOAT_EQ(swell.height, 2.4f);
    EXPECT_FLOAT_EQ(swell.gravity, 9.8f);
    EXPECT_FLOAT_EQ(swell.directionX, 0.8f);
    EXPECT_FLOAT_EQ(swell.directionZ, 0.6f);
    EXPECT_FLOAT_EQ(swell.waves[0].wavelength, 113.0f);
    EXPECT_FLOAT_EQ(swell.waves[1].angle, 0.45f);
    EXPECT_FLOAT_EQ(swell.waves[2].weight, 0.15f);
    // 3 本の割合の合計が 1 なので、うねりの高さより高くならない
    EXPECT_LE(std::abs(GL::Level::WaterSwellHeight(swell, 31.0f, -7.0f, 12.5f)), swell.height + 1e-4f);
}
