#include <Runtime/Core/Filesystem.h>
#include <Runtime/Core/Logger.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Object/AssetManager.h>
#include <Runtime/Physics/MeshCollision.h>
#include <Runtime/Physics/Triangle.h>
#include <Runtime/Platform/Window.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <gtest/gtest.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

    std::string EncodeBase64(std::span<const std::uint8_t> bytes)
    {
        constexpr const char* k_Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (std::size_t i = 0; i < bytes.size(); i += 3)
        {
            const std::size_t remaining = bytes.size() - i;
            std::uint32_t triple = static_cast<std::uint32_t>(bytes[i]) << 16;
            if (remaining > 1)
            {
                triple |= static_cast<std::uint32_t>(bytes[i + 1]) << 8;
            }
            if (remaining > 2)
            {
                triple |= static_cast<std::uint32_t>(bytes[i + 2]);
            }

            out.push_back(k_Alphabet[(triple >> 18) & 0x3F]);
            out.push_back(k_Alphabet[(triple >> 12) & 0x3F]);
            char third = '=';
            if (remaining > 1)
            {
                third = k_Alphabet[(triple >> 6) & 0x3F];
            }
            char fourth = '=';
            if (remaining > 2)
            {
                fourth = k_Alphabet[triple & 0x3F];
            }
            out.push_back(third);
            out.push_back(fourth);
        }
        return out;
    }

    // 三角形 1 枚の glTF。 buffer は data URI に埋め、 外部 .bin を読ませない
    std::string SingleTriangleGltf(const std::array<float, 9>& positions)
    {
        std::vector<std::uint8_t> buffer(42, 0);
        std::memcpy(buffer.data(), positions.data(), 36);
        const std::array<std::uint16_t, 3> indices = {0, 1, 2};
        std::memcpy(buffer.data() + 36, indices.data(), 6);

        std::array<float, 3> low = {positions[0], positions[1], positions[2]};
        std::array<float, 3> high = low;
        for (std::size_t vertex = 1; vertex < 3; ++vertex)
        {
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                low[axis] = std::min(low[axis], positions[vertex * 3 + axis]);
                high[axis] = std::max(high[axis], positions[vertex * 3 + axis]);
            }
        }
        const std::string bounds =
            std::format(R"("min":[{},{},{}],"max":[{},{},{}])", low[0], low[1], low[2], high[0], high[1], high[2]);

        return std::string{R"({"asset":{"version":"2.0"},)"} +
               R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)" +
               R"("buffers":[{"byteLength":42,"uri":"data:application/octet-stream;base64,)" + EncodeBase64(buffer) +
               R"("}],)" + R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},)" +
               R"({"buffer":0,"byteOffset":36,"byteLength":6,"target":34963}],)" +
               R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3",)" + bounds + "}," +
               R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})";
    }

    // ContentRoot 相対の参照。 ContentRoot の外なら空
    std::string ContentRelativeRef(const std::filesystem::path& path)
    {
        const std::filesystem::path relative = path.lexically_relative(NS::Core::FileSystem::ContentRoot());
        if (relative.empty() || *relative.begin() == std::filesystem::path{".."})
            return {};
        return relative.generic_string();
    }

    std::filesystem::path WriteFixture(const char* name, std::string_view content)
    {
        const std::filesystem::path path = NS::Core::FileSystem::GetExeDirectory() / name;
        const bool ok = NS::Core::FileSystem::WriteAllBytes(
            path, std::as_bytes(std::span<const char>{content.data(), content.size()}));
        EXPECT_TRUE(ok);
        return path;
    }

    NS::Core::Vector3 FaceNormal(const NS::Physics::Triangle& triangle)
    {
        return (triangle.v1 - triangle.v0).Cross(triangle.v2 - triangle.v0);
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

// 組み込みの cube は device 無しでも三角形 12 枚と Jolt の形を持つ当たりになり、 どの面の法線も外を向く
TEST_F(AssetManagerTest, BuiltinCubeCollisionHasTwelveOutwardTriangles)
{
    AssetManager am{NS::Core::FileSystem::ContentRoot()};

    const NS::Physics::MeshCollision* collision = am.GetOrLoadMeshCollision("cube");
    ASSERT_NE(collision, nullptr);
    EXPECT_NE(collision->shape, nullptr);
    ASSERT_EQ(collision->triangles.size(), 12u);
    for (const NS::Physics::Triangle& triangle : collision->triangles)
    {
        const NS::Core::Vector3 centroid = (triangle.v0 + triangle.v1 + triangle.v2) / 3.0f;
        EXPECT_GT(FaceNormal(triangle).Dot(centroid), 0.0f);
    }
}

// 同じ参照には同じ当たりを返す。 配置物ごとに読み直さない
TEST_F(AssetManagerTest, SameMeshRefSharesCollision)
{
    AssetManager am{NS::Core::FileSystem::ContentRoot()};

    const NS::Physics::MeshCollision* first = am.GetOrLoadMeshCollision("wedge45");
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(am.GetOrLoadMeshCollision("wedge45"), first);
}

// 右手系の glTF を Z 反転で読んでも表面は同じ側を向く。 期待する法線は元の法線 (0, -2, 6) の Z を反転した物
TEST_F(AssetManagerTest, GltfCollisionKeepsFrontFaceInLeftHandedSpace)
{
    const std::array<float, 9> positions = {0.0f, 0.0f, 1.0f, 2.0f, 0.0f, 1.0f, 0.0f, 3.0f, 2.0f};
    const std::filesystem::path path = WriteFixture("ns_am_collision_triangle.gltf", SingleTriangleGltf(positions));
    const std::string ref = ContentRelativeRef(path);
    if (ref.empty())
        GTEST_SKIP() << "実行ファイルが ContentRoot の外にある: " << path.string();

    AssetManager am{NS::Core::FileSystem::ContentRoot()};
    const NS::Physics::MeshCollision* collision = am.GetOrLoadMeshCollision(ref);
    ASSERT_NE(collision, nullptr);
    ASSERT_EQ(collision->triangles.size(), 1u);

    const NS::Physics::Triangle& triangle = collision->triangles[0];
    const std::array<NS::Core::Vector3, 3> vertices = {triangle.v0, triangle.v1, triangle.v2};
    const std::array<NS::Core::Vector3, 3> expected = {NS::Core::Vector3{0.0f, 0.0f, -1.0f},
                                                       NS::Core::Vector3{2.0f, 0.0f, -1.0f},
                                                       NS::Core::Vector3{0.0f, 3.0f, -2.0f}};
    for (const NS::Core::Vector3& point : expected)
    {
        bool found = false;
        for (const NS::Core::Vector3& vertex : vertices)
        {
            if (NS::Core::Vector3::DistanceSquared(vertex, point) < 1.0e-8f)
            {
                found = true;
            }
        }
        EXPECT_TRUE(found) << point.x << ", " << point.y << ", " << point.z;
    }

    NS::Core::Vector3 normal = FaceNormal(triangle);
    normal.Normalize();
    NS::Core::Vector3 frontFace{0.0f, -2.0f, -6.0f};
    frontFace.Normalize();
    EXPECT_NEAR(normal.x, frontFace.x, 1.0e-5f);
    EXPECT_NEAR(normal.y, frontFace.y, 1.0e-5f);
    EXPECT_NEAR(normal.z, frontFace.z, 1.0e-5f);
}

// 空文字・ContentRoot の外・存在しない file は nullptr。 読めなかった参照は 2 度目も nullptr
TEST_F(AssetManagerTest, UnresolvableMeshRefHasNoCollision)
{
    AssetManager am{NS::Core::FileSystem::ContentRoot()};

    EXPECT_EQ(am.GetOrLoadMeshCollision(""), nullptr);
    EXPECT_EQ(am.GetOrLoadMeshCollision("../secret.gltf"), nullptr);
    EXPECT_EQ(am.GetOrLoadMeshCollision("__ns_am_missing_collision__.gltf"), nullptr);
    EXPECT_EQ(am.GetOrLoadMeshCollision("__ns_am_missing_collision__.gltf"), nullptr);
}

// 同じ file を指す参照は、 先頭に ./ を付けても記録 1 件の同じ三角形を返す
TEST_F(AssetManagerTest, CollisionRefSpellingsShareOneRecord)
{
    const std::array<float, 9> positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const std::filesystem::path path = WriteFixture("ns_am_spelling_triangle.gltf", SingleTriangleGltf(positions));
    const std::string ref = ContentRelativeRef(path);
    if (ref.empty())
        GTEST_SKIP() << "実行ファイルが ContentRoot の外にある: " << path.string();

    AssetManager am{NS::Core::FileSystem::ContentRoot()};
    const NS::Physics::MeshCollision* plain = am.GetOrLoadMeshCollision(ref);
    ASSERT_NE(plain, nullptr);
    EXPECT_EQ(am.GetOrLoadMeshCollision("./" + ref), plain);
    EXPECT_EQ(am.MeshCacheSize(), 1u);
}

// 描画を先に頼んだ glTF は、 当たりを頼んだ時に読み直さない。 間で file を書き換えても最初の中身のまま
TEST_F(AssetManagerTest, MeshAndCollisionReadTheGltfOnce)
{
    const std::array<float, 9> first = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const std::array<float, 9> rewritten = {0.0f, 0.0f, 0.0f, 5.0f, 0.0f, 0.0f, 0.0f, 5.0f, 0.0f};
    const std::filesystem::path path = WriteFixture("ns_am_read_once_triangle.gltf", SingleTriangleGltf(first));
    const std::string ref = ContentRelativeRef(path);
    if (ref.empty())
        GTEST_SKIP() << "実行ファイルが ContentRoot の外にある: " << path.string();

    AssetManager am{NS::Core::FileSystem::ContentRoot()};
    static_cast<void>(am.GetOrLoadMesh(path));
    WriteFixture("ns_am_read_once_triangle.gltf", SingleTriangleGltf(rewritten));

    const NS::Physics::MeshCollision* collision = am.GetOrLoadMeshCollision(ref);
    ASSERT_NE(collision, nullptr);
    ASSERT_EQ(collision->triangles.size(), 1u);
    const NS::Physics::Triangle& triangle = collision->triangles[0];
    EXPECT_FLOAT_EQ(std::max({triangle.v0.x, triangle.v1.x, triangle.v2.x}), 1.0f);
    EXPECT_EQ(am.MeshCacheSize(), 1u);
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
