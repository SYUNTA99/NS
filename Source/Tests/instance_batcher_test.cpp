#include <gtest/gtest.h>

#include <Framework/Core/Logger.h>
#include <Framework/Graphics/InstanceBatcher.h>
#include <Framework/Graphics/Renderer.h>
#include <Framework/Platform/Window.h>

#include <cstddef>
#include <cstdint>

namespace
{
    using NS::Graphics::BlockInstance;
    using NS::Graphics::InstanceBatcher;
    using NS::Graphics::Material;
    using NS::Graphics::StaticMesh;
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;

    WindowDesc MakeWindowDesc(const char* title)
    {
        WindowDesc d{};
        d.title = title;
        d.size = NS::Math::Size2D{320, 240};
        d.visible = false;
        return d;
    }

    RendererDesc MakeRendererDesc()
    {
        RendererDesc d{};
        d.vsync = false;
        d.enableDebugLayer = false;
        return d;
    }

    // bucket key を試験するための偽 Mesh* / Material* を生成する。 InstanceBatcher は
    // ポインタ値を bucket key としてしか扱わないため (count-only モードでは dereference もしない)、
    // 実体を指す必要は無い。 0xDEAD ベースで衝突しない 64bit アドレスを返す
    [[nodiscard]] StaticMesh* FakeMesh(std::uintptr_t index)
    {
        return reinterpret_cast<StaticMesh*>(static_cast<std::uintptr_t>(0xDEAD'0001ull) + index * 0x10ull);
    }

    [[nodiscard]] Material* FakeMaterial(std::uintptr_t index)
    {
        return reinterpret_cast<Material*>(static_cast<std::uintptr_t>(0xBEEF'0001ull) + index * 0x10ull);
    }
} // namespace

class InstanceBatcherTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(InstanceBatcherTest, BlockInstanceLayout)
{
    EXPECT_EQ(sizeof(BlockInstance), static_cast<std::size_t>(80))
        << "BlockInstance stride は HLSL slot1 layout (worldRow0..3 + color) と整合させるため 80 byte 固定";
    EXPECT_EQ(alignof(BlockInstance), static_cast<std::size_t>(16))
        << "BlockInstance は 16 byte 境界に揃える必要がある (SIMD + cbuffer 規約)";
}

TEST_F(InstanceBatcherTest, BucketsByMeshMaterial)
{
    Window window(MakeWindowDesc("ns_instance_batcher_buckets"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    InstanceBatcher batcher(renderer);
    NS::Graphics::detail::SetCountOnlyMode(batcher, true);
    batcher.BeginFrame();

    constexpr std::size_t kUniqueBuckets = 48;
    constexpr std::size_t kTotalInstances = 1000;

    // 48 種の (mesh, material) ペアに 1000 instance を散らす
    // 配分: 1000 / 48 ≈ 20.83、 商 20 で全 bucket に振り、 余り 40 を先頭 40 bucket に 1 つずつ足す
    for (std::size_t i = 0; i < kTotalInstances; ++i)
    {
        const std::uintptr_t bucketIndex = i % kUniqueBuckets;
        BlockInstance inst{};
        batcher.Submit(FakeMesh(bucketIndex), FakeMaterial(bucketIndex), inst);
    }

    EXPECT_EQ(batcher.BucketCount(), kUniqueBuckets) << "(mesh, material) 同一ペアは同一 bucket に集約されるはず";
}

TEST_F(InstanceBatcherTest, DrawCallBudgetUnder200)
{
    Window window(MakeWindowDesc("ns_instance_batcher_budget"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    InstanceBatcher batcher(renderer);
    NS::Graphics::detail::SetCountOnlyMode(batcher, true);
    batcher.BeginFrame();

    constexpr std::size_t kUniqueBuckets = 48;
    constexpr std::size_t kTotalInstances = 1000;
    for (std::size_t i = 0; i < kTotalInstances; ++i)
    {
        const std::uintptr_t bucketIndex = i % kUniqueBuckets;
        BlockInstance inst{};
        batcher.Submit(FakeMesh(bucketIndex), FakeMaterial(bucketIndex), inst);
    }

    batcher.FlushAll(renderer);

    EXPECT_LE(batcher.LastFrameDrawCallCount(), static_cast<std::size_t>(200))
        << "1000 block / 48 unique (mesh, material) bucket で draw call は 200 以下";
    EXPECT_EQ(batcher.LastFrameDrawCallCount(), kUniqueBuckets)
        << "空でない bucket ごとに 1 回 DrawIndexedInstanced する設計のため 48 一致が期待";
}

TEST_F(InstanceBatcherTest, EmptyBucketsAreSkipped)
{
    Window window(MakeWindowDesc("ns_instance_batcher_empty"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    InstanceBatcher batcher(renderer);
    NS::Graphics::detail::SetCountOnlyMode(batcher, true);
    batcher.BeginFrame();
    batcher.FlushAll(renderer);

    EXPECT_EQ(batcher.BucketCount(), static_cast<std::size_t>(0));
    EXPECT_EQ(batcher.LastFrameDrawCallCount(), static_cast<std::size_t>(0));
}
