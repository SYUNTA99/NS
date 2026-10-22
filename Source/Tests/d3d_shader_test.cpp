#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Windows/Window.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <memory>

// 読み直しの道のファイル名からステージを決められない時は、作る時と同じく ERROR を出して断る
TEST(D3DShader, ReloadWithoutStageInFileNameLogsAnError)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "シェーダーのステージの試し", .size = {16, 16}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    // 無いファイルなので代わりのピンクになる。ステージはファイル名から決まる
    const std::unique_ptr<NS::Gfx::D3DShader> shader = NS::Gfx::D3DShader::Create("ns_missing_stage_test.ps.hlsl");
    ASSERT_TRUE(shader->IsUsingFallback());

    const TestLogCapture log;
    EXPECT_FALSE(shader->Reload("ns_missing_stage_test.hlsl"));
    EXPECT_EQ(log.ErrorCount(), 1);
}
