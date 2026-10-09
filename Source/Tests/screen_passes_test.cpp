#include "NSlib/Graphics/ScreenPasses.h"

#include <gtest/gtest.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

// WARP の描画装置で fade.vs.hlsl と screen_distortion.ps.hlsl をコンパイルし、
// 模様の絵を入れて書いた画素を読む

namespace
{
    using Microsoft::WRL::ComPtr;

    constexpr UINT k_Size = 32;

    struct Rgba
    {
        int r = 0;
        int g = 0;
        int b = 0;
    };

    // 入れる絵の模様。押した先の色が線形に拾われても比べられるよう、x と y で滑らかに変える
    Rgba Pattern(int x, int y)
    {
        return Rgba{x * 8, y * 8, 128};
    }

    Rgba SampleLinear(float px, float py)
    {
        const float fx = px - 0.5f;
        const float fy = py - 0.5f;
        const float x0f = std::floor(fx);
        const float y0f = std::floor(fy);
        const float tx = fx - x0f;
        const float ty = fy - y0f;
        const int maxIndex = static_cast<int>(k_Size) - 1;
        const int x0 = std::clamp(static_cast<int>(x0f), 0, maxIndex);
        const int x1 = std::clamp(static_cast<int>(x0f) + 1, 0, maxIndex);
        const int y0 = std::clamp(static_cast<int>(y0f), 0, maxIndex);
        const int y1 = std::clamp(static_cast<int>(y0f) + 1, 0, maxIndex);
        const auto mix = [&](int Rgba::* channel) {
            const float top = (Pattern(x0, y0).*channel) * (1.0f - tx) + (Pattern(x1, y0).*channel) * tx;
            const float bottom = (Pattern(x0, y1).*channel) * (1.0f - tx) + (Pattern(x1, y1).*channel) * tx;
            return static_cast<int>(std::lround(top * (1.0f - ty) + bottom * ty));
        };
        return Rgba{mix(&Rgba::r), mix(&Rgba::g), mix(&Rgba::b)};
    }

    Rgba ExpectedPixel(const NS::Gfx::ScreenDistortionCB& constants, int x, int y, bool& distorted)
    {
        distorted = false;
        float px = static_cast<float>(x) + 0.5f;
        float py = static_cast<float>(y) + 0.5f;
        if (constants.ring[3] > 0.0f && constants.ringHalfWidth > 0.0f)
        {
            const float dx = px - constants.ring[0];
            const float dy = py - constants.ring[1];
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float slot = (distance - constants.ring[2]) / constants.ringHalfWidth;
            if (std::abs(slot) < 1.0f && distance > 1.0e-3f)
            {
                const float crest = 1.0f - slot * slot;
                px -= dx / distance * constants.ring[3] * crest * crest;
                py -= dy / distance * constants.ring[3] * crest * crest;
                distorted = true;
            }
        }
        if (distorted)
        {
            return SampleLinear(px, py);
        }
        return Pattern(x, y);
    }

    // 歪みのシェーダーで全画面を 1 回描き、書いた画素を返す。描画装置かコンパイルが無い時は空
    class DistortionRig
    {
    public:
        DistortionRig()
        {
            HRESULT hr = ::D3D11CreateDevice(nullptr,
                                             D3D_DRIVER_TYPE_WARP,
                                             nullptr,
                                             0,
                                             nullptr,
                                             0,
                                             D3D11_SDK_VERSION,
                                             m_device.GetAddressOf(),
                                             nullptr,
                                             m_context.GetAddressOf());
            if (FAILED(hr))
            {
                m_device.Reset();
                return;
            }
            ComPtr<ID3DBlob> vsBlob = Compile(L"Shaders/fade.vs.hlsl", "VSMain", "vs_5_0");
            ComPtr<ID3DBlob> psBlob = Compile(L"Shaders/screen_distortion.ps.hlsl", "PSMain", "ps_5_0");
            if (vsBlob == nullptr || psBlob == nullptr)
            {
                return;
            }
            (void)m_device->CreateVertexShader(
                vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vs.GetAddressOf());
            (void)m_device->CreatePixelShader(
                psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_ps.GetAddressOf());
            D3D11_SAMPLER_DESC samplerDesc{};
            samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
            (void)m_device->CreateSamplerState(&samplerDesc, m_sampler.GetAddressOf());
            // 全画面三角形は裏向きなので面を捨てない。段のパイプラインと同じ
            D3D11_RASTERIZER_DESC rasterizerDesc{};
            rasterizerDesc.FillMode = D3D11_FILL_SOLID;
            rasterizerDesc.CullMode = D3D11_CULL_NONE;
            rasterizerDesc.DepthClipEnable = TRUE;
            (void)m_device->CreateRasterizerState(&rasterizerDesc, m_rasterizer.GetAddressOf());
        }

