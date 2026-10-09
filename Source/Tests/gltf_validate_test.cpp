#include "NSlib/Graphics/GltfLoader.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

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

    std::filesystem::path TestDirectory()
    {
        return std::filesystem::temp_directory_path() / "ns_gltf_validate_test";
    }

    std::string WriteGltf(const std::string& name, const std::string& text)
    {
        std::filesystem::create_directories(TestDirectory());
        const std::filesystem::path path = TestDirectory() / name;
        std::ofstream(path, std::ios::binary) << text;
        return path.string();
    }

    class GltfValidate : public ::testing::Test
    {
    protected:
        void TearDown() override { std::filesystem::remove_all(TestDirectory()); }
    };
} // namespace

// 数の揃ったファイルは読める。下の試しが別の理由で空になっていない事の確かめ
TEST_F(GltfValidate, MatchingCountsLoad)
{
    const NS::Gfx::MeshGeometry geom = NS::Gfx::LoadGltfMesh(WriteGltf("matching.gltf", TriangleGltf(3)));
    EXPECT_EQ(geom.vertices.size(), 3u);
}

// TEXCOORD_0 が POSITION より少ないファイルは、読まずに空を返す
TEST_F(GltfValidate, MismatchedAttributeCountIsRejected)
{
    const NS::Gfx::MeshGeometry geom = NS::Gfx::LoadGltfMesh(WriteGltf("mismatched.gltf", TriangleGltf(1)));
    EXPECT_TRUE(geom.vertices.empty());
}
