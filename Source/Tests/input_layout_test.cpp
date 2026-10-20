#include "NSlib/Core/Geometry/Vertex.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Gfx/D3DStaticMesh.h"
#include "NSlib/Windows/Filesystem.h"

#include <gtest/gtest.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <system_error>

// 入力レイアウトの正は頂点シェーダーのバイトコード。1 つのメッシュを入力の違う頂点シェーダーで描いても、
// 頂点シェーダーを読み直しても、その中身に合ったレイアウトを使う事を確かめる。窓の要らない WARP の装置で回す

namespace
{
    using Microsoft::WRL::ComPtr;

    // 試しの間だけ WARP の描画装置を Gpu() に差す。作れない機械では空のまま
    struct ScopedWarpGpu
    {
        ComPtr<ID3D11Device> d3d11Device;
        ComPtr<ID3D11DeviceContext> context;

        ScopedWarpGpu()
        {
            const HRESULT hr = ::D3D11CreateDevice(nullptr,
                                                   D3D_DRIVER_TYPE_WARP,
                                                   nullptr,
                                                   0,
                                                   nullptr,
                                                   0,
                                                   D3D11_SDK_VERSION,
                                                   d3d11Device.GetAddressOf(),
                                                   nullptr,
                                                   context.GetAddressOf());
            if (FAILED(hr))
            {
                d3d11Device.Reset();
                context.Reset();
                return;
            }
            NS::Gfx::Gpu().device = d3d11Device.Get();
            NS::Gfx::Gpu().context = context.Get();
        }

        ~ScopedWarpGpu()
        {
            NS::Gfx::Gpu().device = nullptr;
            NS::Gfx::Gpu().context = nullptr;
        }

        [[nodiscard]] bool Valid() const { return d3d11Device != nullptr; }
    };

    // 位置だけを読む頂点シェーダー
    constexpr const char* k_PositionOnlyVs = R"HLSL(
struct VSInput { float3 position : POSITION; };
float4 VSMain(VSInput input) : SV_Position { return float4(input.position, 1.0); }
)HLSL";

    // 位置と法線を読む頂点シェーダー
    constexpr const char* k_PositionNormalVs = R"HLSL(
struct VSInput { float3 position : POSITION; float3 normal : NORMAL; };
float4 VSMain(VSInput input) : SV_Position { return float4(input.position + input.normal, 1.0); }
)HLSL";

    // 法線を先に、位置を後に読む頂点シェーダー。入力の並びだけが k_PositionNormalVs と違う
    constexpr const char* k_NormalPositionVs = R"HLSL(
struct VSInput { float3 normal : NORMAL; float3 position : POSITION; };
float4 VSMain(VSInput input) : SV_Position { return float4(input.position + input.normal, 1.0); }
)HLSL";

    bool WriteText(const std::string& path, const std::string& text)
    {
        return NS::OS::FileSystem::WriteAllBytes(path, std::as_bytes(std::span<const char>{text.data(), text.size()}));
    }

    // 一時のフォルダを作り、試しの終わりに消す
    struct ScopedTempDirectory
    {
        std::filesystem::path path;

        ScopedTempDirectory()
        {
            path = std::filesystem::temp_directory_path() /
                   ("ns_input_layout_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::error_code error;
            std::filesystem::create_directories(path, error);
        }

        ~ScopedTempDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }

        [[nodiscard]] std::string File(const char* name) const { return (path / name).generic_string(); }
    };

    std::unique_ptr<NS::Gfx::D3DStaticMesh> MakeTriangle()
    {
        const std::array<NS::StaticVertex, 3> vertices = {
            NS::StaticVertex{NS::Vector3{0.0f, 0.0f, 0.0f}, NS::Vector2{0.0f, 0.0f}, NS::Vector3{0.0f, 0.0f, 1.0f}},
            NS::StaticVertex{NS::Vector3{1.0f, 0.0f, 0.0f}, NS::Vector2{1.0f, 0.0f}, NS::Vector3{0.0f, 0.0f, 1.0f}},
            NS::StaticVertex{NS::Vector3{0.0f, 1.0f, 0.0f}, NS::Vector2{0.0f, 1.0f}, NS::Vector3{0.0f, 0.0f, 1.0f}},
        };
        const std::array<NS::uint32, 3> indices = {0, 1, 2};
        NS::Gfx::D3DStaticMeshDesc desc{};
        desc.vertices = vertices.data();
        desc.vertexCount = vertices.size();
        desc.indices = indices.data();
        desc.indexCount = indices.size();
        return NS::Gfx::D3DStaticMesh::Create(desc);
    }
} // namespace

