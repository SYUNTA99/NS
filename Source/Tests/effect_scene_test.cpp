#include "pixel_readback.h"
#include <Runtime/Core/CameraData.h>
#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/EffectScene.h>
#include <Runtime/Graphics/GraphicObject.h>
#include <Runtime/Graphics/RenderTarget.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Graphics/Texture.h>
#include <Runtime/Platform/Filesystem.h>

#include <Runtime/Platform/Window.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>

namespace
{
    using NS::Core::CameraData;
    using NS::Gfx::EffectHandle;
    using NS::Gfx::EffectPlayDesc;
    using NS::Gfx::EffectScene;
    using NS::Gfx::Renderer;
    using NS::Gfx::RendererDesc;
    using NS::Gfx::RenderTarget;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;
    using NS::Tests::LargestChannelDifference;
    using NS::Tests::PixelPosition;
    using NS::Tests::ReadPixel;

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

    std::string TestEffectRoot()
    {
        const std::string source = NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::ContentRoot(), "Source");
        const std::string tests = NS::Platform::FileSystem::Combine(source, "Tests");
        return NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::Combine(tests, "data"), "effects");
    }

    CameraData MakeCamera(const NS::Core::Vector3& position)
    {
        CameraData camera;
        camera.SetPosition(position);
        camera.SetAspectRatio(1.0f);
        return camera;
    }

    void Advance(EffectScene& world, int frames)
    {
        for (int i = 0; i < frames; ++i)
        {
            world.Update(k_Frame);
        }
    }

    // handle が Exists でなくなるまで 1 フレームずつ進め、進めた回数を返す。limit 回進めても残っていれば limit
    int UpdatesUntilGone(EffectScene& world, EffectHandle handle, int limit)
    {
        for (int i = 0; i < limit; ++i)
        {
            if (!world.Exists(handle))
            {
                return i;
            }
            world.Update(k_Frame);
        }
        return limit;
    }

    constexpr PixelPosition k_Center{k_TargetSize / 2, k_TargetSize / 2};

    PixelPosition PixelOf(const CameraData& camera, const NS::Core::Vector3& position)
    {
        const NS::Core::Vector3 ndc = NS::Core::Vector3::Transform(position, camera.ViewProjection());
        PixelPosition pixel{};
        pixel.column = static_cast<int>((ndc.x * 0.5f + 0.5f) * k_TargetSize);
        pixel.row = static_cast<int>((0.5f - ndc.y * 0.5f) * k_TargetSize);
        return pixel;
    }

    // 背景からどれかの色の成分がこれ以上離れた画素を、描いた画素と数える
    constexpr int k_DrawnDifference = 16;

    [[nodiscard]] int ChannelRise(const std::array<std::uint8_t, 4>& background,
                                  const std::array<std::uint8_t, 4>& drawn,
                                  std::size_t channel)
    {
        return static_cast<int>(drawn[channel]) - static_cast<int>(background[channel]);
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
        m_world = std::make_unique<EffectScene>(TestEffectRoot());
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

    std::array<std::uint8_t, 4> DrawAndRead(const CameraData& camera, PixelPosition at)
    {
        Draw(camera);
        return ReadPixel(*m_target, at);
    }

    void Draw(const CameraData& camera)
    {
        m_renderer->BeginSceneView(m_target.get());
        m_world->Draw(camera);
    }

    // 最後に描いた絵の row 行目を左から右まで読み、描いた画素を数える
    int CountDrawnInRow(int row, const std::array<std::uint8_t, 4>& background)
    {
        int count = 0;
        for (int column = 0; column < k_TargetSize; ++column)
        {
            if (LargestChannelDifference(background, ReadPixel(*m_target, PixelPosition{column, row})) >=
                k_DrawnDifference)
            {
                ++count;
            }
        }
        return count;
    }

    // 最後に描いた絵の column 列目を上から下まで読み、描いた画素を数える
    int CountDrawnInColumn(int column, const std::array<std::uint8_t, 4>& background)
    {
        int count = 0;
        for (int row = 0; row < k_TargetSize; ++row)
        {
            if (LargestChannelDifference(background, ReadPixel(*m_target, PixelPosition{column, row})) >=
                k_DrawnDifference)
            {
                ++count;
            }
        }
        return count;
    }

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<RenderTarget> m_target;
    std::unique_ptr<EffectScene> m_world;
};

