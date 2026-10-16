#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Shader.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/Windows/Filesystem.h"

#include <gtest/gtest.h>

#include <string>

namespace
{
    std::string ReadStringField(const NS::Material& material, const char* name)
    {
        const NS::FieldDesc* field = NS::FindField(material.GetReflection(), name);
        if (field == nullptr || field->type != NS::FieldType::String)
        {
            ADD_FAILURE() << "文字列の欄が無い: " << name;
            return {};
        }
        std::string value;
        field->get(&material, &value);
        return value;
    }

    void WriteStringField(NS::Material& material, const char* name, const std::string& value)
    {
        const NS::FieldDesc* field = NS::FindField(material.GetReflection(), name);
        ASSERT_NE(field, nullptr) << name;
        field->set(&material, &value);
    }

    std::string ContentPath(const char* relative)
    {
        return NS::OS::FileSystem::Normalize(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), relative));
    }
} // namespace

TEST(MaterialFile, MaterialLoadedFromAPathHoldsTheValuesOfTheFile)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::Material* material = assets.LoadMaterial("Assets/Materials/ground_arrow_band.mat");
    ASSERT_NE(material, nullptr);
    EXPECT_EQ(ReadStringField(*material, "頂点シェーダー"), "Shaders/standard.vs.hlsl");
    EXPECT_EQ(ReadStringField(*material, "ピクセルシェーダー"), "Shaders/ground_arrow.ps.hlsl");
    EXPECT_EQ(ReadStringField(*material, "混ぜ方"), "Alpha");
    EXPECT_EQ(ReadStringField(*material, "テクスチャ 1"), "Assets/Textures/ground_arrow_band.png");
    EXPECT_EQ(ReadStringField(*material, "テクスチャ 2"), "");

    const NS::Material* stone = assets.LoadMaterial("Assets/Materials/stone.mat");
    ASSERT_NE(stone, nullptr);
    const NS::FieldDesc* colorField = NS::FindField(stone->GetReflection(), "基本の色");
    ASSERT_NE(colorField, nullptr);
    ASSERT_EQ(colorField->type, NS::FieldType::Color);
    NS::Color color;
    colorField->get(stone, &color);
    EXPECT_FLOAT_EQ(color.x, 0.6f);
    EXPECT_FLOAT_EQ(color.y, 0.6f);
    EXPECT_FLOAT_EQ(color.z, 0.65f);
    EXPECT_EQ(ReadStringField(*stone, "混ぜ方"), "Opaque");
}

TEST(MaterialFile, MaterialPointsAtTheShaderOfItsPath)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::Material* band = assets.LoadMaterial("Assets/Materials/ground_arrow_band.mat");
    const NS::Material* stone = assets.LoadMaterial("Assets/Materials/stone.mat");
    ASSERT_NE(band, nullptr);
    ASSERT_NE(stone, nullptr);
    ASSERT_NE(band->VertexShader(), nullptr);
    EXPECT_EQ(band->VertexShader()->Path(), ContentPath("Shaders/standard.vs.hlsl"));
    EXPECT_EQ(band->PixelShader()->Path(), ContentPath("Shaders/ground_arrow.ps.hlsl"));
    EXPECT_EQ(band->VertexShader(), stone->VertexShader());
}

TEST(MaterialFile, SharedMaterialShowsTheValuesItWasBuiltFrom)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    assets.RegisterSharedMaterials();
    const NS::Material* water = assets.LoadMaterial("water");
    ASSERT_FALSE(water->IsEmpty());
    EXPECT_EQ(ReadStringField(*water, "ピクセルシェーダー"), "Shaders/water.ps.hlsl");
    EXPECT_EQ(ReadStringField(*water, "混ぜ方"), "Alpha");
    EXPECT_EQ(ReadStringField(*water, "テクスチャ 1"), "Assets/Textures/cube_test.png");
}

TEST(MaterialFile, BlendFieldKeepsItsValueForAnUnknownSpelling)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    NS::Material* material = assets.LoadMaterial("Assets/Materials/ground_arrow_band.mat");
    ASSERT_NE(material, nullptr);
    WriteStringField(*material, "混ぜ方", "alpha");
    EXPECT_EQ(ReadStringField(*material, "混ぜ方"), "Alpha");
    WriteStringField(*material, "混ぜ方", "Additive");
    EXPECT_EQ(ReadStringField(*material, "混ぜ方"), "Additive");
}
