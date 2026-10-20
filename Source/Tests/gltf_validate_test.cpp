#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Geometry/GltfParser.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// 壊れた glTF を読み込みの入口で断る事を縛る
// 数の合わない accessor を読むと、バッファビューの外を読む

namespace
{
    // POSITION は 3 点で固定。TEXCOORD_0 の数だけ変えられる
    std::string TriangleGltf(int texcoordCount)
    {
        const std::string uvBytes = std::to_string(texcoordCount * 8);
        return R"({"asset":{"version":"2.0"},)"
               R"("buffers":[{"byteLength":60,"uri":"data:application/octet-stream;base64,)"
               R"(AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/"}],)"
               R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},)"
               R"({"buffer":0,"byteOffset":36,"byteLength":)" +
               uvBytes +
               R"(}],)"
               R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)"
               R"({"bufferView":1,"componentType":5126,"count":)" +
               std::to_string(texcoordCount) +
               R"(,"type":"VEC2"}],)"
               R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1}}]}],)"
               R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    }

    NS::MeshGeometry ParseText(const std::string& text)
    {
        NS::GltfFileDesc file{};
        file.bytes = std::as_bytes(std::span<const char>{text.data(), text.size()});
        file.name = "triangle.gltf";
        return NS::ParseGltfMesh(file);
    }
} // namespace

// 数の揃ったファイルは読める。下の試しが別の理由で空になっていない事の確かめ
TEST(GltfValidate, MatchingCountsLoad)
{
    const NS::MeshGeometry geom = ParseText(TriangleGltf(3));
    EXPECT_EQ(geom.vertices.size(), 3u);
}

// TEXCOORD_0 が POSITION より少ないファイルは、読まずに空を返す
TEST(GltfValidate, MismatchedAttributeCountIsRejected)
{
    const TestLogCapture log;
    const NS::MeshGeometry geom = ParseText(TriangleGltf(1));
    EXPECT_TRUE(geom.vertices.empty());
    EXPECT_EQ(log.ErrorCount(), 1);
}

// 静的なメッシュと骨付きのメッシュが、右手系から左手系へ同じ変換で読む事を縛る
// 片方だけ符号を書き換えると、同じ形が 2 つの読み方で鏡に映ったように食い違う

namespace
{
    // 三角形 1 つ (z = +1・法線 +Z、骨 1 本に重み 1) と、点 1 つの primitive を持つ
    // 骨付きとして読めるよう skin を付ける。node の変換は単位なので、静的な読み方の世界変換も単位
    const std::string k_SkinnedTriangleGltf =
        R"({"asset":{"version":"2.0"},)"
        R"("buffers":[{"byteLength":208,"uri":"mesh.bin"}],)"
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},)"
        R"({"buffer":0,"byteOffset":36,"byteLength":36},)"
        R"({"buffer":0,"byteOffset":72,"byteLength":12},)"
        R"({"buffer":0,"byteOffset":84,"byteLength":48},)"
        R"({"buffer":0,"byteOffset":132,"byteLength":64},)"
        R"({"buffer":0,"byteOffset":196,"byteLength":12}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)"
        R"({"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},)"
        R"({"bufferView":2,"componentType":5121,"count":3,"type":"VEC4"},)"
        R"({"bufferView":3,"componentType":5126,"count":3,"type":"VEC4"},)"
        R"({"bufferView":4,"componentType":5126,"count":1,"type":"MAT4"},)"
        R"({"bufferView":5,"componentType":5126,"count":1,"type":"VEC3"}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"JOINTS_0":2,"WEIGHTS_0":3}},)"
        R"({"attributes":{"POSITION":5},"mode":0}]}],)"
        R"("skins":[{"joints":[1],"inverseBindMatrices":4}],)"
        R"("nodes":[{"mesh":0,"skin":0},{"name":"root"}],"scenes":[{"nodes":[0,1]}],"scene":0})";

    std::vector<std::byte> SkinnedTriangleBin()
    {
        std::vector<std::byte> bytes(208, std::byte{0});
        const float positions[9] = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f};
        const float normals[9] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        const float weights[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
        const float identity[16] = {
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        const float point[3] = {5.0f, 5.0f, 5.0f};
        std::memcpy(bytes.data(), positions, sizeof(positions));
        std::memcpy(bytes.data() + 36, normals, sizeof(normals));
        std::memcpy(bytes.data() + 84, weights, sizeof(weights));
        std::memcpy(bytes.data() + 132, identity, sizeof(identity));
        std::memcpy(bytes.data() + 196, point, sizeof(point));
        return bytes;
    }

    NS::GltfFileDesc SkinnedTriangleFile()
    {
        NS::GltfFileDesc file{};
        file.bytes = std::as_bytes(std::span<const char>{k_SkinnedTriangleGltf.data(), k_SkinnedTriangleGltf.size()});
        file.name = "skinned_triangle.gltf";
        file.readFile = [](std::string_view) -> std::optional<std::vector<std::byte>> { return SkinnedTriangleBin(); };
        return file;
    }
} // namespace