TEST_F(EffectSceneTest, IsInvalidAndHarmlessWithoutRenderer)
{
    EffectScene world(TestEffectRoot());

    EXPECT_FALSE(world.IsValid());
    EXPECT_FALSE(world.Preload("square_r"));
    EXPECT_FALSE(world.Play("square_r").IsValid());
    world.Update(k_Frame);
    world.Draw(CameraData{});
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
    EXPECT_FALSE(m_world->Play("square_r").IsValid());
}

TEST_F(EffectSceneWithRendererTest, PlayedEffectExistsUntilItsLifeEnds)
{
    ASSERT_TRUE(m_world->Preload("square_r"));

    const EffectHandle handle = m_world->Play("square_r");
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
    const EffectHandle handle = m_world->Play("square_r");
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
    const EffectHandle handle = m_world->Play("square_r");
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
    const EffectHandle first = m_world->Play("square_r");
    const EffectHandle second = m_world->Play("square_r");
    const EffectHandle third = m_world->Play("square_r");
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
    const CameraData camera = MakeCamera(NS::Core::Vector3{0.0f, 0.0f, -5.0f});

    // クリア色の既定値が変わっても壊れないよう、エフェクトを出す前の中央を基準にする
    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);

    ASSERT_TRUE(m_world->Play("square_r").IsValid());
    Advance(*m_world, 5);
    const std::array<std::uint8_t, 4> drawn = DrawAndRead(camera, k_Center);

    EXPECT_GE(LargestChannelDifference(cleared, drawn), 16);
}

TEST_F(EffectSceneWithRendererTest, StoppedEffectLeavesScreenWithZeroDelta)
{
    ASSERT_TRUE(m_world->Preload("square_r"));
    const CameraData camera = MakeCamera(NS::Core::Vector3{0.0f, 0.0f, -5.0f});

    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);
    ASSERT_TRUE(m_world->Play("square_r").IsValid());
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
    const CameraData camera = MakeCamera(NS::Core::Vector3{0.0f, 0.0f, -5.0f});

    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);

    // 位置を捨てて原点に出す実装だと、中央が変わって落ちる
    ASSERT_TRUE(m_world->Play("square_r", NS::Core::Vector3{50.0f, 0.0f, 0.0f}).IsValid());
    Advance(*m_world, 5);
    const std::array<std::uint8_t, 4> drawn = DrawAndRead(camera, k_Center);

    EXPECT_LT(LargestChannelDifference(cleared, drawn), 16);
}

TEST_F(EffectSceneWithRendererTest, EffectAppearsOnTheSideOfItsPlayPosition)
{
    ASSERT_TRUE(m_world->Preload("marker_z_offset"));
    const CameraData camera = MakeCamera(NS::Core::Vector3{0.0f, 0.0f, -5.0f});
    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);

    // marker_z_offset はエフェクトのデータで z を +2 ずらした小さい板
    // 左手系で読むと z が反転し、再生位置の z - 2 に出る
    ASSERT_TRUE(m_world->Play("marker_z_offset", NS::Core::Vector3{1.0f, 0.0f, 0.0f}).IsValid());
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
    const CameraData camera = MakeCamera(NS::Core::Vector3{-5.0f, 0.0f, 0.0f});
    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);

    ASSERT_TRUE(m_world->Play("marker_z_offset").IsValid());
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
    const CameraData camera = MakeCamera(NS::Core::Vector3{0.0f, 0.0f, -5.0f});
    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);

    // marker_depth_clip は深度クリップ 20。カメラ (z = -5) から 15 なら描き、22 なら描かない
    // 遠い方は原点からだと 17 なので、原点から測る実装だと描かれて落ちる
    ASSERT_TRUE(m_world->Play("marker_depth_clip", NS::Core::Vector3{0.0f, 0.0f, 10.0f}).IsValid());
    Advance(*m_world, 5);
    ASSERT_GE(LargestChannelDifference(cleared, DrawAndRead(camera, k_Center)), 16);

    m_world->StopAll();
    ASSERT_TRUE(m_world->Play("marker_depth_clip", NS::Core::Vector3{0.0f, 0.0f, 17.0f}).IsValid());
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

    // 読み損ねた名前を登録する実装だと、2 回目の Preload が通って色テクスチャの無いエフェクトが白で再生される
    EXPECT_FALSE(m_world->Preload("marker_texture_missing"));
    EXPECT_FALSE(m_world->Play("marker_texture_missing").IsValid());
}

