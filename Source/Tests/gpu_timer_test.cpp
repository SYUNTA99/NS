#include <Runtime/Core/Logger.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Graphics/GpuTimer.h>
#include <Runtime/Graphics/RenderTarget.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Platform/Window.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>

namespace
{
    using NS::Gfx::GpuTimer;
    using NS::Gfx::Renderer;
    using NS::Gfx::RendererDesc;
    using NS::Gfx::RenderTarget;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;

    // 重い描画の描画先の 1 辺。全画面の矩形を重ねた時に描画装置が 1 ミリ秒前後掛かる大きさ
    constexpr int k_HeavyTargetSize = 2048;
    constexpr int k_HeavyRectCount = 200;

    WindowDesc MakeWindowDesc()
    {
        WindowDesc d{};
        d.title = "ns_gpu_timer";
        d.size = NS::Core::Size2D{64, 64};
        d.visible = false;
        return d;
    }

    RendererDesc MakeRendererDesc()
    {
        RendererDesc d{};
#ifdef NS_BUILD_DEBUG
        d.enableDebugLayer = true;
#else
        d.enableDebugLayer = false;
#endif
        d.vsync = false;
        return d;
    }
} // namespace

class GpuTimerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(GpuTimerTest, IsInvalidAndReadsNothingWithoutRenderer)
{
    GpuTimer timer;

    EXPECT_FALSE(timer.IsValid());
    timer.Begin();
    timer.End();
    EXPECT_FALSE(timer.ReadMilliseconds().has_value());
}

class GpuTimerWithRendererTest : public GpuTimerTest
{
protected:
    void SetUp() override
    {
        GpuTimerTest::SetUp();
        m_window = std::make_unique<Window>(MakeWindowDesc());
        ASSERT_TRUE(m_window->IsValid());
        m_renderer = std::make_unique<Renderer>(MakeRendererDesc(), *m_window);
        ASSERT_TRUE(m_renderer->IsValid());
        m_target = RenderTarget::Create(NS::Core::Size2D{k_HeavyTargetSize, k_HeavyTargetSize});
        ASSERT_TRUE(m_target && m_target->IsValid());
    }

    void TearDown() override
    {
        if (m_renderer)
        {
            m_renderer->SetSceneTarget(nullptr);
        }
        m_target.reset();
        m_renderer.reset();
        m_window.reset();
        GpuTimerTest::TearDown();
    }

    void DrawHeavy()
    {
        m_renderer->BeginSceneView(m_target.get());
        const float size = static_cast<float>(k_HeavyTargetSize);
        for (int i = 0; i < k_HeavyRectCount; ++i)
        {
            m_renderer->DrawScreenRect(0.0f, 0.0f, size, size, NS::Core::Color{1.0f, 1.0f, 1.0f, 0.01f});
        }
    }

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<RenderTarget> m_target;
};

// 決まった値を返す実装だと、重い描画と空の区間が同じ値になって落ちる
TEST_F(GpuTimerWithRendererTest, MeasuresLongerForHeavierDrawsBetweenBeginAndEnd)
{
    GpuTimer timer;
    ASSERT_TRUE(timer.IsValid());
    // 初回の描画は資源を作るので、測る前に 1 度描いておく
    DrawHeavy();

    timer.Begin();
    timer.End();
    const std::optional<float> empty = timer.ReadMilliseconds();

    timer.Begin();
    DrawHeavy();
    timer.End();
    const std::optional<float> heavy = timer.ReadMilliseconds();

    ASSERT_TRUE(empty.has_value());
    ASSERT_TRUE(heavy.has_value());
    EXPECT_GE(empty.value(), 0.0f);
    EXPECT_GT(heavy.value(), empty.value());
}

// 測りの途中と、読み終えた後は値が無い。前の測りの値を 2 度返さない
TEST_F(GpuTimerWithRendererTest, ReadsNothingBeforeEndAndNothingTwice)
{
    GpuTimer timer;
    ASSERT_TRUE(timer.IsValid());

    EXPECT_FALSE(timer.ReadMilliseconds().has_value());
    timer.Begin();
    EXPECT_FALSE(timer.ReadMilliseconds().has_value());
    timer.End();
    EXPECT_TRUE(timer.ReadMilliseconds().has_value());
    EXPECT_FALSE(timer.ReadMilliseconds().has_value());
}
