#include "pixel_readback.h"

#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/Bloom.h>
#include <Runtime/Graphics/GraphicObject.h>
#include <Runtime/Graphics/RenderTarget.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Graphics/Texture.h>
#include <Runtime/Platform/Window.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

namespace
{
    using NS::Gfx::Bloom;
    using NS::Gfx::BloomDesc;
    using NS::Gfx::Renderer;
    using NS::Gfx::RendererDesc;
    using NS::Gfx::RenderTarget;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;
    using NS::Tests::LargestChannelDifference;
    using NS::Tests::PixelPosition;
    using NS::Tests::ReadPixel;

    constexpr int k_TargetSize = 64;

    // 1 を超える画素 1 つ。当たりの閃光に置く明るさ (2.0〜4.0) の上の端
    constexpr float k_BrightValue = 4.0f;
    constexpr PixelPosition k_BrightPixel{32, 32};
    // 明るい画素の隣。既定の設定で背景より 6 明るくなる (2026-09-27 に測った値)
    constexpr PixelPosition k_NearBrightPixel{33, 32};
    // にじみで明るくなったと言える差。8 ビットの丸めの揺れ (1) の 3 倍
    constexpr int k_VisibleBrightening = 3;

    WindowDesc MakeWindowDesc()
    {
        WindowDesc d{};
        d.title = "ns_bloom";
        d.size = NS::Core::Size2D{k_TargetSize, k_TargetSize};
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

    // 描画先の全画素を RGBA で読む。左上から行ごとに並ぶ
    std::vector<std::uint8_t> ReadAllPixels(const RenderTarget& target)
    {
        ID3D11Texture2D* source = target.Color()->Native();
        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;

        std::vector<std::uint8_t> pixels;
        NS::Gfx::ComPtr<ID3D11Texture2D> staging;
        if (FAILED(NS::Gfx::Gpu().device->CreateTexture2D(&desc, nullptr, &staging)))
        {
            ADD_FAILURE() << "読み戻し用のテクスチャを作れなかった";
            return pixels;
        }
        ID3D11DeviceContext* context = NS::Gfx::Gpu().context;
        context->CopyResource(staging.Get(), source);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            ADD_FAILURE() << "読み戻し用のテクスチャを開けなかった";
            return pixels;
        }
        const std::uint8_t* bytes = static_cast<const std::uint8_t*>(mapped.pData);
        pixels.reserve(static_cast<std::size_t>(desc.Width) * desc.Height * 4);
        for (UINT row = 0; row < desc.Height; ++row)
        {
            const std::uint8_t* line = bytes + static_cast<std::size_t>(row) * mapped.RowPitch;
            pixels.insert(pixels.end(), line, line + static_cast<std::size_t>(desc.Width) * 4);
        }
        context->Unmap(staging.Get(), 0);
        return pixels;
    }

    // 2 枚の絵の色の差の最大。アルファは見ない
    int LargestDifference(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b)
    {
        if (a.size() != b.size())
        {
            ADD_FAILURE() << "絵の大きさが違う (" << a.size() << " と " << b.size() << ")";
            return 255;
        }
        int largest = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (i % 4 == 3)
            {
                continue;
            }
            const int diff = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
            if (diff > largest)
            {
                largest = diff;
            }
        }
        return largest;
    }
} // namespace

class BloomTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

// Bloom は構築時に Gpu() の device を使うので、Renderer より後に作る
class BloomWithRendererTest : public BloomTest
{
protected:
    void SetUp() override
    {
        BloomTest::SetUp();
        m_window = std::make_unique<Window>(MakeWindowDesc());
        ASSERT_TRUE(m_window->IsValid());
        m_renderer = std::make_unique<Renderer>(MakeRendererDesc(), *m_window);
        ASSERT_TRUE(m_renderer->IsValid());
        m_target = RenderTarget::Create(NS::Core::Size2D{k_TargetSize, k_TargetSize});
        ASSERT_TRUE(m_target && m_target->IsValid());
    }

    void TearDown() override
    {
        if (m_renderer)
        {
            // target は Renderer より先に消えるので、指したままにせず外す
            m_renderer->SetSceneTarget(nullptr);
        }
        m_target.reset();
        m_renderer.reset();
        m_window.reset();
        BloomTest::TearDown();
    }

    // 1 を超える画素 1 つを、にじみを通して target へ描く
    void DrawBrightPixelThrough(Bloom& bloom, RenderTarget& target)
    {
        m_renderer->BeginSceneView(&target);
        bloom.BeginWorld(m_renderer->Settings().clearColor);
        m_renderer->DrawScreenRect(static_cast<float>(k_BrightPixel.column),
                                   static_cast<float>(k_BrightPixel.row),
                                   1.0f,
                                   1.0f,
                                   NS::Core::Color{k_BrightValue, k_BrightValue, k_BrightValue, 1.0f});
        bloom.EndWorld();
    }

    // 1 以下の色だけで塗った絵。白 1.0 ちょうどと半透明の重なりを含む
    void DrawPictureAtOrBelowOne()
    {
        m_renderer->DrawScreenRect(4.0f, 4.0f, 20.0f, 12.0f, NS::Core::Color{1.0f, 1.0f, 1.0f, 1.0f});
        m_renderer->DrawScreenRect(30.0f, 4.0f, 20.0f, 20.0f, NS::Core::Color{1.0f, 0.55f, 0.1f, 1.0f});
        m_renderer->DrawScreenRect(8.0f, 30.0f, 40.0f, 10.0f, NS::Core::Color{0.35f, 0.62f, 0.9f, 1.0f});
        m_renderer->DrawScreenRect(20.0f, 20.0f, 30.0f, 30.0f, NS::Core::Color{0.95f, 0.95f, 0.2f, 0.5f});
        m_renderer->DrawScreenRect(40.0f, 44.0f, 16.0f, 16.0f, NS::Core::Color{0.02f, 0.03f, 0.04f, 1.0f});
    }

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<RenderTarget> m_target;
};

