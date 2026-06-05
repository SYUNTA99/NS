#include <gtest/gtest.h>

#include <Framework/Core/Filesystem.h>
#include <Framework/Graphics/GltfLoader.h>
#include <Framework/Graphics/Skeleton.h>
#include <Framework/Graphics/detail/gltf_skin_helpers.h>
#include <Framework/Math/Math.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace
{
    using NS::Graphics::Bone;
    using NS::Graphics::detail::ConjugateZMatrix;
    using NS::Graphics::detail::MirrorQuaternionZ;
    using NS::Graphics::detail::MirrorZ;
    using NS::Graphics::detail::NormalizeJointWeights;
    using NS::Graphics::detail::ReadColumnMajorMatrix;
    using NS::Graphics::detail::TopologicalSortBones;
    using NS::Math::Matrix;
    using NS::Math::Quaternion;
    using NS::Math::Vector3;

    constexpr float kEps = 1e-5f;

    void AppendFloat(std::vector<unsigned char>& buf, float v)
    {
        unsigned char bytes[sizeof(float)];
        std::memcpy(bytes, &v, sizeof(float));
        buf.insert(buf.end(), bytes, bytes + sizeof(float));
    }

    void AppendU16(std::vector<unsigned char>& buf, std::uint16_t v)
    {
        unsigned char bytes[sizeof(std::uint16_t)];
        std::memcpy(bytes, &v, sizeof(std::uint16_t));
        buf.insert(buf.end(), bytes, bytes + sizeof(std::uint16_t));
    }

    // 166 byte buffer: pos(3xVEC3) / weights(3xVEC4) / inverseBind(MAT4) / joints(3xVEC4 ubyte) / idx(3xUSHORT)
    std::vector<unsigned char> MakeSkinnedBufferBin()
    {
        std::vector<unsigned char> buf;
        // positions: v0(0,0,0) v1(1,0,0) v2(0,1,2)
        AppendFloat(buf, 0.0f);
        AppendFloat(buf, 0.0f);
        AppendFloat(buf, 0.0f);
        AppendFloat(buf, 1.0f);
        AppendFloat(buf, 0.0f);
        AppendFloat(buf, 0.0f);
        AppendFloat(buf, 0.0f);
        AppendFloat(buf, 1.0f);
        AppendFloat(buf, 2.0f);
        // weights: 全頂点 (1,0,0,0)
        for (int i = 0; i < 3; ++i)
        {
            AppendFloat(buf, 1.0f);
            AppendFloat(buf, 0.0f);
            AppendFloat(buf, 0.0f);
            AppendFloat(buf, 0.0f);
        }
        // inverse bind: 単位行列 (列優先・行優先とも同一)
        const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        for (float f : identity)
            AppendFloat(buf, f);
        // joints (ubyte): 全頂点 (0,0,0,0)
        for (int i = 0; i < 12; ++i)
            buf.push_back(0u);
        // indices: 0,1,2
        AppendU16(buf, 0u);
        AppendU16(buf, 1u);
        AppendU16(buf, 2u);
        return buf;
    }

    std::string SkinnedGltf()
    {
        return std::string{R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1]}],)"} +
               R"("nodes":[{"mesh":0,"skin":0},{"translation":[0,0,0]}],)" +
               R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"WEIGHTS_0":1,"JOINTS_0":3},"indices":4}]}],)" +
               R"("skins":[{"joints":[1],"inverseBindMatrices":2}],)" +
               R"("buffers":[{"uri":"ns_skinned_fixture.bin","byteLength":166}],)" + R"("bufferViews":[)" +
               R"({"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},)" +
               R"({"buffer":0,"byteOffset":36,"byteLength":48,"target":34962},)" +
               R"({"buffer":0,"byteOffset":84,"byteLength":64},)" +
               R"({"buffer":0,"byteOffset":148,"byteLength":12,"target":34962},)" +
               R"({"buffer":0,"byteOffset":160,"byteLength":6,"target":34963}],)" + R"("accessors":[)" +
               R"({"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,2]},)" +
               R"({"bufferView":1,"componentType":5126,"count":3,"type":"VEC4"},)" +
               R"({"bufferView":2,"componentType":5126,"count":1,"type":"MAT4"},)" +
               R"({"bufferView":3,"componentType":5121,"count":3,"type":"VEC4"},)" +
               R"({"bufferView":4,"componentType":5123,"count":3,"type":"SCALAR"}]})";
    }

    std::string NoSkinGltf()
    {
        return std::string{R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)"} +
               R"("nodes":[{"mesh":0}],)" +
               R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)" +
               R"("buffers":[{"uri":"ns_skinned_fixture.bin","byteLength":166}],)" + R"("bufferViews":[)" +
               R"({"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},)" +
               R"({"buffer":0,"byteOffset":160,"byteLength":6,"target":34963}],)" + R"("accessors":[)" +
               R"({"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,2]},)" +
               R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})";
    }

    std::filesystem::path WriteBinFixture(const char* name, const std::vector<unsigned char>& bytes)
    {
        const std::filesystem::path path = NS::Core::FileSystem::GetExeDirectory() / name;
        const bool ok = NS::Core::FileSystem::WriteAllBytes(
            path, std::as_bytes(std::span<const unsigned char>{bytes.data(), bytes.size()}));
        EXPECT_TRUE(ok);
        return path;
    }

    std::filesystem::path WriteTextFixture(const char* name, const std::string& content)
    {
        const std::filesystem::path path = NS::Core::FileSystem::GetExeDirectory() / name;
        const bool ok = NS::Core::FileSystem::WriteAllBytes(
            path, std::as_bytes(std::span<const char>{content.data(), content.size()}));
        EXPECT_TRUE(ok);
        return path;
    }
} // namespace

