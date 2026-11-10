#include "NSlib/Core/Windows/Window.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "TestLogCapture.h"

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
