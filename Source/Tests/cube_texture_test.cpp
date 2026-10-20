#include "NSlib/Core/Types.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/Windows/Filesystem.h"

#include <gtest/gtest.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <string>

// 空の立方体のテクスチャも、ほかの画像と同じ D3DTexture の道で作る。同じ画像なら 2D と同じ段数の
// mip を持ち、縮めた段の色も 2D と同じになる事を確かめる。窓の要らない WARP の装置で回す

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

    // git が追う空の面の画像
    std::string FacePath()
    {
        return NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets/Skybox/kurt/space_bk.png");
    }

    D3D11_TEXTURE2D_DESC DescOf(const NS::Gfx::D3DTexture& texture)
    {
        D3D11_TEXTURE2D_DESC desc{};
        texture.Native()->GetDesc(&desc);
        return desc;
    }

    D3D11_SRV_DIMENSION ViewDimensionOf(const NS::Gfx::D3DTexture& texture)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
        texture.Srv()->GetDesc(&desc);
        return desc.ViewDimension;
    }

    // 指した部分資源 (1×1 の段) の 1 ピクセルを読む
    std::array<NS::uint8, 4> ReadTexel(ID3D11Texture2D* texture, UINT subresource)
    {
        std::array<NS::uint8, 4> texel{};
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = 1;
        desc.Height = 1;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(NS::Gfx::Gpu().device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())))
        {
            return texel;
        }
        NS::Gfx::Gpu().context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, texture, subresource, nullptr);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(NS::Gfx::Gpu().context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            return texel;
        }
        const NS::uint8* bytes = static_cast<const NS::uint8*>(mapped.pData);
        for (std::size_t i = 0; i < texel.size(); ++i)
        {
            texel[i] = bytes[i];
        }
        NS::Gfx::Gpu().context->Unmap(staging.Get(), 0);
        return texel;
    }
} // namespace

TEST(CubeTexture, FacesGetTheSameMipsAsTheFlatImage)
{
    ScopedWarpGpu gpu;
    if (!gpu.Valid())
    {
        GTEST_SKIP() << "WARP の描画装置を作れない";
    }

    const std::string face = FacePath();
    NS::Gfx::D3DTextureDesc flatDesc{};
    flatDesc.path = face;
    const std::unique_ptr<NS::Gfx::D3DTexture> flat = NS::Gfx::D3DTexture::Create(flatDesc);
    const std::unique_ptr<NS::Gfx::D3DTexture> cube =
        NS::Gfx::D3DTexture::CreateCubeFromFaces({face, face, face, face, face, face});
    ASSERT_FALSE(flat->IsUsingFallback());
    ASSERT_FALSE(cube->IsUsingFallback());
    ASSERT_NE(cube->Srv(), nullptr);

    EXPECT_EQ(ViewDimensionOf(*cube), D3D11_SRV_DIMENSION_TEXTURECUBE);
    const D3D11_TEXTURE2D_DESC flatTex = DescOf(*flat);
    const D3D11_TEXTURE2D_DESC cubeTex = DescOf(*cube);
    ASSERT_GT(flatTex.MipLevels, 1u);
    ASSERT_EQ(cubeTex.MipLevels, flatTex.MipLevels);
    EXPECT_EQ(cubeTex.ArraySize, 6u);

    // 一番小さい段は 1×1。縮めた色が 2D と同じなら、立方体の段も同じ画像から作られている
    const UINT last = flatTex.MipLevels - 1;
    const std::array<NS::uint8, 4> flatTexel = ReadTexel(flat->Native(), last);
    for (UINT faceIndex = 0; faceIndex < 6; ++faceIndex)
    {
        const std::array<NS::uint8, 4> cubeTexel =
            ReadTexel(cube->Native(), D3D11CalcSubresource(last, faceIndex, cubeTex.MipLevels));
        for (std::size_t c = 0; c < 4; ++c)
        {
            EXPECT_LE(std::abs(static_cast<int>(cubeTexel[c]) - static_cast<int>(flatTexel[c])), 2)
                << "面 " << faceIndex << " の成分 " << c;
        }
    }
}

TEST(CubeTexture, MissingFaceFallsBackToPinkCube)
{
    ScopedWarpGpu gpu;
    if (!gpu.Valid())
    {
        GTEST_SKIP() << "WARP の描画装置を作れない";
    }

    const std::string face = FacePath();
    const std::string missing =
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets/Skybox/kurt/__ns_missing__.png");
    const std::unique_ptr<NS::Gfx::D3DTexture> cube =
        NS::Gfx::D3DTexture::CreateCubeFromFaces({face, face, missing, face, face, face});

    EXPECT_TRUE(cube->IsUsingFallback());
    ASSERT_NE(cube->Srv(), nullptr);
    EXPECT_EQ(ViewDimensionOf(*cube), D3D11_SRV_DIMENSION_TEXTURECUBE);
}