TEST(GltfSkinHelperTest, NormalizeWeightsSumsToOne)
{
    std::array<std::uint32_t, 4> joints{0, 1, 2, 3};
    std::array<float, 4> weights{1.0f, 1.0f, 0.0f, 0.0f};
    NormalizeJointWeights(joints, weights);
    EXPECT_NEAR(weights[0], 0.5f, kEps);
    EXPECT_NEAR(weights[1], 0.5f, kEps);
    EXPECT_NEAR(weights[2], 0.0f, kEps);
    EXPECT_NEAR(weights[3], 0.0f, kEps);
}

TEST(GltfSkinHelperTest, NormalizeAllZeroWeightsFallsBackToFirstBone)
{
    std::array<std::uint32_t, 4> joints{7, 8, 9, 10};
    std::array<float, 4> weights{0.0f, 0.0f, 0.0f, 0.0f};
    NormalizeJointWeights(joints, weights);
    EXPECT_EQ(joints[0], 0u);
    EXPECT_NEAR(weights[0], 1.0f, kEps);
    EXPECT_NEAR(weights[1], 0.0f, kEps);
    EXPECT_NEAR(weights[2], 0.0f, kEps);
    EXPECT_NEAR(weights[3], 0.0f, kEps);
}

TEST(GltfSkinHelperTest, MirrorZNegatesZComponent)
{
    const Vector3 r = MirrorZ(Vector3(1.0f, 2.0f, 3.0f));
    EXPECT_NEAR(r.x, 1.0f, kEps);
    EXPECT_NEAR(r.y, 2.0f, kEps);
    EXPECT_NEAR(r.z, -3.0f, kEps);
}

TEST(GltfSkinHelperTest, MirrorQuaternionZNegatesXY)
{
    const Quaternion r = MirrorQuaternionZ(Quaternion(0.1f, 0.2f, 0.3f, 0.4f));
    EXPECT_NEAR(r.x, -0.1f, kEps);
    EXPECT_NEAR(r.y, -0.2f, kEps);
    EXPECT_NEAR(r.z, 0.3f, kEps);
    EXPECT_NEAR(r.w, 0.4f, kEps);
}

TEST(GltfSkinHelperTest, ReadColumnMajorMatrixPlacesTranslationInRow3)
{
    // 列優先 glTF 行列の translation は列 3 (index 12,13,14)
    const float m[16] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 5.0f, 6.0f, 7.0f, 1.0f};
    const Matrix n = ReadColumnMajorMatrix(m);
    // 行ベクトル規約では translation は _41/_42/_43 に入る
    EXPECT_NEAR(n._41, 5.0f, kEps);
    EXPECT_NEAR(n._42, 6.0f, kEps);
    EXPECT_NEAR(n._43, 7.0f, kEps);
    const Vector3 origin = Vector3::Transform(Vector3(0.0f, 0.0f, 0.0f), n);
    EXPECT_NEAR(origin.x, 5.0f, kEps);
    EXPECT_NEAR(origin.y, 6.0f, kEps);
    EXPECT_NEAR(origin.z, 7.0f, kEps);
}

