#include <Runtime/Core/Filesystem.h>
#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/Camera.h>
#include <Runtime/Graphics/EffectScene.h>
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

namespace
{
    using NS::Graphics::Camera;
    using NS::Graphics::CameraDesc;
    using NS::Graphics::EffectHandle;
    using NS::Graphics::EffectPlayDesc;
    using NS::Graphics::EffectScene;
    using NS::Graphics::EffectSceneDesc;
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Graphics::RenderTarget;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;

    constexpr float k_Frame = 1.0f / 60.0f;
    constexpr int k_TargetSize = 64;

    // square_r.efkefc は寿命 100 フレームの板 1 枚。寿命の前後 20 フレームで在るか消えたかを分ける
    // 秒からフレームへの換算が 0.83 倍から 1.25 倍を外れると落ちる
    constexpr int k_FramesBeforeLifeEnds = 80;
    constexpr int k_FramesAfterLifeEnds = 120;

    WindowDesc MakeWindowDesc(const char* title)
    {
        WindowDesc d{};
        d.title = title;
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

    EffectSceneDesc MakeEffectSceneDesc()
    {
        EffectSceneDesc d{};
        d.effectRoot = NS::Core::FileSystem::Combine(NS::Core::FileSystem::Combine(NS::Core::FileSystem::Combine(NS::Core::FileSystem::Combine(NS::Core::FileSystem::ContentRoot(), "Source"), "Tests"), "data"), "effects");
        return d;
    }

    EffectPlayDesc MakePlayDesc(std::string_view name)
    {
        EffectPlayDesc d{};
        d.name = name;
        return d;
    }

    Camera MakeCamera(const NS::Core::Vector3& position)
    {
        CameraDesc d{};
        d.position = position;
        d.aspectRatio = 1.0f;
        return Camera(d);
    }

    void Advance(EffectScene& world, int frames)
    {
        for (int i = 0; i < frames; ++i)
        {
            world.Update(k_Frame);
        }
    }

    struct PixelPosition
    {
        int column = 0;
        int row = 0;
    };

    constexpr PixelPosition k_Center{k_TargetSize / 2, k_TargetSize / 2};

    PixelPosition PixelOf(const Camera& camera, const NS::Core::Vector3& position)
    {
        const NS::Core::Vector3 ndc = NS::Core::Vector3::Transform(position, camera.ViewProjection());
        PixelPosition pixel{};
        pixel.column = static_cast<int>((ndc.x * 0.5f + 0.5f) * k_TargetSize);
        pixel.row = static_cast<int>((0.5f - ndc.y * 0.5f) * k_TargetSize);
        return pixel;
    }

    // 描いた結果は画素を読み戻して確かめる。行列の計算だけを見る試験は、描画を消しても通る
    std::array<std::uint8_t, 4> ReadPixel(const RenderTarget& target, PixelPosition at)
    {
        ID3D11Texture2D* source = target.Color()->Native();
        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;

        NS::Graphics::ComPtr<ID3D11Texture2D> staging;
        std::array<std::uint8_t, 4> pixel{};
        if (FAILED(NS::Graphics::Gpu().device->CreateTexture2D(&desc, nullptr, &staging)))
        {
            ADD_FAILURE() << "読み戻し用のテクスチャを作れなかった";
            return pixel;
        }
        ID3D11DeviceContext* context = NS::Graphics::Gpu().context;
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

    int LargestChannelDifference(const std::array<std::uint8_t, 4>& a, const std::array<std::uint8_t, 4>& b)
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
} // namespace

class EffectSceneTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

// EffectScene は構築時に Gpu() の device と context を使うので、Renderer より後に作る
class EffectSceneWithRendererTest : public EffectSceneTest
{
protected:
    void SetUp() override
    {
        EffectSceneTest::SetUp();
        m_window = std::make_unique<Window>(MakeWindowDesc("ns_effect_scene"));
        ASSERT_TRUE(m_window->IsValid());
        m_renderer = std::make_unique<Renderer>(MakeRendererDesc(), *m_window);
        ASSERT_TRUE(m_renderer->IsValid());
        m_target = RenderTarget::Create(NS::Core::Size2D{k_TargetSize, k_TargetSize});
        ASSERT_TRUE(m_target && m_target->IsValid());
        m_world = std::make_unique<EffectScene>(MakeEffectSceneDesc());
        ASSERT_TRUE(m_world->IsValid());
    }

    void TearDown() override
    {
        m_world.reset();
        if (m_renderer)
        {
            // target は Renderer より先に消えるので、指したままにせず外す
            m_renderer->SetSceneTarget(nullptr);
        }
        m_target.reset();
        m_renderer.reset();
        m_window.reset();
        EffectSceneTest::TearDown();
    }

    std::array<std::uint8_t, 4> DrawAndRead(const Camera& camera, PixelPosition at)
    {
        m_renderer->BeginSceneView(m_target.get());
        m_world->Draw(camera);
        return ReadPixel(*m_target, at);
    }

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<RenderTarget> m_target;
    std::unique_ptr<EffectScene> m_world;
};

TEST_F(EffectSceneTest, IsInvalidAndHarmlessWithoutRenderer)
{
    EffectScene world(MakeEffectSceneDesc());

    EXPECT_FALSE(world.IsValid());
    EXPECT_FALSE(world.Preload("square_r"));
    EXPECT_FALSE(world.Play(MakePlayDesc("square_r")).IsValid());
    world.Update(k_Frame);
    world.Draw(Camera{});
    world.Stop(EffectHandle{0});
    EXPECT_FALSE(world.Exists(EffectHandle{0}));
    world.StopAll();
}

TEST_F(EffectSceneWithRendererTest, IsValidWithRenderer)
{
    EXPECT_TRUE(m_world->IsValid());
}

TEST_F(EffectSceneWithRendererTest, PreloadReturnsFalseForMissingEffect)
{
    EXPECT_FALSE(m_world->Preload("no_such_effect"));
}

TEST_F(EffectSceneWithRendererTest, PlayReturnsInvalidHandleForEffectNotPreloaded)
{
    // ファイルは在るが Preload していない名前。再生の瞬間に読み込みを走らせないため、ここでは出せない
    EXPECT_FALSE(m_world->Play(MakePlayDesc("square_r")).IsValid());
}

TEST_F(EffectSceneWithRendererTest, PlayedEffectExistsUntilItsLifeEnds)
{
    ASSERT_TRUE(m_world->Preload("square_r"));

    const EffectHandle handle = m_world->Play(MakePlayDesc("square_r"));
    ASSERT_TRUE(handle.IsValid());
    EXPECT_TRUE(m_world->Exists(handle));

    Advance(*m_world, k_FramesBeforeLifeEnds);
    EXPECT_TRUE(m_world->Exists(handle));

    Advance(*m_world, k_FramesAfterLifeEnds - k_FramesBeforeLifeEnds);
    EXPECT_FALSE(m_world->Exists(handle));
}

TEST_F(EffectSceneWithRendererTest, ZeroDeltaDoesNotAdvanceEffect)
{
    ASSERT_TRUE(m_world->Preload("square_r"));
    const EffectHandle handle = m_world->Play(MakePlayDesc("square_r"));
    ASSERT_TRUE(handle.IsValid());
    // 寿命の手前まで進めて残りを 20 フレームにしてから 0 を渡す
    // 0 で 1 回に 1/6 フレームほど進むだけでも、120 回で寿命を越えて落ちる
    Advance(*m_world, k_FramesBeforeLifeEnds);

    for (int i = 0; i < k_FramesAfterLifeEnds; ++i)
    {
        m_world->Update(0.0f);
    }

    EXPECT_TRUE(m_world->Exists(handle));
}

TEST_F(EffectSceneWithRendererTest, NegativeDeltaIsIgnored)
{
    ASSERT_TRUE(m_world->Preload("square_r"));
    const EffectHandle handle = m_world->Play(MakePlayDesc("square_r"));
    ASSERT_TRUE(handle.IsValid());

    // Effekseer は負の経過を持ち越し、足して 0 以上になるまで進めない
    // そのまま渡すと止まった分だけ遅れ、寿命を過ぎても残る
    m_world->Update(-1.0f);
    Advance(*m_world, k_FramesAfterLifeEnds);

    EXPECT_FALSE(m_world->Exists(handle));
}

TEST_F(EffectSceneWithRendererTest, StopAndStopAllRemoveEffects)
{
    ASSERT_TRUE(m_world->Preload("square_r"));
    const EffectHandle first = m_world->Play(MakePlayDesc("square_r"));
    const EffectHandle second = m_world->Play(MakePlayDesc("square_r"));
    const EffectHandle third = m_world->Play(MakePlayDesc("square_r"));
    ASSERT_TRUE(first.IsValid());
    ASSERT_TRUE(second.IsValid());
    ASSERT_TRUE(third.IsValid());

    m_world->Stop(first);
    Advance(*m_world, 1);
    EXPECT_FALSE(m_world->Exists(first));
    EXPECT_TRUE(m_world->Exists(second));

    m_world->StopAll();
    Advance(*m_world, 1);
    EXPECT_FALSE(m_world->Exists(second));
    EXPECT_FALSE(m_world->Exists(third));
}

TEST_F(EffectSceneWithRendererTest, DrawChangesPixelsWhereEffectIsPlayed)
{
    ASSERT_TRUE(m_world->Preload("square_r"));
    const Camera camera = MakeCamera(NS::Core::Vector3{0.0f, 0.0f, -5.0f});

    // クリア色の既定値が変わっても壊れないよう、エフェクトを出す前の中央を基準にする
    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);

    ASSERT_TRUE(m_world->Play(MakePlayDesc("square_r")).IsValid());
    Advance(*m_world, 5);
    const std::array<std::uint8_t, 4> drawn = DrawAndRead(camera, k_Center);

    EXPECT_GE(LargestChannelDifference(cleared, drawn), 16);
}

TEST_F(EffectSceneWithRendererTest, StoppedEffectLeavesScreenWithZeroDelta)
{
    ASSERT_TRUE(m_world->Preload("square_r"));
    const Camera camera = MakeCamera(NS::Core::Vector3{0.0f, 0.0f, -5.0f});

    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);
    ASSERT_TRUE(m_world->Play(MakePlayDesc("square_r")).IsValid());
    Advance(*m_world, 5);
    ASSERT_GE(LargestChannelDifference(cleared, DrawAndRead(camera, k_Center)), 16);

