#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Shader.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/Gfx/D3DMaterial.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/Window.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <limits>
#include <optional>
#include <string>
#include <vector>

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

TEST(MaterialFile, PlayerAndShadowFilesHoldTheValuesTheyWereBuiltFrom)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::Material* shadow = assets.LoadMaterial("Assets/Materials/shadow.mat");
    ASSERT_FALSE(shadow->IsEmpty());
    EXPECT_EQ(ReadStringField(*shadow, "頂点シェーダー"), "Shaders/standard.vs.hlsl");
    EXPECT_EQ(ReadStringField(*shadow, "ピクセルシェーダー"), "Shaders/shadow.ps.hlsl");
    EXPECT_EQ(ReadStringField(*shadow, "混ぜ方"), "Alpha");
    EXPECT_EQ(ReadStringField(*shadow, "テクスチャ 1"), "");
    const NS::Material* player = assets.LoadMaterial("Assets/Materials/player.mat");
    ASSERT_FALSE(player->IsEmpty());
    EXPECT_EQ(ReadStringField(*player, "頂点シェーダー"), "Shaders/standard.vs.hlsl");
    EXPECT_EQ(ReadStringField(*player, "ピクセルシェーダー"), "Shaders/player.ps.hlsl");
    EXPECT_EQ(ReadStringField(*player, "混ぜ方"), "Opaque");
    EXPECT_EQ(ReadStringField(*player, "テクスチャ 1"), "Assets/Textures/cube_test.png");
    EXPECT_EQ(ReadStringField(*player, "テクスチャ 2"), "");
}

TEST(MaterialFile, BlendFieldKeepsItsValueForAnUnknownSpelling)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    NS::Material* material = assets.LoadMaterial("Assets/Materials/ground_arrow_band.mat");
    ASSERT_NE(material, nullptr);
    WriteStringField(*material, "混ぜ方", "alpha");
    EXPECT_EQ(ReadStringField(*material, "混ぜ方"), "Alpha");
    const std::optional<std::string> written = assets.MakeMaterialFileJson(*material);
    ASSERT_TRUE(written.has_value());
    NS::MaterialFileDesc reread{};
    std::string error;
    ASSERT_TRUE(NS::ParseMaterialJson(*written, reread, error)) << error;
    EXPECT_EQ(reread.blend, NS::Gfx::BlendMode::Alpha);

    WriteStringField(*material, "混ぜ方", "Additive");
    EXPECT_EQ(ReadStringField(*material, "混ぜ方"), "Additive");
}

TEST(MaterialFile, WritingWhatWasReadGivesTheSameFile)
{
    const std::vector<std::string> files =
        NS::OS::FileSystem::ListFiles(ContentPath("Assets/Materials"), ".mat");
    ASSERT_FALSE(files.empty());
    for (const std::string& file : files)
    {
        const std::optional<std::string> text = NS::OS::FileSystem::ReadAllText(file);
        ASSERT_TRUE(text.has_value()) << file;
        NS::MaterialFileDesc read{};
        std::string error;
        ASSERT_TRUE(NS::ParseMaterialJson(*text, read, error)) << file << ": " << error;

        const std::optional<std::string> written = NS::MakeMaterialJson(read);
        ASSERT_TRUE(written.has_value()) << file;
        EXPECT_EQ(*written, *text) << file;

        NS::MaterialFileDesc reread{};
        ASSERT_TRUE(NS::ParseMaterialJson(*written, reread, error)) << file << ": " << error;
        EXPECT_EQ(reread.vertexShader, read.vertexShader) << file;
        EXPECT_EQ(reread.pixelShader, read.pixelShader) << file;
        EXPECT_EQ(reread.textures, read.textures) << file;
        EXPECT_EQ(reread.baseColor, read.baseColor) << file;
        EXPECT_EQ(reread.blend, read.blend) << file;
    }
}

// Inspector の枠より多いテクスチャと、JSON に書けない色は書かない。書くと枠に出ない道や読めない .mat が残る
TEST(MaterialFile, ValuesTheFieldsCannotHoldAreNotWritten)
{
    NS::MaterialFileDesc file{.vertexShader = "Shaders/standard.vs.hlsl", .pixelShader = "Shaders/player.ps.hlsl"};
    file.textures.assign(4, "Assets/Textures/cube_test.png");
    EXPECT_TRUE(NS::MakeMaterialJson(file).has_value());
    file.textures.push_back("Assets/Textures/cube_test.png");
    EXPECT_FALSE(NS::MakeMaterialJson(file).has_value());

    file.textures.clear();
    file.baseColor.y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(NS::MakeMaterialJson(file).has_value());
}

TEST(MaterialFile, OnlyMaterialsReadFromAFileAreWritten)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    EXPECT_TRUE(assets.MakeMaterialFileJson(*assets.LoadMaterial("Assets/Materials/stone.mat")).has_value());
    EXPECT_FALSE(assets.MakeMaterialFileJson(*assets.LoadMaterial("")).has_value());
    const TestLogCapture log;
    EXPECT_FALSE(
        assets.MakeMaterialFileJson(*assets.LoadMaterial("Assets/Materials/__ns_missing__.mat")).has_value());
    EXPECT_EQ(log.ErrorCount(), 2);
}

// 装置の無い場では作り直せない。前の D3DMaterial を残し、同じ道は同じマテリアルを返す
TEST(MaterialFile, FailedRebuildKeepsTheDrawnMaterial)
{
    ASSERT_EQ(NS::Gfx::Gpu().device, nullptr);
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    NS::Material* material = assets.LoadMaterial("Assets/Materials/stone.mat");
    const NS::Gfx::D3DMaterial* drawn = material->D3D();
    ASSERT_NE(drawn, nullptr);

    WriteStringField(*material, "混ぜ方", "Additive");
    EXPECT_FALSE(assets.RebuildMaterial(*material));
    EXPECT_EQ(material->D3D(), drawn);
    EXPECT_EQ(assets.LoadMaterial("Assets/Materials/stone.mat"), material);
}

TEST(MaterialFile, RebuildDrawsTheNewValues)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "マテリアルの作り直しの試し", .size = {64, 64}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    NS::Material* material = assets.LoadMaterial("Assets/Materials/ground_arrow_band.mat");
    ASSERT_TRUE(material->D3D()->IsValid());
    ASSERT_EQ(material->D3D()->Blend(), NS::Gfx::BlendMode::Alpha);

    WriteStringField(*material, "混ぜ方", "Additive");
    WriteStringField(*material, "ピクセルシェーダー", "Shaders/player.ps.hlsl");
    EXPECT_TRUE(assets.RebuildMaterial(*material));
    EXPECT_EQ(material->D3D()->Blend(), NS::Gfx::BlendMode::Additive);
    EXPECT_EQ(material->PixelShader()->Path(), ContentPath("Shaders/player.ps.hlsl"));
    EXPECT_EQ(assets.LoadMaterial("Assets/Materials/ground_arrow_band.mat"), material);
    assets.Clear();
}
