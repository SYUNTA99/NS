#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/MaterialFile.h"
#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Gfx/Render/DrawConstants.h"

#include <gtest/gtest.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// .mat が選ぶ面のシェーダーの出力を見る。窓の要らないソフトウェアの描画装置 (WARP) で 4x4 の浮動小数の
// 描き先へ全画面の三角形を描き、中のピクセルを読む。3 つの頂点に同じ値を渡すので、どのピクセルにも同じ
// uv・法線・世界の位置が届く

namespace
{
    // 描く 1 回の物ごとの定数とビューごとの定数の組。シェーダの b0 と b3 に差す
    struct DrawValues
    {
        NS::Gfx::ObjectConstants object{};
        NS::Gfx::ViewConstants view{};
    };

    using Microsoft::WRL::ComPtr;

    constexpr std::array<float, 4> k_Cleared{0.25f, 0.5f, 0.75f, -1.0f};

    // lit.ps.hlsl の MaterialParams と同じ並び
    struct LitParams
    {
        NS::uint32 worldProjection = 0u;
        float textureScale = 1.0f;
        float alphaCutoff = 0.0f;
        float pad = 0.0f;
    };
    static_assert(sizeof(LitParams) == 16);

    // unlit.ps.hlsl の MaterialParams と同じ並び
    struct UnlitParams
    {
        std::array<float, 3> fixedColor{1.0f, 1.0f, 1.0f};
        NS::uint32 useFixedColor = 0u;
    };
    static_assert(sizeof(UnlitParams) == 16);

    // soft_circle.ps.hlsl の MaterialParams と同じ並び
    struct SoftCircleParams
    {
        float strength = 1.0f;
        std::array<float, 3> pad{};
    };
    static_assert(sizeof(SoftCircleParams) == 16);

    struct SurfaceInput
    {
        float u = 0.0f;
        float v = 0.0f;
        NS::Vector3 worldPos{};
        NS::Vector3 normal{0.0f, 1.0f, 0.0f};
    };

    struct RigVertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.5f;
        float u = 0.0f;
        float v = 0.0f;
        NS::Vector3 normal{};
        NS::Vector3 worldPos{};
    };

    struct Pixel
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 0.0f;
    };

    constexpr std::string_view k_PassThroughVertexShader = R"HLSL(