    // 経過 0 のフレームでも、止めたエフェクトは止めた時の姿のまま画面に残らない
    m_world->StopAll();
    m_world->Update(0.0f);
    const std::array<std::uint8_t, 4> afterStop = DrawAndRead(camera, k_Center);

    EXPECT_LT(LargestChannelDifference(cleared, afterStop), 16);
}

TEST_F(EffectSceneWithRendererTest, EffectPlayedOffScreenLeavesCenterUnchanged)
{
    ASSERT_TRUE(m_world->Preload("square_r"));
    const Camera camera = MakeCamera(NS::Core::Vector3{0.0f, 0.0f, -5.0f});

    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);

    // 位置を捨てて原点に出す実装だと、中央が変わって落ちる
    EffectPlayDesc desc = MakePlayDesc("square_r");
    desc.position = NS::Core::Vector3{50.0f, 0.0f, 0.0f};
    ASSERT_TRUE(m_world->Play(desc).IsValid());
    Advance(*m_world, 5);
    const std::array<std::uint8_t, 4> drawn = DrawAndRead(camera, k_Center);

    EXPECT_LT(LargestChannelDifference(cleared, drawn), 16);
}

TEST_F(EffectSceneWithRendererTest, EffectAppearsOnTheSideOfItsPlayPosition)
{
    ASSERT_TRUE(m_world->Preload("marker_z_offset"));
    const Camera camera = MakeCamera(NS::Core::Vector3{0.0f, 0.0f, -5.0f});
    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);

    // marker_z_offset はエフェクトのデータで z を +2 ずらした小さい板
    // 左手系で読むと z が反転し、再生位置の z - 2 に出る
    EffectPlayDesc desc = MakePlayDesc("marker_z_offset");
    desc.position = NS::Core::Vector3{1.0f, 0.0f, 0.0f};
    ASSERT_TRUE(m_world->Play(desc).IsValid());
    Advance(*m_world, 5);
    const NS::Core::Vector3 expected{1.0f, 0.0f, -2.0f};
    const NS::Core::Vector3 mirrored{-1.0f, 0.0f, -2.0f};
    const std::array<std::uint8_t, 4> atExpected = DrawAndRead(camera, PixelOf(camera, expected));
    const std::array<std::uint8_t, 4> atMirrored = DrawAndRead(camera, PixelOf(camera, mirrored));

    EXPECT_GE(LargestChannelDifference(cleared, atExpected), 16);
    EXPECT_LT(LargestChannelDifference(cleared, atMirrored), 16);
}

