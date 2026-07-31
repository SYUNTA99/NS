#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneData.h"
#include "Runtime/Object/World.h"
#include "Runtime/Physics/PhysicsWorld.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>

/// 更新経路の 1 回あたり所要時間を出し、 1 フレーム予算に対して無視できるかを数字で判断する
/// 時間で合否は決めない (機械の状態で揺れるため)。 assertion は更新が全 component へ届いた事だけ

namespace
{
    constexpr double k_FixedStepMs = 1000.0 / 60.0;

    // 帯を散らす。 同じ priority ばかりだとソートが最良ケースになり実態より速く出る
    constexpr int k_Bands[] = {NS::Object::TickPriority::EarlyUpdate,
                               NS::Object::TickPriority::Update,
                               NS::Object::TickPriority::LateUpdate,
                               NS::Object::TickPriority::LateUpdate + 50};

    // 更新回数だけ数える。 OnUpdate の中身が重いと経路の取り分が埋もれる
    class TickCountingComponent : public NS::Object::Component
    {
    public:
        explicit TickCountingComponent(int priority) noexcept : Component(priority) {}

        void OnUpdate() override { ++m_count; }

        [[nodiscard]] int Count() const noexcept { return m_count; }

        NS_REFLECT_NONE(TickCountingComponent, Component)

    private:
        int m_count = 0; // OnUpdate が呼ばれた回数
    };

    void Populate(NS::Object::World& world, std::size_t objectCount, std::size_t componentsPerObject)
    {
        for (std::size_t i = 0; i < objectCount; ++i)
        {
            auto* obj = world.Spawn<NS::Object::GameObject>();
            for (std::size_t c = 0; c < componentsPerObject; ++c)
                obj->AddComponent<TickCountingComponent>(k_Bands[(i + c) % std::size(k_Bands)]);
        }
    }

    [[nodiscard]] double MeasureMicros(NS::Object::World& world, int iterations, bool snapshotOnly)
    {
        const auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i)
        {
            if (snapshotOnly)
                world.SnapshotObjects();
            else
                world.UpdateAllObjects();
        }
        const auto end = std::chrono::steady_clock::now();
        const double totalMicros = std::chrono::duration<double, std::micro>(end - begin).count();
        return totalMicros / static_cast<double>(iterations);
    }

    void Report(const char* label, std::size_t objects, std::size_t components, double allMicros, double snapMicros)
    {
        const double budgetPercent = (allMicros / 1000.0) / k_FixedStepMs * 100.0;
        std::cout << "[更新経路の実測] " << label << " 配置物=" << objects << " component=" << components << "\n"
                  << "  UpdateAllObjects  " << allMicros << " us  (16.667ms 予算の " << budgetPercent << " %)\n"
                  << "  SnapshotObjects   " << snapMicros << " us\n"
                  << "  確保とソートの取り分 " << (allMicros - snapMicros) << " us" << std::endl;
    }

    // 経路が全 component を回したか。 時間ではなくここで合否を決める
    [[nodiscard]] int TotalTicks(const NS::Object::World& world)
    {
        int total = 0;
        world.ForEachComponent<TickCountingComponent>(
            [&total](const TickCountingComponent& comp) { total += comp.Count(); });
        return total;
    }
} // namespace

// 同梱レベル相当。 今の実物でどれだけ食うか
TEST(WorldUpdateCost, BundledLevelScale)
{
    constexpr std::size_t k_Objects = 4;
    constexpr std::size_t k_PerObject = 5; // 4 体で 20 個。 live 実測のおよそ 18 個に一番近い割り切り
    constexpr int k_Iterations = 20000;

    NS::Object::World world;
    Populate(world, k_Objects, k_PerObject);

    const double all = MeasureMicros(world, k_Iterations, false);
    const double snap = MeasureMicros(world, k_Iterations, true);
    Report("同梱レベル相当", k_Objects, k_Objects * k_PerObject, all, snap);

    EXPECT_EQ(TotalTicks(world), static_cast<int>(k_Objects * k_PerObject) * k_Iterations);
}

namespace
{
    // depth 段のツリーを組む。 実際のレベルは浅く、 1 列の鎖は最悪ケースの確認用
    [[nodiscard]] NS::Object::SceneData MakeParentTree(std::uint32_t count, std::uint32_t depth)
    {
        NS::Object::SceneData data{};
        data.objects.reserve(count);
        // 1 段あたりの体数。 深さ 1 なら全員が根、 count なら 1 列の鎖になる
        const std::uint32_t perLevel = std::max(std::uint32_t{1}, count / std::max(depth, std::uint32_t{1}));
        for (std::uint32_t i = 0; i < count; ++i)
        {
            NS::Object::ObjectData object{};
            object.objectId = i + 1;
            // 先頭の 1 段は根。 以降は 1 段上の同じ位置にぶら下げる
            if (i >= perLevel)
                object.parentId = i - perLevel + 1;
            NS::Object::EnsureTransformComponent(object);
            data.objects.push_back(std::move(object));
        }
        data.nextObjectId = count + 1;
        return data;
    }

    [[nodiscard]] double MeasureRebuildMicros(const NS::Object::SceneData& data, int iterations)
    {
        NS::Object::Scene scene;
        NS::Physics::PhysicsWorld physics;
        NS::Object::World world;
        const auto factory = [](const NS::Object::ObjectData& entry) {
            return NS::Object::BuildSceneObject(entry, nullptr);
        };

        const auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i)
            world.Rebuild(data, scene, physics, factory);
        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::micro>(end - begin).count() / static_cast<double>(iterations);
    }
} // namespace

// 実際のレベルに近い浅い階層。 読込と undo のたびに通る経路
TEST(WorldRebuildCost, ShallowTreeScale)
{
    for (const std::uint32_t count : {std::uint32_t{100}, std::uint32_t{500}, std::uint32_t{1000}})
    {
        const NS::Object::SceneData data = MakeParentTree(count, 3);
        const double micros = MeasureRebuildMicros(data, 20);
        std::cout << "[組み直しの実測] 深さ 3 段  配置物=" << count << "  Rebuild " << micros << " us\n";
    }
}

// 1 列の鎖。 実際には起きないが、 階層の深さが効く所を切り分けるために測る
TEST(WorldRebuildCost, DeepChainScale)
{
    for (const std::uint32_t count : {std::uint32_t{100}, std::uint32_t{500}, std::uint32_t{1000}})
    {
        const NS::Object::SceneData data = MakeParentTree(count, count);
        const double micros = MeasureRebuildMicros(data, 5);
        std::cout << "[組み直しの実測] 1 列の鎖  配置物=" << count << "  Rebuild " << micros << " us\n";
    }
}

// 余裕水準。 過去の最大 156 体の 6 倍以上を見る
TEST(WorldUpdateCost, HeadroomScale)
{
    constexpr std::size_t k_Objects = 1000;
    constexpr std::size_t k_PerObject = 4;
    constexpr int k_Iterations = 200;

    NS::Object::World world;
    Populate(world, k_Objects, k_PerObject);

    const double all = MeasureMicros(world, k_Iterations, false);
    const double snap = MeasureMicros(world, k_Iterations, true);
    Report("余裕水準", k_Objects, k_Objects * k_PerObject, all, snap);

    EXPECT_EQ(world.ObjectCount(), k_Objects);
    EXPECT_EQ(TotalTicks(world), static_cast<int>(k_Objects * k_PerObject) * k_Iterations);
}
