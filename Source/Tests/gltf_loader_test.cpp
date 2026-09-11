#include <Runtime/Core/Filesystem.h>
#include <Runtime/Graphics/GltfLoader.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <span>
#include <string>
#include <string_view>

namespace
{
    // 単一三角形 buffer (102 byte): pos v0(1,2,3) v1(4,5,6) v2(7,8,9) / normal / uv / idx[0,1,2]
    const char* k_TriBase64 = "AACAPwAAAEAAAEBAAACAQAAAoEAAAMBAAADgQAAAAEEAABBBAAAAAAAAAAAAAIA/"
                              "AAAAAAAAgD8AAAAAAACAPwAAAAAAAAAAzczMPc3MTD6amZk"
                              "+zczMPgAAAD+amRk/AAABAAIA";

    // 2 primitive buffer (204 byte): prim0 pos(1..9)/n(0,0,1)/uv(0)/idx[0,1,2],
    //                                prim1 pos(10..18)/n(0,1,0)/uv(1)/idx[0,1,2]
    const char* k_MultiBase64 =
        "AACAPwAAAEAAAEBAAACAQAAAoEAAAMBAAADgQAAAAEEAABBBAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/"
        "AAAAAAAAAAAAAIA/AAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAABAAIAAAAgQQAAMEEAAEBBAABQQQAAYEEAAHBBAACAQQAAiEEAAJBBAAAAAAAAgD8AAA"
        "AAAAAAAAAAgD8AAAAAAAAAAA"
        "AAgD8AAAAAAACAPwAAgD8AAIA/AACAPwAAgD8AAIA/AAABAAIA";

    // node translation [100,0,0] 配下に 2 primitive を持つ mesh
    std::string MultiPrimGltf()
    {
        return std::string{R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)"} +
               R"("nodes":[{"mesh":0,"translation":[100,0,0]}],)" + R"("meshes":[{"primitives":[)" +
               R"({"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3},)" +
               R"({"attributes":{"POSITION":4,"NORMAL":5,"TEXCOORD_0":6},"indices":7}]}],)" +
               R"("buffers":[{"byteLength":204,"uri":"data:application/octet-stream;base64,)" + k_MultiBase64 +
               R"("}],)" + R"("bufferViews":[)" + R"({"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},)" +
               R"({"buffer":0,"byteOffset":36,"byteLength":36,"target":34962},)" +
               R"({"buffer":0,"byteOffset":72,"byteLength":24,"target":34962},)" +
               R"({"buffer":0,"byteOffset":96,"byteLength":6,"target":34963},)" +
               R"({"buffer":0,"byteOffset":102,"byteLength":36,"target":34962},)" +
               R"({"buffer":0,"byteOffset":138,"byteLength":36,"target":34962},)" +
               R"({"buffer":0,"byteOffset":174,"byteLength":24,"target":34962},)" +
               R"({"buffer":0,"byteOffset":198,"byteLength":6,"target":34963}],)" + R"("accessors":[)" +
               R"({"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[1,2,3],"max":[7,8,9]},)" +
               R"({"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},)" +
               R"({"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},)" +
               R"({"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"},)" +
               R"({"bufferView":4,"componentType":5126,"count":3,"type":"VEC3","min":[10,11,12],"max":[16,17,18]},)" +
               R"({"bufferView":5,"componentType":5126,"count":3,"type":"VEC3"},)" +
               R"({"bufferView":6,"componentType":5126,"count":3,"type":"VEC2"},)" +
               R"({"bufferView":7,"componentType":5123,"count":3,"type":"SCALAR"}]})";
    }

    // 単一三角形 + extensionsRequired に Draco。展開できないので弾く対象
    std::string DracoRequiredGltf()
    {
        return std::string{R"({"asset":{"version":"2.0"},"extensionsRequired":["KHR_draco_mesh_compression"],)"} +
               R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)" +
               R"("buffers":[{"byteLength":102,"uri":"data:application/octet-stream;base64,)" + k_TriBase64 +
               R"("}],)" + R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},)" +
               R"({"buffer":0,"byteOffset":96,"byteLength":6,"target":34963}],)" +
               R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[1,2,3],"max":[7,8,9]},)" +
               R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})";
    }

    // 単一三角形 だが primitive mode=1 (LINES)。 三角形以外なので skip される
    std::string LineTopologyGltf()
    {
        return std::string{R"({"asset":{"version":"2.0"},)"} +
               R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"mode":1}]}],)" +
               R"("buffers":[{"byteLength":102,"uri":"data:application/octet-stream;base64,)" + k_TriBase64 +
               R"("}],)" + R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},)" +
               R"({"buffer":0,"byteOffset":96,"byteLength":6,"target":34963}],)" +
               R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[1,2,3],"max":[7,8,9]},)" +
               R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})";
    }

    // 法線なし三角形 (42 byte): pos v0(0,0,0) v1(1,0,0) v2(0,1,0) / idx[0,1,2]、 NORMAL 属性なし
    const char* k_NoNormalBase64 = "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA";

    std::string NoNormalGltf()
    {
        return std::string{R"({"asset":{"version":"2.0"},)"} +
               R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)" +
               R"("buffers":[{"byteLength":42,"uri":"data:application/octet-stream;base64,)" + k_NoNormalBase64 +
               R"("}],)" + R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},)" +
               R"({"buffer":0,"byteOffset":36,"byteLength":6,"target":34963}],)" +
               R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},)" +
               R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})";
    }

    // fixture を exe 隣に NS Filesystem 経由で書き出してパスを返す。std::ofstream は使わない
    std::filesystem::path WriteFixture(const char* name, std::string_view content)
    {
        const std::filesystem::path path = NS::Core::FileSystem::GetExeDirectory() / name;
        const bool ok = NS::Core::FileSystem::WriteAllBytes(
            path, std::as_bytes(std::span<const char>{content.data(), content.size()}));
        EXPECT_TRUE(ok);
        return path;
    }
} // namespace

