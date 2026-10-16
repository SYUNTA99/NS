#include "NSlib/Core/Geometry/GltfParser.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// 外部のバッファ (.bin) を指す .gltf を、渡した読み方で読む事を縛る
// 外部のバッファを cgltf が自分で開くと、AssetManager の読み方を通らない

namespace
{
    // 外部のバッファを 1 つだけ指す。埋め込みのバッファは読み方を通らないので使わない
    const std::string k_TriangleGltf = R"({"asset":{"version":"2.0"},)"
                                       R"("buffers":[{"byteLength":36,"uri":"tri.bin"}],)"
                                       R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],)"
                                       R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],)"
                                       R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
                                       R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";

    std::vector<std::byte> TriangleBin()
    {
        const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        std::vector<std::byte> bytes(sizeof(positions));
        std::memcpy(bytes.data(), positions, sizeof(positions));
        return bytes;
    }

    std::span<const std::byte> AsBytes(const std::string& text)
    {
        return std::as_bytes(std::span<const char>{text.data(), text.size()});
    }
} // namespace

// cgltf が自分で開くと、渡した関数は呼ばれず、作業フォルダに Models/tri.bin も無いので読めない
TEST(GltfExternalBuffer, ReadsTheBinThroughTheGivenFunction)
{
    std::vector<std::string> asked;
    NS::GltfFileDesc file{};
    file.bytes = AsBytes(k_TriangleGltf);
    file.name = "triangle.gltf";
    file.baseDirectory = "Models";
    file.readFile = [&](std::string_view path) -> std::optional<std::vector<std::byte>> {
        asked.emplace_back(path);
        return TriangleBin();
    };

    const NS::MeshGeometry geom = NS::ParseGltfMesh(file);

    EXPECT_EQ(asked, (std::vector<std::string>{"Models/tri.bin"}));
    EXPECT_EQ(geom.vertices.size(), 3u);
}

// 読めない .bin を読めた事にして、位置が全部 0 の形を返さない
TEST(GltfExternalBuffer, FailsWhenTheFunctionCannotRead)
{
    NS::GltfFileDesc file{};
    file.bytes = AsBytes(k_TriangleGltf);
    file.name = "triangle.gltf";
    file.baseDirectory = "Models";
    file.readFile = [](std::string_view) -> std::optional<std::vector<std::byte>> { return std::nullopt; };

    EXPECT_TRUE(NS::ParseGltfMesh(file).vertices.empty());
}

// 道から読む口は、本体のフォルダを基準に渡す。基準が空だと作業フォルダの tri.bin を探して読めない
TEST(GltfExternalBuffer, LoadsTheBinBesideTheFileFromAPath)
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "ns_gltf_external_buffer_test";
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "triangle.gltf", std::ios::binary) << k_TriangleGltf;
    {
        const std::vector<std::byte> bin = TriangleBin();
        std::ofstream(directory / "tri.bin", std::ios::binary)
            .write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));
    }

    const NS::MeshGeometry geom = NS::LoadGltfMesh((directory / "triangle.gltf").generic_string());
    std::filesystem::remove_all(directory);

    EXPECT_EQ(geom.vertices.size(), 3u);
}