TEST_F(EffectSceneWithRendererTest, PreloadReturnsTrueForLoadedName)
{
    ASSERT_TRUE(m_world->Preload("square_r"));
    EXPECT_TRUE(m_world->Preload("square_r"));
}

// 大きさは絵の形そのものに掛かる。位置だけを渡す実装だと幅が変わらずに落ちる
TEST_F(EffectSceneWithRendererTest, PlayWithScaleTwoDrawsTheBoardTwiceAsWide)
{
    ASSERT_TRUE(m_world->Preload("board_wide"));
    const CameraData camera = MakeCamera(NS::Core::Vector3{0.0f, 0.0f, -5.0f});
    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);

    const EffectHandle unit = m_world->Play("board_wide", EffectPlayDesc{});
    ASSERT_TRUE(unit.IsValid());
    Advance(*m_world, 1);
    Draw(camera);
    const int unitWidth = CountDrawnInRow(k_Center.row, cleared);

    m_world->Stop(unit);
    const EffectHandle doubled =
        m_world->Play("board_wide", EffectPlayDesc{.scale = NS::Core::Vector3{2.0f, 2.0f, 2.0f}});
    ASSERT_TRUE(doubled.IsValid());
    Advance(*m_world, 1);
    Draw(camera);
    const int doubledWidth = CountDrawnInRow(k_Center.row, cleared);

    // 横 2 m の板は 5 m 先で約 22 画素。両端の丸めで 1 画素ずつ揺れる
    ASSERT_GE(unitWidth, 16);
    EXPECT_NEAR(doubledWidth, 2 * unitWidth, 2);
}

// 向きの固定された横長の板を、視線の軸まわりに 90 度回すと縦長に描かれる
TEST_F(EffectSceneWithRendererTest, PlayWithRotationTurnsTheFixedBoard)
{
    ASSERT_TRUE(m_world->Preload("board_wide"));
    const CameraData camera = MakeCamera(NS::Core::Vector3{0.0f, 0.0f, -5.0f});
    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);

    const EffectHandle upright = m_world->Play("board_wide", EffectPlayDesc{});
    ASSERT_TRUE(upright.IsValid());
    Advance(*m_world, 1);
    Draw(camera);
    const int uprightWidth = CountDrawnInRow(k_Center.row, cleared);
    const int uprightHeight = CountDrawnInColumn(k_Center.column, cleared);

    m_world->Stop(upright);
    const NS::Core::Quaternion quarterTurn =
        NS::Core::Quaternion::CreateFromAxisAngle(NS::Core::Vector3{0.0f, 0.0f, 1.0f}, DirectX::XM_PIDIV2);
    const EffectHandle turned = m_world->Play("board_wide", EffectPlayDesc{.rotation = quarterTurn});
    ASSERT_TRUE(turned.IsValid());
    Advance(*m_world, 1);
    Draw(camera);
    const int turnedWidth = CountDrawnInRow(k_Center.row, cleared);
    const int turnedHeight = CountDrawnInColumn(k_Center.column, cleared);

    ASSERT_GT(uprightWidth, 3 * uprightHeight);
    EXPECT_NEAR(turnedHeight, uprightWidth, 2);
    EXPECT_NEAR(turnedWidth, uprightHeight, 2);
}

// 付いていく層は毎フレーム姿勢を渡し直す。渡した後の更新で描く位置が動く
TEST_F(EffectSceneWithRendererTest, SetTransformMovesWhereTheEffectIsDrawn)
{
    ASSERT_TRUE(m_world->Preload("board_wide"));
    const CameraData camera = MakeCamera(NS::Core::Vector3{0.0f, 0.0f, -5.0f});
    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);

    const EffectHandle handle = m_world->Play("board_wide", EffectPlayDesc{});
    ASSERT_TRUE(handle.IsValid());
    Advance(*m_world, 1);
    ASSERT_GE(LargestChannelDifference(cleared, DrawAndRead(camera, k_Center)), k_DrawnDifference);

    // 縦 0.4 m の板を 1.5 m 上げると、中央の行から外れる
    const NS::Core::Vector3 raised{0.0f, 1.5f, 0.0f};
    m_world->SetTransform(handle, raised, NS::Core::Quaternion::Identity, NS::Core::Vector3{1.0f, 1.0f, 1.0f});
    Advance(*m_world, 1);

    EXPECT_LT(LargestChannelDifference(cleared, DrawAndRead(camera, k_Center)), k_DrawnDifference);
    EXPECT_GE(LargestChannelDifference(cleared, DrawAndRead(camera, PixelOf(camera, raised))), k_DrawnDifference);
}