TEST_F(EffectSceneWithRendererTest, EffectDataIsReadAsLeftHanded)
{
    ASSERT_TRUE(m_world->Preload("marker_z_offset"));

    // 正面からだと z のずれは大きさにしか出ない。横から見て z の符号を画面の左右に分ける
    const Camera camera = MakeCamera(NS::Core::Vector3{-5.0f, 0.0f, 0.0f});
    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);

    ASSERT_TRUE(m_world->Play(MakePlayDesc("marker_z_offset")).IsValid());
    Advance(*m_world, 5);
    const NS::Core::Vector3 expected{0.0f, 0.0f, -2.0f};
    const NS::Core::Vector3 mirrored{0.0f, 0.0f, 2.0f};
    const std::array<std::uint8_t, 4> atExpected = DrawAndRead(camera, PixelOf(camera, expected));
    const std::array<std::uint8_t, 4> atMirrored = DrawAndRead(camera, PixelOf(camera, mirrored));

    EXPECT_GE(LargestChannelDifference(cleared, atExpected), 16);
    EXPECT_LT(LargestChannelDifference(cleared, atMirrored), 16);
}

TEST_F(EffectSceneWithRendererTest, EffectBeyondItsDepthClippingIsNotDrawn)
{
    ASSERT_TRUE(m_world->Preload("marker_depth_clip"));
    const Camera camera = MakeCamera(NS::Core::Vector3{0.0f, 0.0f, -5.0f});
    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);

    // marker_depth_clip は深度クリップ 20。カメラ (z = -5) から 15 なら描き、22 なら描かない
    // 遠い方は原点からだと 17 なので、原点から測る実装だと描かれて落ちる
    EffectPlayDesc nearDesc = MakePlayDesc("marker_depth_clip");
    nearDesc.position = NS::Core::Vector3{0.0f, 0.0f, 10.0f};
    ASSERT_TRUE(m_world->Play(nearDesc).IsValid());
    Advance(*m_world, 5);
    ASSERT_GE(LargestChannelDifference(cleared, DrawAndRead(camera, k_Center)), 16);

    m_world->StopAll();
    EffectPlayDesc farDesc = MakePlayDesc("marker_depth_clip");
    farDesc.position = NS::Core::Vector3{0.0f, 0.0f, 17.0f};
    ASSERT_TRUE(m_world->Play(farDesc).IsValid());
    Advance(*m_world, 5);
    const std::array<std::uint8_t, 4> drawnFar = DrawAndRead(camera, k_Center);

    EXPECT_LT(LargestChannelDifference(cleared, drawnFar), 16);
}

TEST_F(EffectSceneWithRendererTest, PreloadRejectsEffectWithMissingTexture)
{
    // 試験の置き場には marker_textured の画像だけを置いてある
    // marker_texture_missing は同じ作りで、画像を読み損ねる
    EXPECT_TRUE(m_world->Preload("marker_textured"));
    EXPECT_FALSE(m_world->Preload("marker_texture_missing"));

    // 読み損ねた名前を登録すると、2 回目の Preload が通って色テクスチャの無いエフェクトが白で再生される
    EXPECT_FALSE(m_world->Preload("marker_texture_missing"));
    EXPECT_FALSE(m_world->Play(MakePlayDesc("marker_texture_missing")).IsValid());
}

TEST_F(EffectSceneWithRendererTest, PreloadReturnsTrueForLoadedName)
{
    ASSERT_TRUE(m_world->Preload("square_r"));
    EXPECT_TRUE(m_world->Preload("square_r"));
}