TEST(InputLayout, EachVertexShaderGetsItsOwnLayoutOnOneMesh)
{
    const ScopedWarpGpu gpu;
    if (!gpu.Valid())
    {
        GTEST_SKIP() << "WARP の描画装置を作れない";
    }
    const ScopedTempDirectory directory;
    const std::string positionPath = directory.File("position.vs.hlsl");
    const std::string normalPath = directory.File("normal.vs.hlsl");
    ASSERT_TRUE(WriteText(positionPath, k_PositionOnlyVs));
    ASSERT_TRUE(WriteText(normalPath, k_PositionNormalVs));
    const std::unique_ptr<NS::Gfx::D3DShader> positionVs = NS::Gfx::D3DShader::Create(positionPath);
    const std::unique_ptr<NS::Gfx::D3DShader> normalVs = NS::Gfx::D3DShader::Create(normalPath);
    ASSERT_FALSE(positionVs->IsUsingFallback());
    ASSERT_FALSE(normalVs->IsUsingFallback());
    EXPECT_NE(positionVs->BytecodeId(), normalVs->BytecodeId());

    const std::unique_ptr<NS::Gfx::D3DStaticMesh> mesh = MakeTriangle();
    ASSERT_TRUE(mesh->IsValid());

    mesh->CreateInputLayout(*positionVs);
    ID3D11InputLayout* positionLayout = mesh->InputLayout();
    mesh->CreateInputLayout(*normalVs);
    ID3D11InputLayout* normalLayout = mesh->InputLayout();
    mesh->CreateInputLayout(*positionVs);
    ID3D11InputLayout* positionLayoutAgain = mesh->InputLayout();

    ASSERT_NE(positionLayout, nullptr);
    ASSERT_NE(normalLayout, nullptr);
    EXPECT_NE(positionLayout, normalLayout);
    EXPECT_EQ(positionLayout, positionLayoutAgain);
}

TEST(InputLayout, ReloadWithChangedInputsRebuildsTheLayout)
{
    const ScopedWarpGpu gpu;
    if (!gpu.Valid())
    {
        GTEST_SKIP() << "WARP の描画装置を作れない";
    }
    const ScopedTempDirectory directory;
    const std::string path = directory.File("reload.vs.hlsl");
    ASSERT_TRUE(WriteText(path, k_PositionNormalVs));
    const std::unique_ptr<NS::Gfx::D3DShader> vs = NS::Gfx::D3DShader::Create(path);
    ASSERT_FALSE(vs->IsUsingFallback());

    const std::unique_ptr<NS::Gfx::D3DStaticMesh> mesh = MakeTriangle();
    ASSERT_TRUE(mesh->IsValid());
    mesh->CreateInputLayout(*vs);
    // 古いレイアウトを生かしたまま比べる。捨てると、新しいレイアウトが同じ番地に作られる事がある
    const ComPtr<ID3D11InputLayout> before = mesh->InputLayout();
    ASSERT_NE(before, nullptr);
    const NS::uint64 idBefore = vs->BytecodeId();

    ASSERT_TRUE(WriteText(path, k_NormalPositionVs));
    ASSERT_TRUE(vs->Reload(path));
    EXPECT_NE(vs->BytecodeId(), idBefore);

    mesh->CreateInputLayout(*vs);
    ASSERT_NE(mesh->InputLayout(), nullptr);
    EXPECT_NE(mesh->InputLayout(), before.Get());
}