// stack_by_input は動的入力 0 番の枚数だけ加算の板を同じ所に重ねる。1 枚で明るさ 32
TEST_F(EffectSceneWithRendererTest, DynamicInputChangesHowManyAreSpawned)
{
    ASSERT_TRUE(m_world->Preload("stack_by_input"));
    const CameraData camera = MakeCamera(NS::Core::Vector3{0.0f, 0.0f, -5.0f});
    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);

    const EffectHandle one = m_world->Play("stack_by_input", EffectPlayDesc{.dynamicInputs = {1.0f}});
    ASSERT_TRUE(one.IsValid());
    Advance(*m_world, 1);
    const int oneRise = LargestChannelDifference(cleared, DrawAndRead(camera, k_Center));

    m_world->Stop(one);
    const EffectHandle four = m_world->Play("stack_by_input", EffectPlayDesc{.dynamicInputs = {4.0f}});
    ASSERT_TRUE(four.IsValid());
    Advance(*m_world, 1);
    const int fourRise = LargestChannelDifference(cleared, DrawAndRead(camera, k_Center));

    // 再生した後、最初の更新の前に渡した値でも数が決まる
    m_world->Stop(four);
    const EffectHandle later = m_world->Play("stack_by_input", EffectPlayDesc{});
    ASSERT_TRUE(later.IsValid());
    m_world->SetDynamicInput(later, 0, 4.0f);
    Advance(*m_world, 1);
    const int laterRise = LargestChannelDifference(cleared, DrawAndRead(camera, k_Center));

    ASSERT_GE(oneRise, k_DrawnDifference);
    // 3 枚ぶんで 96。8 ビットの丸めを見込んで 80
    EXPECT_GE(fourRise - oneRise, 80);
    EXPECT_NEAR(laterRise, fourRise, 2);
}

// stream_life10 は 1 フレームに 1 枚、寿命 10 の加算の板を出し続ける。1 枚で明るさ 20
// 親だけ止めると新しい板は出ないが、出ていた板は寿命まで残って描かれる
TEST_F(EffectSceneWithRendererTest, StopRootSpawnsNoMoreButKeepsTheChildrenUntilTheirLifeEnds)
{
    ASSERT_TRUE(m_world->Preload("stream_life10"));
    const CameraData camera = MakeCamera(NS::Core::Vector3{0.0f, 0.0f, -5.0f});
    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);

    const NS::Core::Vector3 keptAt{-1.5f, 0.0f, 0.0f};
    const NS::Core::Vector3 stoppedAt{1.5f, 0.0f, 0.0f};
    const EffectHandle kept = m_world->Play("stream_life10", EffectPlayDesc{.position = keptAt});
    const EffectHandle stopped = m_world->Play("stream_life10", EffectPlayDesc{.position = stoppedAt});
    ASSERT_TRUE(kept.IsValid());
    ASSERT_TRUE(stopped.IsValid());
    // 寿命の 2 倍進めて、生きている板を 10 枚に揃える
    Advance(*m_world, 20);

    m_world->StopRoot(stopped);
    Advance(*m_world, 5);
    Draw(camera);
    const int keptRise = LargestChannelDifference(cleared, ReadPixel(*m_target, PixelOf(camera, keptAt)));
    const int stoppedRise = LargestChannelDifference(cleared, ReadPixel(*m_target, PixelOf(camera, stoppedAt)));

    // 止めた方は寿命の残る 5 枚ぶん、止めていない方より 100 暗い。丸めと飽和を見込んで 40
    EXPECT_GE(stoppedRise, k_DrawnDifference);
    EXPECT_LE(stoppedRise, keptRise - 40);
    EXPECT_TRUE(m_world->Exists(stopped));

    Advance(*m_world, 10);
    EXPECT_FALSE(m_world->Exists(stopped));
    EXPECT_TRUE(m_world->Exists(kept));
}

// 親だけ止めるのと違い、Stop は子も含めて経過 0 の更新で画面から消える
TEST_F(EffectSceneWithRendererTest, StopRemovesTheEffectWithinTheSameFrame)
{
    ASSERT_TRUE(m_world->Preload("board_wide"));
    const CameraData camera = MakeCamera(NS::Core::Vector3{0.0f, 0.0f, -5.0f});
    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);

    const EffectHandle handle = m_world->Play("board_wide", EffectPlayDesc{});
    ASSERT_TRUE(handle.IsValid());
    Advance(*m_world, 1);
    ASSERT_GE(LargestChannelDifference(cleared, DrawAndRead(camera, k_Center)), k_DrawnDifference);

    m_world->Stop(handle);
    m_world->Update(0.0f);

    EXPECT_FALSE(m_world->Exists(handle));
    EXPECT_LT(LargestChannelDifference(cleared, DrawAndRead(camera, k_Center)), k_DrawnDifference);
}