        [[nodiscard]] bool HasDevice() const { return m_device != nullptr; }
        [[nodiscard]] bool Valid() const
        {
            return m_vs != nullptr && m_ps != nullptr && m_sampler != nullptr && m_rasterizer != nullptr;
        }
        [[nodiscard]] const std::string& CompileError() const { return m_compileError; }

        // bodyMask は行の順に 1 画素 1 バイト。空なら型を差さない
        std::vector<Rgba> Run(const NS::Gfx::ScreenDistortionCB& constants,
                              const std::vector<std::uint8_t>& bodyMask = {})
        {
            std::vector<Rgba> out;
            std::vector<std::uint8_t> pixels(k_Size * k_Size * 4);
            for (UINT y = 0; y < k_Size; ++y)
            {
                for (UINT x = 0; x < k_Size; ++x)
                {
                    const Rgba color = Pattern(static_cast<int>(x), static_cast<int>(y));
                    std::uint8_t* pixel = &pixels[(y * k_Size + x) * 4];
                    pixel[0] = static_cast<std::uint8_t>(color.r);
                    pixel[1] = static_cast<std::uint8_t>(color.g);
                    pixel[2] = static_cast<std::uint8_t>(color.b);
                    pixel[3] = 255;
                }
            }
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = k_Size;
            desc.Height = k_Size;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            const D3D11_SUBRESOURCE_DATA initial{pixels.data(), k_Size * 4, 0};
            ComPtr<ID3D11Texture2D> source;
            ComPtr<ID3D11ShaderResourceView> sourceView;
            if (FAILED(m_device->CreateTexture2D(&desc, &initial, source.GetAddressOf())) ||
                FAILED(m_device->CreateShaderResourceView(source.Get(), nullptr, sourceView.GetAddressOf())))
            {
                return out;
            }
            desc.BindFlags = D3D11_BIND_RENDER_TARGET;
            ComPtr<ID3D11Texture2D> target;
            ComPtr<ID3D11RenderTargetView> targetView;
            if (FAILED(m_device->CreateTexture2D(&desc, nullptr, target.GetAddressOf())) ||
                FAILED(m_device->CreateRenderTargetView(target.Get(), nullptr, targetView.GetAddressOf())))
            {
                return out;
            }
            D3D11_BUFFER_DESC cbDesc{};
            cbDesc.ByteWidth = sizeof(constants);
            cbDesc.Usage = D3D11_USAGE_DEFAULT;
            cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            const D3D11_SUBRESOURCE_DATA cbData{&constants, 0, 0};
            ComPtr<ID3D11Buffer> cb;
            if (FAILED(m_device->CreateBuffer(&cbDesc, &cbData, cb.GetAddressOf())))
            {
                return out;
            }

            ComPtr<ID3D11ShaderResourceView> maskView;
            if (!bodyMask.empty())
            {
                D3D11_TEXTURE2D_DESC maskDesc = desc;
                maskDesc.Format = DXGI_FORMAT_R8_UNORM;
                maskDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                const D3D11_SUBRESOURCE_DATA maskData{bodyMask.data(), k_Size, 0};
                ComPtr<ID3D11Texture2D> mask;
                if (FAILED(m_device->CreateTexture2D(&maskDesc, &maskData, mask.GetAddressOf())) ||
                    FAILED(m_device->CreateShaderResourceView(mask.Get(), nullptr, maskView.GetAddressOf())))
                {
                    return out;
                }
            }

            ID3D11RenderTargetView* targets[] = {targetView.Get()};
            ID3D11ShaderResourceView* views[] = {sourceView.Get(), maskView.Get()};
            ID3D11SamplerState* samplers[] = {m_sampler.Get()};
            ID3D11Buffer* cbs[] = {cb.Get()};
            const D3D11_VIEWPORT viewport{
                0.0f, 0.0f, static_cast<float>(k_Size), static_cast<float>(k_Size), 0.0f, 1.0f};
            m_context->OMSetRenderTargets(1, targets, nullptr);
            m_context->RSSetViewports(1, &viewport);
            m_context->RSSetState(m_rasterizer.Get());
            m_context->IASetInputLayout(nullptr);
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_context->VSSetShader(m_vs.Get(), nullptr, 0);
            m_context->PSSetShader(m_ps.Get(), nullptr, 0);
            m_context->PSSetShaderResources(0, 2, views);
            m_context->PSSetSamplers(0, 1, samplers);
            m_context->PSSetConstantBuffers(0, 1, cbs);
            m_context->Draw(3, 0);
            m_context->OMSetRenderTargets(0, nullptr, nullptr);

            desc.BindFlags = 0;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;
            if (FAILED(m_device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())))
            {
                return out;
            }
            m_context->CopyResource(staging.Get(), target.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            {
                return out;
            }
            for (UINT y = 0; y < k_Size; ++y)
            {
                const std::uint8_t* row = static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch;
                for (UINT x = 0; x < k_Size; ++x)
                {
                    out.push_back(Rgba{row[x * 4], row[x * 4 + 1], row[x * 4 + 2]});
                }
            }
            m_context->Unmap(staging.Get(), 0);
            return out;
        }