struct VSIn
{
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 worldPos : TEXCOORD1;
};
struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 worldNormal : NORMAL;
    float3 worldPos : TEXCOORD1;
};
VSOut VSMain(VSIn input)
{
    VSOut output;
    output.pos = float4(input.pos, 1.0);
    output.uv = input.uv;
    output.worldNormal = input.normal;
    output.worldPos = input.worldPos;
    return output;
}
)HLSL";

    std::string BlobText(const ComPtr<ID3DBlob>& blob)
    {
        if (blob == nullptr)
        {
            return "コンパイルの文が無い (ファイルが無いかもしれない)";
        }
        return std::string(static_cast<const char*>(blob->GetBufferPointer()), blob->GetBufferSize());
    }

    ComPtr<ID3D11Buffer> MakeConstantBuffer(ID3D11Device& device, std::span<const std::byte> bytes)
    {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(bytes.size());
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        const D3D11_SUBRESOURCE_DATA data{bytes.data(), 0, 0};
        ComPtr<ID3D11Buffer> buffer;
        (void)device.CreateBuffer(&desc, &data, buffer.GetAddressOf());
        return buffer;
    }

    class SurfaceShaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const HRESULT created = ::D3D11CreateDevice(nullptr,
                                                        D3D_DRIVER_TYPE_WARP,
                                                        nullptr,
                                                        0,
                                                        nullptr,
                                                        0,
                                                        D3D11_SDK_VERSION,
                                                        m_device.GetAddressOf(),
                                                        nullptr,
                                                        m_context.GetAddressOf());
            if (FAILED(created))
            {
                GTEST_SKIP() << "WARP の描画装置を作れなかった";
            }

            ComPtr<ID3DBlob> vertexBlob;
            ComPtr<ID3DBlob> errors;
            const HRESULT compiled = ::D3DCompile(k_PassThroughVertexShader.data(),
                                                  k_PassThroughVertexShader.size(),
                                                  "surface_shader_test",
                                                  nullptr,
                                                  nullptr,
                                                  "VSMain",
                                                  "vs_5_0",
                                                  0,
                                                  0,
                                                  vertexBlob.GetAddressOf(),
                                                  errors.GetAddressOf());
            ASSERT_TRUE(SUCCEEDED(compiled)) << BlobText(errors);
            ASSERT_TRUE(SUCCEEDED(m_device->CreateVertexShader(
                vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, m_vertexShader.GetAddressOf())));
            const D3D11_INPUT_ELEMENT_DESC layout[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            ASSERT_TRUE(SUCCEEDED(m_device->CreateInputLayout(layout,
                                                              4,
                                                              vertexBlob->GetBufferPointer(),
                                                              vertexBlob->GetBufferSize(),
                                                              m_layout.GetAddressOf())));

            D3D11_TEXTURE2D_DESC targetDesc{};
            targetDesc.Width = 4;
            targetDesc.Height = 4;
            targetDesc.MipLevels = 1;
            targetDesc.ArraySize = 1;
            targetDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            targetDesc.SampleDesc.Count = 1;
            targetDesc.Usage = D3D11_USAGE_DEFAULT;
            targetDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
            ASSERT_TRUE(SUCCEEDED(m_device->CreateTexture2D(&targetDesc, nullptr, m_target.GetAddressOf())));
            ASSERT_TRUE(SUCCEEDED(m_device->CreateRenderTargetView(m_target.Get(), nullptr, m_targetView.GetAddressOf())));
            targetDesc.Usage = D3D11_USAGE_STAGING;
            targetDesc.BindFlags = 0;
            targetDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ASSERT_TRUE(SUCCEEDED(m_device->CreateTexture2D(&targetDesc, nullptr, m_staging.GetAddressOf())));

            // 面のシェーダーの .mat は LinearWrap で読む。ここでは uv の属するテクセルの値をそのまま見るため点で読む
            D3D11_SAMPLER_DESC samplerDesc{};
            samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
            samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
            samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
            ASSERT_TRUE(SUCCEEDED(m_device->CreateSamplerState(&samplerDesc, m_sampler.GetAddressOf())));
        }

        ComPtr<ID3D11PixelShader> CompilePixelShader(const std::filesystem::path& path)
        {
            ComPtr<ID3DBlob> blob;
            ComPtr<ID3DBlob> errors;
            const HRESULT compiled = ::D3DCompileFromFile(path.c_str(),
                                                          nullptr,
                                                          D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                                          "PSMain",
                                                          "ps_5_0",
                                                          D3DCOMPILE_ENABLE_STRICTNESS,
                                                          0,
                                                          blob.GetAddressOf(),
                                                          errors.GetAddressOf());
            if (FAILED(compiled))
            {
                ADD_FAILURE() << path.string() << " をコンパイルできない: " << BlobText(errors);
                return nullptr;
            }
            ComPtr<ID3D11PixelShader> shader;
            EXPECT_TRUE(SUCCEEDED(m_device->CreatePixelShader(
                blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, shader.GetAddressOf())));
            return shader;
        }

        // 2x2 のテクスチャ。rgba は左上・右上・左下・右下の順に、1 テクセル 4 バイト
        ComPtr<ID3D11ShaderResourceView> MakeTexture(const std::array<NS::uint8, 16>& rgba)
        {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = 2;
            desc.Height = 2;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            const D3D11_SUBRESOURCE_DATA data{rgba.data(), 8, 0};
            ComPtr<ID3D11Texture2D> texture;
            EXPECT_TRUE(SUCCEEDED(m_device->CreateTexture2D(&desc, &data, texture.GetAddressOf())));
            ComPtr<ID3D11ShaderResourceView> view;
            EXPECT_TRUE(SUCCEEDED(m_device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf())));
            return view;
        }

        Pixel Draw(ID3D11PixelShader* shader,
                   const SurfaceInput& input,
                   const DrawValues& frame,
                   std::span<const std::byte> materialParams,
                   ID3D11ShaderResourceView* texture)
        {
            const std::array<RigVertex, 3> vertices{
                RigVertex{-1.0f, -1.0f, 0.5f, input.u, input.v, input.normal, input.worldPos},
                RigVertex{-1.0f, 3.0f, 0.5f, input.u, input.v, input.normal, input.worldPos},
                RigVertex{3.0f, -1.0f, 0.5f, input.u, input.v, input.normal, input.worldPos},
            };
            D3D11_BUFFER_DESC vertexDesc{};
            vertexDesc.ByteWidth = static_cast<UINT>(sizeof(RigVertex) * vertices.size());
            vertexDesc.Usage = D3D11_USAGE_DEFAULT;
            vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            const D3D11_SUBRESOURCE_DATA vertexData{vertices.data(), 0, 0};
            ComPtr<ID3D11Buffer> vertexBuffer;
            EXPECT_TRUE(SUCCEEDED(m_device->CreateBuffer(&vertexDesc, &vertexData, vertexBuffer.GetAddressOf())));

            const ComPtr<ID3D11Buffer> objectBuffer = MakeConstantBuffer(
                *m_device.Get(), std::as_bytes(std::span<const NS::Gfx::ObjectConstants, 1>(&frame.object, 1)));
            const ComPtr<ID3D11Buffer> viewBuffer = MakeConstantBuffer(
                *m_device.Get(), std::as_bytes(std::span<const NS::Gfx::ViewConstants, 1>(&frame.view, 1)));
            ComPtr<ID3D11Buffer> materialBuffer;
            if (!materialParams.empty())
            {
                materialBuffer = MakeConstantBuffer(*m_device.Get(), materialParams);
            }

            m_context->ClearRenderTargetView(m_targetView.Get(), k_Cleared.data());
            ID3D11RenderTargetView* targets[] = {m_targetView.Get()};
            m_context->OMSetRenderTargets(1, targets, nullptr);
            const D3D11_VIEWPORT viewport{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
            m_context->RSSetViewports(1, &viewport);

            const UINT stride = sizeof(RigVertex);
            const UINT offset = 0;
            ID3D11Buffer* vertexBuffers[] = {vertexBuffer.Get()};
            m_context->IASetInputLayout(m_layout.Get());
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_context->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
            m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
            m_context->PSSetShader(shader, nullptr, 0);
            ID3D11Buffer* objectBuffers[] = {objectBuffer.Get()};
            ID3D11Buffer* viewBuffers[] = {viewBuffer.Get()};
            ID3D11Buffer* materialBuffers[] = {materialBuffer.Get()};
            m_context->PSSetConstantBuffers(NS::Gfx::k_DrawConstantsSlot, 1, objectBuffers);
            m_context->PSSetConstantBuffers(NS::Gfx::k_ViewConstantsSlot, 1, viewBuffers);
            m_context->PSSetConstantBuffers(2, 1, materialBuffers);
            ID3D11ShaderResourceView* views[] = {texture};
            ID3D11SamplerState* samplers[] = {m_sampler.Get()};
            m_context->PSSetShaderResources(0, 1, views);
            m_context->PSSetSamplers(0, 1, samplers);
            m_context->Draw(3, 0);

            m_context->CopyResource(m_staging.Get(), m_target.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            Pixel pixel{};
            if (SUCCEEDED(m_context->Map(m_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            {
                const std::byte* row = static_cast<const std::byte*>(mapped.pData) + mapped.RowPitch;
                const float* values = reinterpret_cast<const float*>(row) + 4;
                pixel = Pixel{values[0], values[1], values[2], values[3]};
                m_context->Unmap(m_staging.Get(), 0);
            }
            else
            {
                ADD_FAILURE() << "描き先を読めなかった";
            }
            return pixel;
        }

        ComPtr<ID3D11Device> m_device;
        ComPtr<ID3D11DeviceContext> m_context;
        ComPtr<ID3D11VertexShader> m_vertexShader;
        ComPtr<ID3D11InputLayout> m_layout;
        ComPtr<ID3D11Texture2D> m_target;
        ComPtr<ID3D11RenderTargetView> m_targetView;
        ComPtr<ID3D11Texture2D> m_staging;
        ComPtr<ID3D11SamplerState> m_sampler;
    };

    template <typename Params>
    std::span<const std::byte> ParamBytes(const Params& params)
    {
        return std::as_bytes(std::span<const Params, 1>(&params, 1));
    }

    DrawValues WhiteLightFrame()
    {
        DrawValues frame{};
        frame.view.lightDir = NS::Vector3{0.0f, -1.0f, 0.0f};
        frame.object.baseColor = NS::Vector3{1.0f, 1.0f, 1.0f};
        return frame;
    }

    void ExpectSameColor(const Pixel& actual, const Pixel& expected)
    {
        EXPECT_NEAR(actual.r, expected.r, 1.0e-3f);
        EXPECT_NEAR(actual.g, expected.g, 1.0e-3f);
        EXPECT_NEAR(actual.b, expected.b, 1.0e-3f);
    }

    bool IsCleared(const Pixel& pixel)
    {
        return pixel.r == k_Cleared[0] && pixel.g == k_Cleared[1] && pixel.b == k_Cleared[2] && pixel.a == k_Cleared[3];
    }

    // 暗いテクセルと明るいテクセルの市松
    constexpr std::array<NS::uint8, 16> k_CheckerTexels{
        64, 64, 64, 255, 255, 255, 255, 255, 255, 255, 255, 255, 64, 64, 64, 255};
} // namespace

// worldProjection が 1 の時は uv を見ず、面に沿った世界の位置 × textureScale で貼る
// stone.mat の 0.16 は 2x2 の市松 1 枚で 6.25 m、四角 1 つで 3.125 m
TEST_F(SurfaceShaderTest, LitTilesByTheWorldPositionAlongTheSurface)
{
    const ComPtr<ID3D11PixelShader> shader = CompilePixelShader(L"Shaders/lit.ps.hlsl");
    ASSERT_NE(shader, nullptr);
    const ComPtr<ID3D11ShaderResourceView> texture = MakeTexture(k_CheckerTexels);
    const DrawValues frame = WhiteLightFrame();

    const LitParams byUv{};
    const Pixel dark = Draw(shader.Get(), SurfaceInput{.u = 0.25f, .v = 0.25f}, frame, ParamBytes(byUv), texture.Get());
    const Pixel bright = Draw(shader.Get(), SurfaceInput{.u = 0.75f, .v = 0.25f}, frame, ParamBytes(byUv), texture.Get());
    EXPECT_GT(bright.r, dark.r + 0.1f);

    const LitParams byWorld{.worldProjection = 1u, .textureScale = 0.16f};
    const NS::Vector3 up{0.0f, 1.0f, 0.0f};
    const NS::Vector3 side{1.0f, 0.0f, 0.0f};

    // 床は XZ で読み、uv の違いは結果を変えない。四角の境は 3.125 m
    ExpectSameColor(Draw(shader.Get(),
                         SurfaceInput{.u = 0.75f, .v = 0.25f, .worldPos = NS::Vector3{3.0f, 9.0f, 1.0f}, .normal = up},
                         frame,
                         ParamBytes(byWorld),
                         texture.Get()),
                    dark);
    ExpectSameColor(Draw(shader.Get(),
                         SurfaceInput{.u = 0.25f, .v = 0.25f, .worldPos = NS::Vector3{3.2f, 9.0f, 1.0f}, .normal = up},
                         frame,
                         ParamBytes(byWorld),
                         texture.Get()),
                    bright);

    // 横の面は面に沿った水平の向きと高さで読む。光の当たり方が床と違うので、同じ法線の uv の描画と比べる
    const Pixel sideDark =
        Draw(shader.Get(), SurfaceInput{.u = 0.25f, .v = 0.25f, .normal = side}, frame, ParamBytes(byUv), texture.Get());
    const Pixel sideBright =
        Draw(shader.Get(), SurfaceInput{.u = 0.75f, .v = 0.25f, .normal = side}, frame, ParamBytes(byUv), texture.Get());
    ExpectSameColor(
        Draw(shader.Get(),
             SurfaceInput{.u = 0.75f, .v = 0.25f, .worldPos = NS::Vector3{9.0f, 1.0f, 1.0f}, .normal = side},
             frame,
             ParamBytes(byWorld),
             texture.Get()),
        sideDark);
    ExpectSameColor(
        Draw(shader.Get(),
             SurfaceInput{.u = 0.25f, .v = 0.25f, .worldPos = NS::Vector3{9.0f, 1.0f, 4.0f}, .normal = side},
             frame,
             ParamBytes(byWorld),
             texture.Get()),
        sideBright);

    // textureScale は貼る大きさを変える
    const LitParams doubled{.worldProjection = 1u, .textureScale = 0.32f};
    const SurfaceInput sameSpot{.worldPos = NS::Vector3{2.0f, 0.0f, 1.0f}, .normal = up};
    ExpectSameColor(Draw(shader.Get(), sameSpot, frame, ParamBytes(byWorld), texture.Get()), dark);
    ExpectSameColor(Draw(shader.Get(), sameSpot, frame, ParamBytes(doubled), texture.Get()), bright);
}

// alphaCutoff より薄いテクセルは描かない。0 の時は全部を描く
TEST_F(SurfaceShaderTest, LitDiscardsTexelsThinnerThanAlphaCutoff)
{
    const ComPtr<ID3D11PixelShader> shader = CompilePixelShader(L"Shaders/lit.ps.hlsl");
    ASSERT_NE(shader, nullptr);
    const ComPtr<ID3D11ShaderResourceView> texture =
        MakeTexture({255, 255, 255, 0, 255, 255, 255, 255, 255, 255, 255, 0, 255, 255, 255, 255});
    const DrawValues frame = WhiteLightFrame();
    const SurfaceInput thin{.u = 0.25f, .v = 0.25f};
    const SurfaceInput solid{.u = 0.75f, .v = 0.25f};

    const LitParams cutout{.alphaCutoff = 0.5f};
    EXPECT_TRUE(IsCleared(Draw(shader.Get(), thin, frame, ParamBytes(cutout), texture.Get())));
    EXPECT_FALSE(IsCleared(Draw(shader.Get(), solid, frame, ParamBytes(cutout), texture.Get())));

    const LitParams noCutout{};
    EXPECT_FALSE(IsCleared(Draw(shader.Get(), thin, frame, ParamBytes(noCutout), texture.Get())));
}

// カメラから 100 m より手前は変わらず、遠くの面はかすみの色へ寄る
TEST_F(SurfaceShaderTest, LitFadesTowardTheHazeColorFarFromTheCamera)
{
    const ComPtr<ID3D11PixelShader> shader = CompilePixelShader(L"Shaders/lit.ps.hlsl");
    ASSERT_NE(shader, nullptr);
    const ComPtr<ID3D11ShaderResourceView> texture = MakeTexture(k_CheckerTexels);
    const LitParams params{};
    DrawValues frame = WhiteLightFrame();
    frame.view.cameraPosition = NS::Vector3{500.0f, 0.0f, 0.0f};
    const SurfaceInput near0{.u = 0.25f, .v = 0.25f, .worldPos = NS::Vector3{500.0f, 0.0f, 0.0f}};
    const SurfaceInput near99{.u = 0.25f, .v = 0.25f, .worldPos = NS::Vector3{500.0f, 0.0f, 99.0f}};
    const SurfaceInput far400{.u = 0.25f, .v = 0.25f, .worldPos = NS::Vector3{500.0f, 0.0f, 400.0f}};

    const Pixel atCamera = Draw(shader.Get(), near0, frame, ParamBytes(params), texture.Get());
    ExpectSameColor(Draw(shader.Get(), near99, frame, ParamBytes(params), texture.Get()), atCamera);
    // 暗いテクセルは、かすみの明るい色へ持ち上がる
    const Pixel farPixel = Draw(shader.Get(), far400, frame, ParamBytes(params), texture.Get());
    EXPECT_GT(farPixel.r, atCamera.r + 0.05f);
    EXPECT_GT(farPixel.b, atCamera.b + 0.05f);
}

// シーンの光の色と環境光は lit の出力に届く
TEST_F(SurfaceShaderTest, LitFollowsTheLightAndAmbientColors)
{
    const ComPtr<ID3D11PixelShader> shader = CompilePixelShader(L"Shaders/lit.ps.hlsl");
    ASSERT_NE(shader, nullptr);
    const ComPtr<ID3D11ShaderResourceView> texture =
        MakeTexture({255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255});
    const LitParams params{};
    const SurfaceInput input{.u = 0.5f, .v = 0.5f};

    DrawValues dark = WhiteLightFrame();
    dark.view.lightColor = NS::Vector3{0.0f, 0.0f, 0.0f};
    dark.view.ambientColor = NS::Vector3{0.0f, 0.0f, 0.0f};
    const Pixel darkPixel = Draw(shader.Get(), input, dark, ParamBytes(params), texture.Get());

    DrawValues redLight = dark;
    redLight.view.lightColor = NS::Vector3{0.5f, 0.0f, 0.0f};
    const Pixel redPixel = Draw(shader.Get(), input, redLight, ParamBytes(params), texture.Get());
    EXPECT_GT(redPixel.r, darkPixel.r + 0.1f);
    EXPECT_NEAR(redPixel.g, darkPixel.g, 1.0e-3f);

    DrawValues greenAmbient = dark;
    greenAmbient.view.ambientColor = NS::Vector3{0.0f, 0.5f, 0.0f};
    const Pixel greenPixel = Draw(shader.Get(), input, greenAmbient, ParamBytes(params), texture.Get());
    EXPECT_GT(greenPixel.g, darkPixel.g + 0.1f);
    EXPECT_NEAR(greenPixel.r, darkPixel.r, 1.0e-3f);
}

// unlit は baseColor を出す。useFixedColor の時は baseColor を無視して fixedColor を出す
TEST_F(SurfaceShaderTest, UnlitReplacesTheActorColorWithFixedColor)
{
    const ComPtr<ID3D11PixelShader> shader = CompilePixelShader(L"Shaders/unlit.ps.hlsl");
    ASSERT_NE(shader, nullptr);
    DrawValues red{};
    red.object.baseColor = NS::Vector3{1.0f, 0.0f, 0.0f};
    DrawValues blue{};
    blue.object.baseColor = NS::Vector3{0.0f, 0.0f, 1.0f};

    const UnlitParams actorColor{};
    const Pixel redPixel = Draw(shader.Get(), SurfaceInput{}, red, ParamBytes(actorColor), nullptr);
    const Pixel bluePixel = Draw(shader.Get(), SurfaceInput{}, blue, ParamBytes(actorColor), nullptr);
    EXPECT_GT(redPixel.r, bluePixel.r + 0.1f);
    EXPECT_GT(bluePixel.b, redPixel.b + 0.1f);

    const UnlitParams fixedGreen{.fixedColor = {0.0f, 1.0f, 0.0f}, .useFixedColor = 1u};
    const Pixel fromRed = Draw(shader.Get(), SurfaceInput{}, red, ParamBytes(fixedGreen), nullptr);
    const Pixel fromBlue = Draw(shader.Get(), SurfaceInput{}, blue, ParamBytes(fixedGreen), nullptr);
    ExpectSameColor(fromRed, fromBlue);
    EXPECT_GT(fromRed.g, 0.5f);
    EXPECT_NEAR(fromRed.r, 0.0f, 1.0e-3f);
}

// 読めないマテリアルに使う missing.mat は、アクターの色が何でもピンクを出す
TEST_F(SurfaceShaderTest, MissingMaterialStaysPinkWhateverTheActorColorIs)
{
    std::ifstream file("Assets/Materials/missing.mat");
    ASSERT_TRUE(file.is_open());
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    NS::MaterialFileDesc desc;
    std::string error;
    ASSERT_TRUE(NS::ParseMaterialJson(text, desc, error)) << error;
    const ComPtr<ID3D11PixelShader> shader = CompilePixelShader(desc.pixelShader);
    ASSERT_NE(shader, nullptr);

    ASSERT_TRUE(desc.params.contains("fixedColor"));
    ASSERT_TRUE(desc.params.contains("useFixedColor"));
    const std::vector<double>& color = desc.params.at("fixedColor").numbers;
    ASSERT_EQ(color.size(), 3u);
    const UnlitParams params{
        .fixedColor = {static_cast<float>(color[0]), static_cast<float>(color[1]), static_cast<float>(color[2])},
        .useFixedColor = static_cast<NS::uint32>(desc.params.at("useFixedColor").numbers[0]),
    };

    DrawValues green{};
    green.object.baseColor = NS::Vector3{0.0f, 1.0f, 0.0f};
    const Pixel pixel = Draw(shader.Get(), SurfaceInput{}, green, ParamBytes(params), nullptr);
    EXPECT_GT(pixel.r, 0.5f);
    EXPECT_NEAR(pixel.g, 0.0f, 1.0e-3f);
    EXPECT_NEAR(pixel.r, pixel.b, 1.0e-3f);
}

// soft_circle は中心で濃さ g_opacity × strength、外へ二乗で薄くなり、半径 0.5 で 0 になる。色は baseColor
TEST_F(SurfaceShaderTest, SoftCircleFadesFromOpacityAtTheCenter)
{
    const ComPtr<ID3D11PixelShader> shader = CompilePixelShader(L"Shaders/soft_circle.ps.hlsl");
    ASSERT_NE(shader, nullptr);
    DrawValues frame{};
    frame.object.baseColor = NS::Vector3{0.0f, 0.0f, 0.0f};
    frame.object.opacity = 0.4f;

    const SoftCircleParams params{};
    const std::span<const std::byte> strengthOne = ParamBytes(params);
    const Pixel center = Draw(shader.Get(), SurfaceInput{.u = 0.5f, .v = 0.5f}, frame, strengthOne, nullptr);
    EXPECT_NEAR(center.a, 0.4f, 1.0e-3f);
    EXPECT_NEAR(center.r, 0.0f, 1.0e-3f);
    const Pixel halfway = Draw(shader.Get(), SurfaceInput{.u = 0.75f, .v = 0.5f}, frame, strengthOne, nullptr);
    EXPECT_NEAR(halfway.a, 0.1f, 1.0e-3f);
    const Pixel outside = Draw(shader.Get(), SurfaceInput{.u = 1.0f, .v = 0.5f}, frame, strengthOne, nullptr);
    EXPECT_NEAR(outside.a, 0.0f, 1.0e-3f);

    frame.object.baseColor = NS::Vector3{1.0f, 1.0f, 1.0f};
    frame.object.opacity = 1.0f;
    const Pixel white = Draw(shader.Get(), SurfaceInput{.u = 0.5f, .v = 0.5f}, frame, strengthOne, nullptr);
    EXPECT_NEAR(white.a, 1.0f, 1.0e-3f);
    EXPECT_GT(white.r, 0.5f);

    // 物ごとの濃さでなく .mat で決める濃さは strength で掛ける
    const SoftCircleParams weak{.strength = 0.25f};
    const Pixel faint = Draw(shader.Get(), SurfaceInput{.u = 0.5f, .v = 0.5f}, frame, ParamBytes(weak), nullptr);
    EXPECT_NEAR(faint.a, 0.25f, 1.0e-3f);
}
