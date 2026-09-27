#include "Game/Player/EffectLayerList.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/EffectScene.h"
#include "Runtime/Graphics/Renderer.h"
#include "Runtime/Platform/Filesystem.h"
#include "Runtime/Platform/Window.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using NS::Game::Player::EffectLayerList;
    using NS::Game::Player::EffectLayerRecord;
    using NS::Gfx::EffectPlayDesc;

    constexpr float k_Frame = 1.0f / 60.0f;

    [[nodiscard]] std::vector<std::string> StartedNames(const EffectLayerList& layers)
    {
        std::vector<std::string> names;
        layers.AppendStartedNames(names);
        return names;
    }

    [[nodiscard]] std::vector<std::string> LiveNames(const EffectLayerList& layers, const NS::Gfx::EffectScene* effects)
    {
        std::vector<std::string> names;
        layers.AppendLiveNames(effects, names);
        return names;
    }
} // namespace

// 描画の無い世界でも、出すと決めた層は名前と出したフレームで残る。試しと Replay はこの記録を読む
TEST(EffectLayerList, RecordsTheLayerWithoutAnEffectScene)
{
    EffectLayerList layers;
    layers.BeginStep(nullptr);
    layers.BeginStep(nullptr);

    const std::uint32_t id = layers.Play(nullptr, "charge.curl", EffectPlayDesc{});

    const EffectLayerRecord* record = layers.Find(id);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->name, "charge.curl");
    EXPECT_EQ(record->startStep, 2);
    EXPECT_EQ(record->startStep, layers.Step());
    EXPECT_FALSE(record->rootStopStep.has_value());
    EXPECT_FALSE(record->endStep.has_value());
    EXPECT_FALSE(record->handle.IsValid());
    EXPECT_EQ(StartedNames(layers), (std::vector<std::string>{"charge.curl"}));
    EXPECT_EQ(LiveNames(layers, nullptr), (std::vector<std::string>{"charge.curl"}));
}

// 出した層として並ぶのは出したフレームだけ。次のフレームからは残っている層としてだけ並ぶ
TEST(EffectLayerList, NamesTheLayerAsStartedOnlyInItsOwnStep)
{
    EffectLayerList layers;
    layers.BeginStep(nullptr);
    static_cast<void>(layers.Play(nullptr, "impact.core", EffectPlayDesc{}));
    static_cast<void>(layers.Play(nullptr, "impact.core", EffectPlayDesc{}));
    layers.BeginStep(nullptr);
    static_cast<void>(layers.Play(nullptr, "impact.sparks", EffectPlayDesc{}));

    EXPECT_EQ(StartedNames(layers), (std::vector<std::string>{"impact.sparks"}));
    EXPECT_EQ(LiveNames(layers, nullptr), (std::vector<std::string>{"impact.core", "impact.core", "impact.sparks"}));
}

// 親を止めたフレームは最初の 1 回だけ残す。止めても子が残る間は残っている層
TEST(EffectLayerList, RecordsTheFirstRootStopAndKeepsTheLayerLive)
{
    EffectLayerList layers;
    layers.BeginStep(nullptr);
    const std::uint32_t id = layers.Play(nullptr, "slam.trail", EffectPlayDesc{});
    layers.BeginStep(nullptr);
    layers.StopRoot(nullptr, id);
    layers.BeginStep(nullptr);
    layers.StopRoot(nullptr, id);

    const EffectLayerRecord* record = layers.Find(id);
    ASSERT_NE(record, nullptr);
    ASSERT_TRUE(record->rootStopStep.has_value());
    EXPECT_EQ(record->rootStopStep.value(), 2);
    EXPECT_EQ(LiveNames(layers, nullptr), (std::vector<std::string>{"slam.trail"}));
}

// 消した層は消したフレームから残っている層に並ばない。消した後の親止めと 2 回目の消しは何もしない
TEST(EffectLayerList, EndsTheStoppedLayerInThatStepAndIgnoresLaterStops)
{
    EffectLayerList layers;
    layers.BeginStep(nullptr);
    const std::uint32_t id = layers.Play(nullptr, "charge.spin", EffectPlayDesc{});
    layers.BeginStep(nullptr);
    layers.Stop(nullptr, id);
    layers.BeginStep(nullptr);
    layers.Stop(nullptr, id);
    layers.StopRoot(nullptr, id);

    const EffectLayerRecord* record = layers.Find(id);
    ASSERT_NE(record, nullptr);
    ASSERT_TRUE(record->endStep.has_value());
    EXPECT_EQ(record->endStep.value(), 2);
    EXPECT_FALSE(record->rootStopStep.has_value());
    EXPECT_TRUE(LiveNames(layers, nullptr).empty());
}

// 無い番号を止めても、他の層の記録は変わらない
TEST(EffectLayerList, IgnoresAnUnknownNumber)
{
    EffectLayerList layers;
    const std::uint32_t id = layers.Play(nullptr, "charge.gather", EffectPlayDesc{});

    layers.StopRoot(nullptr, id + 1);
    layers.Stop(nullptr, id + 1);

    EXPECT_EQ(layers.Find(id + 1), nullptr);
    ASSERT_NE(layers.Find(id), nullptr);
    EXPECT_FALSE(layers.Find(id)->rootStopStep.has_value());
    EXPECT_FALSE(layers.Find(id)->endStep.has_value());
}