TEST(GltfLoaderTest, NonexistentPathReturnsEmpty)
{
    const auto geom = NS::Graphics::LoadGltfMesh("ns_does_not_exist_3f9a1c.gltf");
    EXPECT_TRUE(geom.vertices.empty());
    EXPECT_TRUE(geom.indices.empty());
}

TEST(GltfLoaderTest, ConcatenatesPrimitivesWithNodeTransformAndOffset)
{
    const std::string json = MultiPrimGltf();
    const std::filesystem::path path = WriteFixture("ns_gltf_loader_test_multi.gltf", json);
    const auto geom = NS::Graphics::LoadGltfMesh(path.string());

    // 2 primitive 連結で 6 頂点 / 6 index
    ASSERT_EQ(geom.vertices.size(), 6u);
    ASSERT_EQ(geom.indices.size(), 6u);

    // node translation(+100x) 適用 + Z 反転 (prim0 先頭 / prim1 先頭)
    EXPECT_FLOAT_EQ(geom.vertices[0].position.x, 101.0f);
    EXPECT_FLOAT_EQ(geom.vertices[0].position.y, 2.0f);
    EXPECT_FLOAT_EQ(geom.vertices[0].position.z, -3.0f);
    EXPECT_FLOAT_EQ(geom.vertices[3].position.x, 110.0f);
    EXPECT_FLOAT_EQ(geom.vertices[3].position.z, -12.0f);

    // normal: prim0 (0,0,1)->Z反転(0,0,-1) / prim1 (0,1,0)
    EXPECT_FLOAT_EQ(geom.vertices[0].normal.z, -1.0f);
    EXPECT_FLOAT_EQ(geom.vertices[3].normal.y, 1.0f);

    // winding 反転 + prim1 は baseVertex=3 の offset: [0,2,1, 3,5,4]
    const std::vector<std::uint32_t> expected{0u, 2u, 1u, 3u, 5u, 4u};
    EXPECT_EQ(geom.indices, expected);
}

TEST(GltfLoaderTest, RejectsDracoCompressed)
{
    const std::string json = DracoRequiredGltf();
    const std::filesystem::path path = WriteFixture("ns_gltf_loader_test_draco.gltf", json);
    const auto geom = NS::Graphics::LoadGltfMesh(path.string());
    EXPECT_TRUE(geom.vertices.empty());
    EXPECT_TRUE(geom.indices.empty());
}

TEST(GltfLoaderTest, SkipsNonTriangleTopology)
{
    const std::string json = LineTopologyGltf();
    const std::filesystem::path path = WriteFixture("ns_gltf_loader_test_lines.gltf", json);
    const auto geom = NS::Graphics::LoadGltfMesh(path.string());
    EXPECT_TRUE(geom.vertices.empty());
    EXPECT_TRUE(geom.indices.empty());
}

TEST(GltfLoaderTest, ComputesSmoothNormalWhenAbsent)
{
    const std::string json = NoNormalGltf();
    const std::filesystem::path path = WriteFixture("ns_gltf_loader_test_nonormal.gltf", json);
    const auto geom = NS::Graphics::LoadGltfMesh(path.string());

    ASSERT_EQ(geom.vertices.size(), 3u);

    // XY 平面三角形の面法線 = RH で (0,0,1) -> Z 反転で NS の (0,0,-1)。 3 頂点とも同方向
    for (const auto& v : geom.vertices)
    {
        EXPECT_NEAR(v.normal.x, 0.0f, 1e-5f);
        EXPECT_NEAR(v.normal.y, 0.0f, 1e-5f);
        EXPECT_NEAR(v.normal.z, -1.0f, 1e-5f);
    }
}
