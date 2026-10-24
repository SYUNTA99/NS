#include "NSlib/Gfx/D3DCommandList.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/Core/Windows/Window.h"

#include <gtest/gtest.h>

#include <memory>

namespace
{
    std::unique_ptr<NS::Gfx::D3DTexture> MakeColorTarget(UINT width, UINT height)
    {
        NS::Gfx::D3DTextureDesc desc{};
        desc.width = width;
        desc.height = height;
        desc.bindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        return NS::Gfx::D3DTexture::Create(desc);
    }
} // namespace

// 控えた後に別の描き先とビューポートを差しても、戻せば控えた描き先・深度・ビューポートになる
TEST(CommandListTargets, RestoreReturnsToTheSavedTargetsAndViewport)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "描き先の控えの試し", .size = {16, 16}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    const std::unique_ptr<NS::Gfx::D3DTexture> other = MakeColorTarget(8, 4);
    ASSERT_TRUE(other->IsValid());

    device.BeginFrame();
    NS::Gfx::D3DCommandList& cmd = device.Commands();
    cmd.SetViewport(12.0f, 10.0f);
    NS::Gfx::ComPtr<ID3D11RenderTargetView> firstColor;
    NS::Gfx::ComPtr<ID3D11DepthStencilView> firstDepth;
    cmd->OMGetRenderTargets(1, firstColor.GetAddressOf(), firstDepth.GetAddressOf());
    ASSERT_NE(firstColor, nullptr);
    ASSERT_NE(firstDepth, nullptr);

    const NS::Gfx::SavedTargets saved = cmd.SaveTargets();
    cmd.SetRenderTarget(other->Rtv(), nullptr);
    cmd.SetViewport(8.0f, 4.0f);
    cmd.RestoreTargets(saved);

    NS::Gfx::ComPtr<ID3D11RenderTargetView> color;
    NS::Gfx::ComPtr<ID3D11DepthStencilView> depth;
    cmd->OMGetRenderTargets(1, color.GetAddressOf(), depth.GetAddressOf());
    EXPECT_EQ(color, firstColor);
    EXPECT_EQ(depth, firstDepth);
    D3D11_VIEWPORT viewport{};
    UINT viewportCount = 1;
    cmd->RSGetViewports(&viewportCount, &viewport);
    ASSERT_EQ(viewportCount, 1u);
    EXPECT_FLOAT_EQ(viewport.Width, 12.0f);
    EXPECT_FLOAT_EQ(viewport.Height, 10.0f);
}

// 外した場所は空になり、外さなかった場所はそのまま残る
TEST(CommandListTargets, UnbindClearsOnlyTheGivenPixelShaderResources)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "読む絵を外す試し", .size = {16, 16}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    const std::unique_ptr<NS::Gfx::D3DTexture> texture = MakeColorTarget(4, 4);
    ASSERT_TRUE(texture->IsValid());

    NS::Gfx::D3DCommandList& cmd = device.Commands();
    cmd.PSSetShaderResource(*texture, 0);
    cmd.PSSetShaderResource(*texture, 1);
    cmd.PSSetShaderResource(*texture, 2);
    cmd.UnbindPixelShaderResources(0, 2);

    NS::Gfx::ComPtr<ID3D11ShaderResourceView> bound[3];
    for (UINT slot = 0; slot < 3; ++slot)
    {
        cmd->PSGetShaderResources(slot, 1, bound[slot].GetAddressOf());
    }
    EXPECT_EQ(bound[0], nullptr);
    EXPECT_EQ(bound[1], nullptr);
    EXPECT_EQ(bound[2].Get(), texture->Srv());
}