// 消えた記録は決めたフレーム数だけ残し、過ぎたら捨てる。溜まり続けると毎フレームの走査が伸びる
TEST(EffectLayerList, DropsAnEndedRecordOnlyAfterTheKeepWindow)
{
    EffectLayerList layers;
    layers.BeginStep(nullptr);
    const std::uint32_t ended = layers.Play(nullptr, "impact.dust", EffectPlayDesc{});
    const std::uint32_t live = layers.Play(nullptr, "impact.dust", EffectPlayDesc{});
    layers.Stop(nullptr, ended);

    for (int i = 0; i < EffectLayerList::k_KeepEndedSteps; ++i)
    {
        layers.BeginStep(nullptr);
    }
    EXPECT_NE(layers.Find(ended), nullptr);
    layers.BeginStep(nullptr);

    EXPECT_EQ(layers.Find(ended), nullptr);
    EXPECT_NE(layers.Find(live), nullptr);
}

// EffectScene は構築時に Gpu() の device と context を使うので、Renderer より後に作る
class EffectLayerListWithRendererTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        NS::Core::Logger::Init();
        NS::Platform::WindowDesc windowDesc{};
        windowDesc.title = "ns_effect_layer_list";
        windowDesc.size = NS::Core::Size2D{64, 64};
        windowDesc.visible = false;
        m_window = std::make_unique<NS::Platform::Window>(windowDesc);
        ASSERT_TRUE(m_window->IsValid());
        NS::Gfx::RendererDesc rendererDesc{};
#ifdef NS_BUILD_DEBUG
        rendererDesc.enableDebugLayer = true;
#else
        rendererDesc.enableDebugLayer = false;
#endif
        rendererDesc.vsync = false;
        m_renderer = std::make_unique<NS::Gfx::Renderer>(rendererDesc, *m_window);
        ASSERT_TRUE(m_renderer->IsValid());
        const std::string source = NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::ContentRoot(), "Source");
        const std::string data =
            NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::Combine(source, "Tests"), "data");
        m_effects = std::make_unique<NS::Gfx::EffectScene>(NS::Platform::FileSystem::Combine(data, "effects"));
        ASSERT_TRUE(m_effects->IsValid());
    }

    void TearDown() override
    {
        m_effects.reset();
        m_renderer.reset();
        m_window.reset();
        NS::Core::Logger::Shutdown();
    }

    std::unique_ptr<NS::Platform::Window> m_window;
    std::unique_ptr<NS::Gfx::Renderer> m_renderer;
    std::unique_ptr<NS::Gfx::EffectScene> m_effects;
};

// 親を止めた層は、子が寿命で消えた更新のあったフレームに残っている層から外れ、次のフレームの頭で同じフレームを
// 消えたフレームとして記録に書く。Replay が数える消えたフレームと、記録の消えたフレームが揃う
TEST_F(EffectLayerListWithRendererTest, EndsTheLayerAtTheStepWhoseUpdateRemovedIt)
{
    // stream_life10 は 1 フレームに 1 枚、寿命 10 の板を出し続ける
    ASSERT_TRUE(m_effects->Preload("stream_life10"));
    EffectLayerList layers;
    layers.BeginStep(m_effects.get());
    const std::uint32_t id = layers.Play(m_effects.get(), "stream_life10", EffectPlayDesc{});
    ASSERT_NE(layers.Find(id), nullptr);
    ASSERT_TRUE(layers.Find(id)->handle.IsValid());

    std::optional<int> firstStepNotLive;
    for (int i = 0; i < 60 && !firstStepNotLive.has_value(); ++i)
    {
        if (i > 0)
        {
            layers.BeginStep(m_effects.get());
        }
        if (layers.Step() == 5)
        {
            layers.StopRoot(m_effects.get(), id);
        }
        // 固定ステップの終わりの UpdateEffects と同じ所で進める
        m_effects->Update(k_Frame);
        if (LiveNames(layers, m_effects.get()).empty())
        {
            firstStepNotLive = layers.Step();
        }
    }
    ASSERT_TRUE(firstStepNotLive.has_value());
    // 止めた後も寿命 10 の板が残るので、止めたフレームより後に消える
    EXPECT_GT(firstStepNotLive.value(), 5);
    ASSERT_TRUE(layers.Find(id)->rootStopStep.has_value());
    EXPECT_EQ(layers.Find(id)->rootStopStep.value(), 5);
    EXPECT_FALSE(layers.Find(id)->endStep.has_value());

    layers.BeginStep(m_effects.get());

    ASSERT_TRUE(layers.Find(id)->endStep.has_value());
    EXPECT_EQ(layers.Find(id)->endStep.value(), firstStepNotLive.value());
}

// 読んでいない絵の層は再生されず、無効なハンドルで記録に残る。Stop までは残っている層として並ぶ
TEST_F(EffectLayerListWithRendererTest, KeepsTheRecordOfALayerWhosePictureWasNotLoaded)
{
    EffectLayerList layers;
    layers.BeginStep(m_effects.get());
    const std::uint32_t id = layers.Play(m_effects.get(), "not_loaded", EffectPlayDesc{});
    m_effects->Update(k_Frame);
    layers.BeginStep(m_effects.get());

    ASSERT_NE(layers.Find(id), nullptr);
    EXPECT_FALSE(layers.Find(id)->handle.IsValid());
    EXPECT_EQ(LiveNames(layers, m_effects.get()), (std::vector<std::string>{"not_loaded"}));
}
