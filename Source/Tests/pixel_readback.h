#pragma once

#include <Runtime/Graphics/GraphicObject.h>
#include <Runtime/Graphics/RenderTarget.h>
#include <Runtime/Graphics/Texture.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <gtest/gtest.h>

namespace NS::Tests
{
    //! @brief ReadPixel が読む画素の位置。原点は左上
    struct PixelPosition
    {
        int column = 0; //!< 左からの画素数
        int row = 0;    //!< 上からの画素数
    };

    // 行列の計算だけを見る試しは描画を消しても通る。描いた結果は画素で確かめる
    //! @brief 描画先の色テクスチャから 1 画素を読み戻す
    //! @param[in] target 読み戻す元の描画先。色は R8G8B8A8
    //! @param[in] at 読む画素の位置
    //! @return 画素の RGBA。読み戻せなかった場合は ADD_FAILURE を立てて 0 で埋めた 4 成分を返す
    inline std::array<std::uint8_t, 4> ReadPixel(const NS::Gfx::RenderTarget& target, PixelPosition at)
    {
        ID3D11Texture2D* source = target.Color()->Native();
        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;

        NS::Gfx::ComPtr<ID3D11Texture2D> staging;
        std::array<std::uint8_t, 4> pixel{};
        if (FAILED(NS::Gfx::Gpu().device->CreateTexture2D(&desc, nullptr, &staging)))
        {
            ADD_FAILURE() << "読み戻し用のテクスチャを作れなかった";
            return pixel;
        }
        ID3D11DeviceContext* context = NS::Gfx::Gpu().context;
        context->CopyResource(staging.Get(), source);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            ADD_FAILURE() << "読み戻し用のテクスチャを開けなかった";
            return pixel;
        }
        const auto* bytes = static_cast<const std::uint8_t*>(mapped.pData);
        const std::uint8_t* texel =
            bytes + static_cast<std::size_t>(at.row) * mapped.RowPitch + static_cast<std::size_t>(at.column) * 4;
        for (std::size_t i = 0; i < pixel.size(); ++i)
        {
            pixel[i] = texel[i];
        }
        context->Unmap(staging.Get(), 0);
        return pixel;
    }

    //! @brief 2 つの画素の色の違いを返す
    //! @param[in] a 比べる画素
    //! @param[in] b 比べる画素
    //! @return 先頭 3 成分の差の絶対値のうち最大の値。アルファは見ない
    inline int LargestChannelDifference(const std::array<std::uint8_t, 4>& a, const std::array<std::uint8_t, 4>& b)
    {
        int largest = 0;
        for (std::size_t i = 0; i < 3; ++i)
        {
            const int diff = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
            if (diff > largest)
            {
                largest = diff;
            }
        }
        return largest;
    }
} // namespace NS::Tests