TEST(GltfSkinHelperTest, ConjugateZMatrixFlipsTranslationZ)
{
    const Matrix conjugated = ConjugateZMatrix(Matrix::CreateTranslation(1.0f, 2.0f, 3.0f));
    EXPECT_NEAR(conjugated._41, 1.0f, kEps);
    EXPECT_NEAR(conjugated._42, 2.0f, kEps);
    EXPECT_NEAR(conjugated._43, -3.0f, kEps);
    const Vector3 origin = Vector3::Transform(Vector3(0.0f, 0.0f, 0.0f), conjugated);
    EXPECT_NEAR(origin.z, -3.0f, kEps);
}

TEST(GltfSkinHelperTest, TopologicalSortPutsParentsBeforeChildren)
{
    // 入力は index0=child(parent=1), index1=root(parent=-1) で子が先
    std::vector<Bone> bones(2);
    bones[0].parentIndex = 1;
    bones[1].parentIndex = -1;
    const std::vector<std::uint32_t> remap = TopologicalSortBones(bones);

    ASSERT_EQ(bones.size(), 2u);
    EXPECT_EQ(bones[0].parentIndex, -1); // root が先頭
    EXPECT_EQ(bones[1].parentIndex, 0);  // child の親は先頭の root
    EXPECT_EQ(remap[0], 1u);             // old child -> new index 1
    EXPECT_EQ(remap[1], 0u);             // old root  -> new index 0
}

TEST(GltfSkinLoadTest, NonexistentPathIsInvalid)
{
    const auto data = NS::Graphics::LoadGltfSkinnedMesh("ns_skin_does_not_exist_7c2.gltf");
    EXPECT_FALSE(data.IsValid());
    EXPECT_TRUE(data.vertices.empty());
}

TEST(GltfSkinLoadTest, StaticMeshWithoutSkinIsInvalid)
{
    WriteBinFixture("ns_skinned_fixture.bin", MakeSkinnedBufferBin());
    const auto gltfPath = WriteTextFixture("ns_skin_noskin.gltf", NoSkinGltf());

    const auto data = NS::Graphics::LoadGltfSkinnedMesh(gltfPath.string());
    EXPECT_FALSE(data.IsValid());
    EXPECT_TRUE(data.vertices.empty());
}

TEST(GltfSkinLoadTest, LoadsSkinnedTriangleWithLeftHandedConversion)
{
    WriteBinFixture("ns_skinned_fixture.bin", MakeSkinnedBufferBin());
    const auto gltfPath = WriteTextFixture("ns_skin_happy.gltf", SkinnedGltf());

    const auto data = NS::Graphics::LoadGltfSkinnedMesh(gltfPath.string());
    ASSERT_TRUE(data.IsValid());
    EXPECT_EQ(data.vertices.size(), 3u);
    ASSERT_EQ(data.indices.size(), 3u);
    EXPECT_EQ(data.skeleton.BoneCount(), 1u);

    // 重み正規化済 + 単一ボーンなので joint index は remap 後も 0
    EXPECT_NEAR(data.vertices[0].weights[0], 1.0f, kEps);
    EXPECT_EQ(data.vertices[0].joints[0], 0u);

    // v2(0,1,2) は Z 反転で z=-2
    EXPECT_NEAR(data.vertices[2].position.x, 0.0f, kEps);
    EXPECT_NEAR(data.vertices[2].position.y, 1.0f, kEps);
    EXPECT_NEAR(data.vertices[2].position.z, -2.0f, kEps);

    // winding 反転で [0,1,2] -> [0,2,1]
    EXPECT_EQ(data.indices[0], 0u);
    EXPECT_EQ(data.indices[1], 2u);
    EXPECT_EQ(data.indices[2], 1u);
}
