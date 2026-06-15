#include "Framework/Scene/MaterialLibrary.h"

#include <gtest/gtest.h>

#include <string>

namespace
{
    using NS::Scene::MaterialFileDesc;
    using NS::Scene::ParseMaterialJson;

    TEST(MaterialLibraryParseTest, FullValidJsonParsesAllFields)
    {
        const std::string json = R"({
            "vs": "Shaders/standard.vs.hlsl",
            "ps": "Shaders/player.ps.hlsl",
            "textures": ["Assets/Textures/cube_test.png", "Assets/Textures/extra.png"],
            "baseColor": [0.6, 0.5, 0.4],
            "blend": "Alpha"
        })";
        MaterialFileDesc desc{};
        std::string err;
        ASSERT_TRUE(ParseMaterialJson(json, desc, err)) << err;
        EXPECT_EQ(desc.vertexShader.generic_string(), "Shaders/standard.vs.hlsl");
        EXPECT_EQ(desc.pixelShader.generic_string(), "Shaders/player.ps.hlsl");
        ASSERT_EQ(desc.textures.size(), 2u);
        EXPECT_EQ(desc.textures[0].generic_string(), "Assets/Textures/cube_test.png");
        EXPECT_EQ(desc.textures[1].generic_string(), "Assets/Textures/extra.png");
        EXPECT_FLOAT_EQ(desc.baseColor.x, 0.6f);
        EXPECT_FLOAT_EQ(desc.baseColor.y, 0.5f);
        EXPECT_FLOAT_EQ(desc.baseColor.z, 0.4f);
        EXPECT_EQ(desc.blend, NS::Graphics::BlendMode::Alpha);
    }

    TEST(MaterialLibraryParseTest, MissingVsOrPsFails)
    {
        const std::string json = R"({ "ps": "Shaders/player.ps.hlsl" })";
        MaterialFileDesc desc{};
        std::string err;
        EXPECT_FALSE(ParseMaterialJson(json, desc, err));
        EXPECT_FALSE(err.empty());
    }

    TEST(MaterialLibraryParseTest, InvalidJsonFails)
    {
        const std::string json = "{ this is not json )";
        MaterialFileDesc desc{};
        std::string err;
        EXPECT_FALSE(ParseMaterialJson(json, desc, err));
        EXPECT_FALSE(err.empty());
    }

    TEST(MaterialLibraryParseTest, OptionalFieldsDefaultWhenAbsent)
    {
        const std::string json = R"({ "vs": "a.vs.hlsl", "ps": "b.ps.hlsl" })";
        MaterialFileDesc desc{};
        std::string err;
        ASSERT_TRUE(ParseMaterialJson(json, desc, err)) << err;
        EXPECT_TRUE(desc.textures.empty());
        EXPECT_FLOAT_EQ(desc.baseColor.x, 1.0f);
        EXPECT_FLOAT_EQ(desc.baseColor.y, 1.0f);
        EXPECT_FLOAT_EQ(desc.baseColor.z, 1.0f);
        EXPECT_EQ(desc.blend, NS::Graphics::BlendMode::Opaque);
    }

    TEST(MaterialLibraryParseTest, BlendStringMapsToEnum)
    {
        const auto parseBlend = [](const char* blendValue, NS::Graphics::BlendMode& outBlend) {
            const std::string json =
                std::string(R"({ "vs": "a.vs.hlsl", "ps": "b.ps.hlsl", "blend": ")") + blendValue + "\" }";
            MaterialFileDesc desc{};
            std::string err;
            const bool ok = ParseMaterialJson(json, desc, err);
            outBlend = desc.blend;
            return ok;
        };
        NS::Graphics::BlendMode blend{};
        ASSERT_TRUE(parseBlend("Additive", blend));
        EXPECT_EQ(blend, NS::Graphics::BlendMode::Additive);
        ASSERT_TRUE(parseBlend("Alpha", blend));
        EXPECT_EQ(blend, NS::Graphics::BlendMode::Alpha);
        // 未知の blend は Opaque にフォールバックする
        ASSERT_TRUE(parseBlend("Nonsense", blend));
        EXPECT_EQ(blend, NS::Graphics::BlendMode::Opaque);
    }
} // namespace