TEST_F(BloomTest, IsInvalidAndHarmlessWithoutRenderer)
{
    Bloom bloom{BloomDesc{}};

    EXPECT_FALSE(bloom.IsValid());
    bloom.BeginWorld(NS::Core::Color{0.1f, 0.1f, 0.15f, 1.0f});
    bloom.EndWorld();
}

TEST_F(BloomWithRendererTest, IsValidWithRenderer)
{
    Bloom bloom{BloomDesc{}};

    EXPECT_TRUE(bloom.IsValid());
}

TEST_F(BloomWithRendererTest, PixelBrighterThanOneBrightensItsSurroundings)
{
    Bloom bloom{BloomDesc{}};
    ASSERT_TRUE(bloom.IsValid());

    m_renderer->BeginSceneView(m_target.get());
    const std::array<std::uint8_t, 4> background = ReadPixel(*m_target, k_NearBrightPixel);

    DrawBrightPixelThrough(bloom, *m_target);
    const std::array<std::uint8_t, 4> beside = ReadPixel(*m_target, k_NearBrightPixel);

    EXPECT_GT(LargestChannelDifference(background, beside), k_VisibleBrightening);
    for (std::size_t i = 0; i < 3; ++i)
    {
        EXPECT_GE(beside[i], background[i]) << i;
    }
}

// 物の絵は S 字で 1 以下に書く。閾値 1 なら、浮動小数の描画先を通しても 8 ビットの丸めの揺れしか変わらない
TEST_F(BloomWithRendererTest, PictureAtOrBelowOneStaysWithinOneStep)
{
    m_renderer->BeginSceneView(m_target.get());
    DrawPictureAtOrBelowOne();
    const std::vector<std::uint8_t> direct = ReadAllPixels(*m_target);

    Bloom bloom{BloomDesc{}};
    ASSERT_TRUE(bloom.IsValid());
    m_renderer->BeginSceneView(m_target.get());
    bloom.BeginWorld(m_renderer->Settings().clearColor);
    DrawPictureAtOrBelowOne();
    bloom.EndWorld();
    const std::vector<std::uint8_t> through = ReadAllPixels(*m_target);

    EXPECT_LE(LargestDifference(direct, through), 1);
}

// EndWorld の後は元の描画先へ描ける。重ね描きとデバッグの線はここへ描く
TEST_F(BloomWithRendererTest, EndWorldRestoresTheTarget)
{
    Bloom bloom{BloomDesc{}};
    ASSERT_TRUE(bloom.IsValid());

    m_renderer->BeginSceneView(m_target.get());
    bloom.BeginWorld(m_renderer->Settings().clearColor);
    bloom.EndWorld();
    m_renderer->DrawScreenRect(0.0f, 0.0f, 2.0f, 2.0f, NS::Core::Color{1.0f, 0.0f, 0.0f, 1.0f});

    const std::array<std::uint8_t, 4> drawn = ReadPixel(*m_target, PixelPosition{0, 0});
    EXPECT_EQ(drawn[0], 255);
    EXPECT_EQ(drawn[1], 0);
    EXPECT_EQ(drawn[2], 0);
}

TEST_F(BloomWithRendererTest, KeepsWorkingWhenTheTargetSizeChanges)
{
    Bloom bloom{BloomDesc{}};
    ASSERT_TRUE(bloom.IsValid());
    DrawBrightPixelThrough(bloom, *m_target);

    std::unique_ptr<RenderTarget> wide = RenderTarget::Create(NS::Core::Size2D{48, 40});
    ASSERT_TRUE(wide && wide->IsValid());
    m_renderer->BeginSceneView(wide.get());
    const std::array<std::uint8_t, 4> background = ReadPixel(*wide, k_NearBrightPixel);

    DrawBrightPixelThrough(bloom, *wide);
    const std::array<std::uint8_t, 4> beside = ReadPixel(*wide, k_NearBrightPixel);
    const std::array<std::uint8_t, 4> corner = ReadPixel(*wide, PixelPosition{47, 0});

    EXPECT_GT(LargestChannelDifference(background, beside), k_VisibleBrightening);
    EXPECT_LE(LargestChannelDifference(background, corner), 1);
    m_renderer->SetSceneTarget(nullptr);
}

// 足す強さ 0 は、1 を超える画素があっても周りを変えない
TEST_F(BloomWithRendererTest, ZeroIntensityOnlyPassesThrough)
{
    BloomDesc desc{};
    desc.intensity = 0.0f;
    Bloom bloom{desc};
    ASSERT_TRUE(bloom.IsValid());

    m_renderer->BeginSceneView(m_target.get());
    const std::array<std::uint8_t, 4> background = ReadPixel(*m_target, k_NearBrightPixel);

    DrawBrightPixelThrough(bloom, *m_target);
    const std::array<std::uint8_t, 4> beside = ReadPixel(*m_target, k_NearBrightPixel);
    const std::array<std::uint8_t, 4> bright = ReadPixel(*m_target, k_BrightPixel);

    EXPECT_LE(LargestChannelDifference(background, beside), 1);
    EXPECT_EQ(bright[0], 255);
}