    private:
        ComPtr<ID3DBlob> Compile(const wchar_t* path, const char* entry, const char* target)
        {
            ComPtr<ID3DBlob> blob;
            ComPtr<ID3DBlob> errors;
            const HRESULT hr = ::D3DCompileFromFile(path,
                                                    nullptr,
                                                    D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                                    entry,
                                                    target,
                                                    0,
                                                    0,
                                                    blob.GetAddressOf(),
                                                    errors.GetAddressOf());
            if (FAILED(hr))
            {
                if (errors != nullptr)
                {
                    m_compileError.assign(static_cast<const char*>(errors->GetBufferPointer()),
                                          errors->GetBufferSize());
                }
                return nullptr;
            }
            return blob;
        }

        ComPtr<ID3D11Device> m_device;
        ComPtr<ID3D11DeviceContext> m_context;
        ComPtr<ID3D11VertexShader> m_vs;
        ComPtr<ID3D11PixelShader> m_ps;
        ComPtr<ID3D11SamplerState> m_sampler;
        ComPtr<ID3D11RasterizerState> m_rasterizer;
        std::string m_compileError;
    };

    // 絵の真ん中に置いた輪。半径 8、半分の幅 4。単位は画素
    NS::Gfx::ScreenDistortionCB CenterRing(float push)
    {
        NS::Gfx::ScreenDistortionCB constants{};
        constants.ring[0] = 16.0f;
        constants.ring[1] = 16.0f;
        constants.ring[2] = 8.0f;
        constants.ring[3] = push;
        constants.ringHalfWidth = 4.0f;
        return constants;
    }
} // namespace

// 定数はシェーダーの cbuffer と並びを揃える
TEST(ScreenDistortion, ConstantsMatchTheShaderLayout)
{
    EXPECT_EQ(sizeof(NS::Gfx::ScreenDistortionCB), 32u);
    EXPECT_EQ(offsetof(NS::Gfx::ScreenDistortionCB, ringHalfWidth), 16u);
    EXPECT_EQ(offsetof(NS::Gfx::ScreenDistortionCB, keepBody), 20u);
}

