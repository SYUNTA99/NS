#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DRenderTarget.h"
#include "NSlib/Core/Windows/Window.h"

#include <gtest/gtest.h>

#include <memory>

// Size() は最後に差したビューの大きさを返す。シーンの描き先が付いたままでも、バックバッファへ戻せばその大きさになる
TEST(DeviceTarget, SizeFollowsTheLastBoundView)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "描き先の大きさの試し", .size = {16, 16}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    const std::unique_ptr<NS::Gfx::D3DRenderTarget> target = NS::Gfx::D3DRenderTarget::Create({8, 4});
    ASSERT_TRUE(target->IsValid());

    device.SetSceneTarget(target.get());
    device.BeginFrame();
    EXPECT_EQ(device.Size().width, 8);
    EXPECT_EQ(device.Size().height, 4);

    device.BindBackbuffer();
    EXPECT_EQ(device.Size().width, 16);
    EXPECT_EQ(device.Size().height, 16);

    device.BeginSceneView(target.get());
    EXPECT_EQ(device.Size().width, 8);
    EXPECT_EQ(device.Size().height, 4);

    device.BindBackbuffer();
    device.SetSceneTarget(nullptr);
}