// 全体に掛けた色で描く。白い板に赤を掛けると緑と青は背景のまま
TEST_F(EffectSceneWithRendererTest, PlayWithColorTintsTheEffect)
{
    ASSERT_TRUE(m_world->Preload("board_wide"));
    const CameraData camera = MakeCamera(NS::Core::Vector3{0.0f, 0.0f, -5.0f});
    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);

    const EffectHandle handle =
        m_world->Play("board_wide", EffectPlayDesc{.color = NS::Core::Color{1.0f, 0.0f, 0.0f, 1.0f}});
    ASSERT_TRUE(handle.IsValid());
    Advance(*m_world, 1);
    const std::array<std::uint8_t, 4> drawn = DrawAndRead(camera, k_Center);

    EXPECT_GE(ChannelRise(cleared, drawn, 0), k_DrawnDifference);
    EXPECT_LT(ChannelRise(cleared, drawn, 1), k_DrawnDifference);
    EXPECT_LT(ChannelRise(cleared, drawn, 2), k_DrawnDifference);
}

// mover_x は生まれた所から +X へ 1 フレーム 0.5 m 進む縦横 0.4 m の板
// Play した層は、その後の最初の更新で生まれ、生まれた瞬間の姿で描かれる。更新の前は何も描かない
// 固定ステップの中で Play した層は、同じステップの終わりの UpdateEffects の後の絵に生まれた姿で写る
TEST_F(EffectSceneWithRendererTest, PlayedEffectIsDrawnAtItsBirthAfterTheFirstUpdate)
{
    ASSERT_TRUE(m_world->Preload("mover_x"));
    const CameraData camera = MakeCamera(NS::Core::Vector3{0.0f, 0.0f, -5.0f});
    const std::array<std::uint8_t, 4> cleared = DrawAndRead(camera, k_Center);
    const PixelPosition oneStepAhead = PixelOf(camera, NS::Core::Vector3{0.5f, 0.0f, 0.0f});

    const EffectHandle handle = m_world->Play("mover_x", EffectPlayDesc{});
    ASSERT_TRUE(handle.IsValid());
    EXPECT_LT(LargestChannelDifference(cleared, DrawAndRead(camera, k_Center)), k_DrawnDifference);

    Advance(*m_world, 1);
    EXPECT_GE(LargestChannelDifference(cleared, DrawAndRead(camera, k_Center)), k_DrawnDifference);
    EXPECT_LT(LargestChannelDifference(cleared, DrawAndRead(camera, oneStepAhead)), k_DrawnDifference);

    Advance(*m_world, 1);
    EXPECT_LT(LargestChannelDifference(cleared, DrawAndRead(camera, k_Center)), k_DrawnDifference);
    EXPECT_GE(LargestChannelDifference(cleared, DrawAndRead(camera, oneStepAhead)), k_DrawnDifference);
}

// life_random は寿命が 5〜60 フレームの乱数で決まる板 1 枚
// 同じ名前の同じ回数目の再生は、先に別の絵を出していても、他の所で std::rand を引いていても同じ寿命になる
TEST_F(EffectSceneWithRendererTest, OtherEffectsPlayedBeforeDoNotChangeTheRandomLife)
{
    constexpr int k_Limit = 200;
    ASSERT_TRUE(m_world->Preload("life_random"));
    const int alone = UpdatesUntilGone(*m_world, m_world->Play("life_random"), k_Limit);

    EffectScene other(TestEffectRoot());
    ASSERT_TRUE(other.IsValid());
    ASSERT_TRUE(other.Preload("life_random"));
    ASSERT_TRUE(other.Preload("square_r"));
    for (int i = 0; i < 3; ++i)
    {
        static_cast<void>(other.Play("square_r"));
    }
    static_cast<void>(std::rand());
    const int afterOthers = UpdatesUntilGone(other, other.Play("life_random"), k_Limit);

    EXPECT_GT(alone, 0);
    EXPECT_LT(alone, k_Limit);
    EXPECT_EQ(alone, afterOthers);
}