// 型の上は押さないにすると、型が 1 の画素は入れた絵のまま。型が 0 の画素は輪の式どおり押す
TEST(ScreenDistortion, KeepsThePixelsOnTheBodyMask)
{
    DistortionRig rig;
    if (!rig.HasDevice())
    {
        GTEST_SKIP() << "WARP の描画装置を作れなかった";
    }
    ASSERT_TRUE(rig.Valid()) << rig.CompileError();

    std::vector<std::uint8_t> mask(k_Size * k_Size, 0);
    for (UINT y = 0; y < k_Size; ++y)
    {
        for (UINT x = 0; x < k_Size / 2; ++x)
        {
            mask[y * k_Size + x] = 255;
        }
    }
    NS::Gfx::ScreenDistortionCB constants = CenterRing(3.0f);
    constants.keepBody = 1.0f;
    const std::vector<Rgba> written = rig.Run(constants, mask);
    ASSERT_EQ(written.size(), static_cast<std::size_t>(k_Size * k_Size));
    int keptOnRing = 0;
    int pushedOnRing = 0;
    for (int y = 0; y < static_cast<int>(k_Size); ++y)
    {
        for (int x = 0; x < static_cast<int>(k_Size); ++x)
        {
            SCOPED_TRACE(testing::Message() << "x=" << x << " y=" << y);
            bool distorted = false;
            const Rgba pushed = ExpectedPixel(constants, x, y, distorted);
            const Rgba original = Pattern(x, y);
            const Rgba& actual = written[static_cast<std::size_t>(y) * k_Size + static_cast<std::size_t>(x)];
            if (mask[static_cast<std::size_t>(y) * k_Size + static_cast<std::size_t>(x)] != 0)
            {
                EXPECT_EQ(actual.r, original.r);
                EXPECT_EQ(actual.g, original.g);
                EXPECT_EQ(actual.b, original.b);
                if (distorted && (pushed.r != original.r || pushed.g != original.g))
                {
                    ++keptOnRing;
                }
            }
            else if (distorted)
            {
                EXPECT_LE(std::abs(actual.r - pushed.r), 2);
                EXPECT_LE(std::abs(actual.g - pushed.g), 2);
                EXPECT_LE(std::abs(actual.b - pushed.b), 2);
                ++pushedOnRing;
            }
        }
    }
    // 型の上にも、型の外にも、押せば色が動く輪の画素がある
    EXPECT_GT(keptOnRing, 20);
    EXPECT_GT(pushedOnRing, 50);
}

// 輪の上の画素は、輪の外へ押し出した所の色を線形に拾う。輪の外の画素は入れた絵のまま
TEST(ScreenDistortion, PushesThePixelsOnTheRingOutward)
{
    DistortionRig rig;
    if (!rig.HasDevice())
    {
        GTEST_SKIP() << "WARP の描画装置を作れなかった";
    }
    ASSERT_TRUE(rig.Valid()) << rig.CompileError();

    const NS::Gfx::ScreenDistortionCB constants = CenterRing(3.0f);
    const std::vector<Rgba> written = rig.Run(constants);
    ASSERT_EQ(written.size(), static_cast<std::size_t>(k_Size * k_Size));
    int distortedCount = 0;
    int movedCount = 0;
    for (int y = 0; y < static_cast<int>(k_Size); ++y)
    {
        for (int x = 0; x < static_cast<int>(k_Size); ++x)
        {
            SCOPED_TRACE(testing::Message() << "x=" << x << " y=" << y);
            bool distorted = false;
            const Rgba expected = ExpectedPixel(constants, x, y, distorted);
            const Rgba& actual = written[static_cast<std::size_t>(y) * k_Size + static_cast<std::size_t>(x)];
            if (distorted)
            {
                // 線形に拾う所は描画装置の丸めの誤差を許す
                EXPECT_LE(std::abs(actual.r - expected.r), 2);
                EXPECT_LE(std::abs(actual.g - expected.g), 2);
                EXPECT_LE(std::abs(actual.b - expected.b), 2);
                ++distortedCount;
                const Rgba original = Pattern(x, y);
                if (actual.r != original.r || actual.g != original.g)
                {
                    ++movedCount;
                }
            }
            else
            {
                EXPECT_EQ(actual.r, expected.r);
                EXPECT_EQ(actual.g, expected.g);
                EXPECT_EQ(actual.b, expected.b);
            }
        }
    }
    // 輪が絵の上にあり、色が実際に動いた画素がある
    EXPECT_GT(distortedCount, 100);
    EXPECT_GT(movedCount, 50);
}

TEST(ScreenDistortion, ZeroPushLeavesThePictureUnchanged)
{
    DistortionRig rig;
    if (!rig.HasDevice())
    {
        GTEST_SKIP() << "WARP の描画装置を作れなかった";
    }
    ASSERT_TRUE(rig.Valid()) << rig.CompileError();

    const std::vector<Rgba> written = rig.Run(CenterRing(0.0f));
    ASSERT_EQ(written.size(), static_cast<std::size_t>(k_Size * k_Size));
    for (int y = 0; y < static_cast<int>(k_Size); ++y)
    {
        for (int x = 0; x < static_cast<int>(k_Size); ++x)
        {
            SCOPED_TRACE(testing::Message() << "x=" << x << " y=" << y);
            const Rgba original = Pattern(x, y);
            const Rgba& actual = written[static_cast<std::size_t>(y) * k_Size + static_cast<std::size_t>(x)];
            EXPECT_EQ(actual.r, original.r);
            EXPECT_EQ(actual.g, original.g);
            EXPECT_EQ(actual.b, original.b);
        }
    }
}