// 点の primitive はどちらの読み方でも積まず、三角形の 3 点は位置と法線の Z が反転して揃う
TEST(GltfConvert, StaticAndSkinnedReadTheSameTriangle)
{
    const TestLogCapture log;
    const NS::MeshGeometry staticMesh = NS::ParseGltfMesh(SkinnedTriangleFile());
    const NS::SkinnedMeshData skinnedMesh = NS::ParseGltfSkinnedMesh(SkinnedTriangleFile());
    EXPECT_EQ(log.ErrorCount(), 2); // 点の primitive を読み方ごとに 1 回断る

    ASSERT_EQ(staticMesh.vertices.size(), 3u);
    ASSERT_EQ(skinnedMesh.vertices.size(), 3u);
    EXPECT_EQ(staticMesh.indices, skinnedMesh.indices);
    for (std::size_t i = 0; i < 3u; ++i)
    {
        const NS::StaticVertex& s = staticMesh.vertices[i];
        const NS::SkinnedVertex& k = skinnedMesh.vertices[i];
        EXPECT_FLOAT_EQ(s.position.z, -1.0f);
        EXPECT_FLOAT_EQ(s.normal.z, -1.0f);
        EXPECT_FLOAT_EQ(s.position.x, k.position.x);
        EXPECT_FLOAT_EQ(s.position.y, k.position.y);
        EXPECT_FLOAT_EQ(s.position.z, k.position.z);
        EXPECT_FLOAT_EQ(s.normal.x, k.normal.x);
        EXPECT_FLOAT_EQ(s.normal.y, k.normal.y);
        EXPECT_FLOAT_EQ(s.normal.z, k.normal.z);
    }
}

// 静的なメッシュの包む箱は、積んだ三角形の頂点の最小・最大。点の primitive の (5, 5, 5) を含まない
TEST(GltfConvert, StaticBoundsMatchTheTriangleVertices)
{
    const NS::MeshGeometry geom = NS::ParseGltfMesh(SkinnedTriangleFile());
    ASSERT_TRUE(geom.hasBounds);
    EXPECT_FLOAT_EQ(geom.bounds.Center.x, 0.5f);
    EXPECT_FLOAT_EQ(geom.bounds.Center.y, 0.5f);
    EXPECT_FLOAT_EQ(geom.bounds.Center.z, -1.0f);
    EXPECT_FLOAT_EQ(geom.bounds.Extents.x, 0.5f);
    EXPECT_FLOAT_EQ(geom.bounds.Extents.y, 0.5f);
    EXPECT_FLOAT_EQ(geom.bounds.Extents.z, 0.0f);
}

// 包む箱の関数は、幅を空けて並んだ位置の最小・最大の箱を返す
TEST(AABBFromPositions, BoxSpansTheMinAndMaxOfStridedPositions)
{
    NS::StaticVertex vertices[3]{};
    vertices[0].position = NS::Vector3{-1.0f, 2.0f, 3.0f};
    vertices[1].position = NS::Vector3{4.0f, -2.0f, 0.0f};
    vertices[2].position = NS::Vector3{0.0f, 0.0f, 7.0f};

    const NS::AABB box = NS::AABBFromPositions(&vertices[0].position, 3u, sizeof(NS::StaticVertex));

    EXPECT_FLOAT_EQ(box.Center.x, 1.5f);
    EXPECT_FLOAT_EQ(box.Center.y, 0.0f);
    EXPECT_FLOAT_EQ(box.Center.z, 3.5f);
    EXPECT_FLOAT_EQ(box.Extents.x, 2.5f);
    EXPECT_FLOAT_EQ(box.Extents.y, 2.0f);
    EXPECT_FLOAT_EQ(box.Extents.z, 3.5f);
}

// 頂点が 0 の時は、DirectX の箱の既定 (大きさ 1) でなく、中心 0・大きさ 0 の箱
TEST(AABBFromPositions, NoPositionsGiveAnEmptyBoxAtTheOrigin)
{
    const NS::AABB box = NS::AABBFromPositions(nullptr, 0u, sizeof(NS::Vector3));

    EXPECT_FLOAT_EQ(box.Center.x, 0.0f);
    EXPECT_FLOAT_EQ(box.Center.y, 0.0f);
    EXPECT_FLOAT_EQ(box.Center.z, 0.0f);
    EXPECT_FLOAT_EQ(box.Extents.x, 0.0f);
    EXPECT_FLOAT_EQ(box.Extents.y, 0.0f);
    EXPECT_FLOAT_EQ(box.Extents.z, 0.0f);
}
