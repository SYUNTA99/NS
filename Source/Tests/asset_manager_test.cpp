#include <Runtime/Core/Filesystem.h>
#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Object/AssetManager.h>
#include <Runtime/Platform/Window.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>

namespace
{
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;
    using NS::Object::AssetManager;
    using NS::Object::MaterialFileDesc;
    using NS::Object::ParseMaterialJson;

    WindowDesc MakeWindowDesc(const char* title)
    {
        WindowDesc d{};
        d.title = title;
        d.size = NS::Core::Size2D{320, 240};
        d.visible = false;
        return d;
    }

    RendererDesc MakeRendererDesc()
    {
        RendererDesc d{};
        d.vsync = false;
        d.enableDebugLayer = false;
        return d;
    }

    std::filesystem::path ShaderPath(const char* name)
    {
        return NS::Core::FileSystem::ContentRoot() / "Shaders" / name;
    }

    std::filesystem::path TexturePath(const char* name)
    {
        return NS::Core::FileSystem::ContentRoot() / "Assets" / "Textures" / name;
    }

    std::filesystem::path MaterialPath(const char* name)
    {
        return NS::Core::FileSystem::ContentRoot() / "Assets" / "Materials" / name;
    }
} // namespace

class AssetManagerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

// 同一 path は共有され同一の非 null インスタンスが返る
TEST_F(AssetManagerTest, SamePathReturnsSamePointer)
{
    Window window(MakeWindowDesc("ns_am_dedup"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    AssetManager am{NS::Core::FileSystem::ContentRoot()};

    auto* shaderA = am.GetOrLoadShader(ShaderPath("standard.vs.hlsl"));
    auto* shaderB = am.GetOrLoadShader(ShaderPath("standard.vs.hlsl"));
    ASSERT_NE(shaderA, nullptr);
    EXPECT_EQ(shaderA, shaderB);

    auto* texA = am.GetOrLoadTexture(TexturePath("cube_test.png"));
    auto* texB = am.GetOrLoadTexture(TexturePath("cube_test.png"));
    ASSERT_NE(texA, nullptr);
    EXPECT_EQ(texA, texB);
}

// 組み込み名前キーは同一の非 null StaticMesh を返し、 未登録名は nullptr
TEST_F(AssetManagerTest, BuiltinReturnsSameNonNullPointer)
{
    Window window(MakeWindowDesc("ns_am_builtin"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    AssetManager am{NS::Core::FileSystem::ContentRoot()};
    am.RegisterBuiltins();

    EXPECT_NE(am.Builtin("cube"), nullptr);
    EXPECT_EQ(am.Builtin("cube"), am.Builtin("cube"));
    EXPECT_NE(am.Builtin("wedge45"), nullptr);
    EXPECT_NE(am.Builtin("shadowQuad"), nullptr);
    EXPECT_EQ(am.Builtin("nonexistent"), nullptr);
}

// device 無しでも読込失敗した mesh path は負キャッシュされ、 2 度目以降は再読込せず即 nullptr を返す
TEST_F(AssetManagerTest, FailedMeshLoadIsNegativeCached)
{
    AssetManager am{NS::Core::FileSystem::ContentRoot()};
    const std::filesystem::path missing = "__ns_am_missing_mesh__.gltf";

    EXPECT_EQ(am.GetOrLoadMesh(missing), nullptr);
    EXPECT_EQ(am.MeshCacheSize(), 1u); // 失敗を 1 件だけ負キャッシュする
    EXPECT_EQ(am.GetOrLoadMesh(missing), nullptr);
    EXPECT_EQ(am.MeshCacheSize(), 1u); // 2 度目は再読込せず件数が増えない
}

// Reload は path キーの Shader をその場で置き換えるのでキャッシュのポインタが不変
TEST_F(AssetManagerTest, ReloadShaderInPlaceKeepsIdentity)
{
    Window window(MakeWindowDesc("ns_am_reload"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    AssetManager am{NS::Core::FileSystem::ContentRoot()};
    const auto path = ShaderPath("standard.vs.hlsl");
    auto* before = am.GetOrLoadShader(path);
    ASSERT_NE(before, nullptr);

    EXPECT_TRUE(am.Reload(path));
    EXPECT_EQ(am.GetOrLoadShader(path), before);
}

// 未キャッシュ path の Reload は false
TEST_F(AssetManagerTest, ReloadMissReturnsFalse)
{
    Window window(MakeWindowDesc("ns_am_reload_miss"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    AssetManager am{NS::Core::FileSystem::ContentRoot()};
    EXPECT_FALSE(am.Reload("C:/nonexistent/__ns_am_missing.vs.hlsl"));
}

// device 無しの .mat JSON 解析: 全フィールド
TEST(AssetManagerParseTest, FullValidJsonParsesAllFields)
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

TEST(AssetManagerParseTest, MissingVsOrPsFails)
{
    const std::string json = R"({ "ps": "Shaders/player.ps.hlsl" })";
    MaterialFileDesc desc{};
    std::string err;
    EXPECT_FALSE(ParseMaterialJson(json, desc, err));
    EXPECT_FALSE(err.empty());
}

TEST(AssetManagerParseTest, InvalidJsonFails)
{
    const std::string json = "{ this is not json )";
    MaterialFileDesc desc{};
    std::string err;
    EXPECT_FALSE(ParseMaterialJson(json, desc, err));
    EXPECT_FALSE(err.empty());
}

TEST(AssetManagerParseTest, OptionalFieldsDefaultWhenAbsent)
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

TEST(AssetManagerParseTest, BlendStringMapsToEnum)
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

// 同一 .mat path の LoadMaterial は重複除去され同一 Material* を返す。 内部 leaf を借りて組む
TEST_F(AssetManagerTest, LoadMaterialDedupReturnsSamePointer)
{
    Window window(MakeWindowDesc("ns_am_loadmat"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    AssetManager am{NS::Core::FileSystem::ContentRoot()};
    const auto matPath = MaterialPath("flat.mat");
    if (!std::filesystem::exists(matPath))
        GTEST_SKIP() << "flat.mat が無い: " << matPath.string();

    auto first = am.LoadMaterial(matPath);
    auto second = am.LoadMaterial(matPath);
    ASSERT_NE(first.material, nullptr);
    EXPECT_EQ(first.material, second.material);
}

// RegisterSharedMaterials 後、 player/water/shadow が非 null かつ同一アクセサが同一ポインタ
TEST_F(AssetManagerTest, SharedMaterialsNonNullAfterRegister)
{
    Window window(MakeWindowDesc("ns_am_shared"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    AssetManager am{NS::Core::FileSystem::ContentRoot()};
    am.RegisterBuiltins();
    am.RegisterSharedMaterials();

    EXPECT_NE(am.SharedMaterial("player"), nullptr);
    EXPECT_NE(am.SharedMaterial("water"), nullptr);
    EXPECT_NE(am.SharedMaterial("shadow"), nullptr);
    EXPECT_EQ(am.SharedMaterial("player"), am.SharedMaterial("player"));
    EXPECT_EQ(am.SharedMaterial("nonexistent"), nullptr);
}
