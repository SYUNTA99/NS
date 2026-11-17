#include "NSlib/Core/Windows/Window.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "TestLogCapture.h"

#include <dxgi1_6.h>
#include <gtest/gtest.h>

TEST(D3DDeviceLifetime, ASecondDeviceDoesNotReplaceTheActiveDevice)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "描画装置の寿命", .size = {16, 16}, .visible = false});
    NS::Gfx::D3DDevice first(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(first.IsValid());

    const TestLogCapture log;
    {
        NS::Gfx::D3DDevice second(NS::Gfx::D3DDeviceDesc{}, window);
        EXPECT_FALSE(second.IsValid());
        EXPECT_EQ(NS::Gfx::D3DDevice::Current(), &first);
    }
    EXPECT_EQ(NS::Gfx::D3DDevice::Current(), &first);
    EXPECT_TRUE(first.IsValid());
    EXPECT_GT(log.ErrorCount(), 0);
}

TEST(D3DDeviceLifetime, AHeadlessDeviceCreatesResourcesAndReleasesTheCurrentReference)
{
    ASSERT_EQ(NS::Gfx::D3DDevice::Current(), nullptr);
    {
        NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, D3D_DRIVER_TYPE_WARP);
        ASSERT_TRUE(device.IsValid());
        EXPECT_EQ(NS::Gfx::D3DDevice::Current(), &device);
        device.Resize({16, 16});
        EXPECT_EQ(device.Size(), NS::Size2D{});

        const std::unique_ptr<NS::Gfx::D3DTexture> texture = NS::Gfx::D3DTexture::Create({.width = 8, .height = 8});
        ASSERT_TRUE(texture->IsValid());
        NS::Gfx::ComPtr<ID3D11Device> owner;
        texture->Native()->GetDevice(owner.GetAddressOf());
        EXPECT_EQ(owner.Get(), device.Native());
    }
    EXPECT_EQ(NS::Gfx::D3DDevice::Current(), nullptr);

    NS::Gfx::D3DDevice next(NS::Gfx::D3DDeviceDesc{}, D3D_DRIVER_TYPE_WARP);
    EXPECT_TRUE(next.IsValid());
    EXPECT_EQ(NS::Gfx::D3DDevice::Current(), &next);
}

// 窓のある装置は、DXGI が性能の高い順に並べた 1 番目のアダプタで作る。2 枚持ちのノート PC で内蔵 GPU で動かさない
TEST(D3DDeviceLifetime, AWindowDeviceUsesTheHighPerformanceAdapter)
{
    NS::Gfx::ComPtr<IDXGIFactory6> factory;
    if (FAILED(::CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()))))
    {
        GTEST_SKIP() << "IDXGIFactory6 の無い Windows では性能の順に並べられない";
    }
    NS::Gfx::ComPtr<IDXGIAdapter1> expected;
    ASSERT_TRUE(SUCCEEDED(factory->EnumAdapterByGpuPreference(
        0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(expected.GetAddressOf()))));
    DXGI_ADAPTER_DESC1 expectedDesc{};
    ASSERT_TRUE(SUCCEEDED(expected->GetDesc1(&expectedDesc)));

    NS::OS::Window window(NS::OS::WindowDesc{.title = "性能の高い GPU", .size = {16, 16}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    NS::Gfx::ComPtr<IDXGIDevice> dxgiDevice;
    ASSERT_TRUE(SUCCEEDED(device.Native()->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf()))));
    NS::Gfx::ComPtr<IDXGIAdapter> used;
    ASSERT_TRUE(SUCCEEDED(dxgiDevice->GetAdapter(used.GetAddressOf())));
    DXGI_ADAPTER_DESC usedDesc{};
    ASSERT_TRUE(SUCCEEDED(used->GetDesc(&usedDesc)));

    // アダプタは LUID で見分ける。名前は同じ型の GPU を 2 枚挿すと重なる
    EXPECT_EQ(usedDesc.AdapterLuid.LowPart, expectedDesc.AdapterLuid.LowPart);
    EXPECT_EQ(usedDesc.AdapterLuid.HighPart, expectedDesc.AdapterLuid.HighPart);
}
