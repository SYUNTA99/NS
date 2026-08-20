#include "Game/Level/BlockObject.h"
#include "Game/Player.h"

#include <Game/Level/BreakableComponent.h>
#include <Game/Level/ImpactResolverComponent.h>
#include <Game/Level/LaunchedBodyComponent.h>
#include <Game/Level/MomentumComponent.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/BoxColliderComponent.h>
#include <Runtime/Object/Components/CameraBrainComponent.h>
#include <Runtime/Object/Components/CharacterMovementComponent.h>
#include <Runtime/Object/Components/PlacedVirtualCamera.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Reflection/TypeRegistry.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/World.h>
#include <Runtime/Physics/PhysicsWorld.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <string_view>
#include <utility>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Object;

namespace
{
    using NS::Core::Vector3;

    constexpr float k_FixedDt = 1.0f / 60.0f;
    constexpr float k_ReboundSpeed = 9.0f;
    constexpr float k_ReboundUpSpeed = 3.0f;
    constexpr float k_RunSpeed = 8.0f;
    constexpr float k_MaxDashSpeed = 16.0f;
    constexpr float k_LaunchSpeedCap = 60.0f;
    constexpr float k_ReboundSpeedCap = 24.0f;

    struct Rig
    {
        SceneNs::CharacterMovementComponent* movement = nullptr;
        LevelNs::MomentumComponent* momentum = nullptr;
        LevelNs::ImpactResolverComponent* impact = nullptr;
        SceneNs::BoxColliderComponent* targetBox = nullptr;
        LevelNs::BreakableComponent* breakable = nullptr;
    };

    Rig Build(
        SceneNs::Scene& scene, const Vector3& playerPos, std::int16_t cellX, std::int16_t cellZ, bool withBreakable)
    {
        NS::Core::FrameTimer::SetFixedDelta(k_FixedDt);

        SceneNs::SceneData data;
        SceneNs::ObjectData player = MakePlayerObject(playerPos, NS::Core::Quaternion{});
        player.components.push_back(SceneNs::MakeComponentEntry("MomentumComponent"));
        player.components.push_back(SceneNs::MakeComponentEntry("ImpactResolverComponent"));
        data.objects.push_back(player);

        SceneNs::ObjectData target = LevelNs::MakeCellObject(cellX, 0, cellZ);
        if (withBreakable)
            target.components.push_back(SceneNs::MakeComponentEntry("BreakableComponent"));
        data.objects.push_back(target);
        scene.LoadFromData(std::move(data));

        Rig rig;
        Player* live = FindPlayer(scene.World());
        EXPECT_NE(live, nullptr);
        if (live != nullptr)
        {
            rig.movement = live->FindComponent<SceneNs::CharacterMovementComponent>();
            rig.momentum = live->FindComponent<LevelNs::MomentumComponent>();
            rig.impact = live->FindComponent<LevelNs::ImpactResolverComponent>();
        }
        scene.World().ForEachComponent<SceneNs::BoxColliderComponent>(
            [&rig](SceneNs::BoxColliderComponent& box) { rig.targetBox = &box; });
        scene.World().ForEachComponent<LevelNs::BreakableComponent>(
            [&rig](LevelNs::BreakableComponent& breakable) { rig.breakable = &breakable; });
        return rig;
    }

    LevelNs::LaunchedBodyComponent* HitBody(const Rig& rig)
    {
        if (rig.targetBox == nullptr)
            return nullptr;
        return rig.targetBox->Owner()->FindComponent<LevelNs::LaunchedBodyComponent>();
    }

    void Step(SceneNs::Scene& scene)
    {
        scene.World().UpdateObjects(SceneNs::TickPriority::EarlyUpdate, SceneNs::TickPriority::Update);
    }

    [[nodiscard]] float HorizontalSpeed(const Vector3& v) noexcept
    {
        return std::sqrt(v.x * v.x + v.z * v.z);
    }

    void SetFloatField(SceneNs::Component& comp, std::string_view label, float value)
    {
        const SceneNs::FieldDesc* field = SceneNs::FindField(comp.GetReflection(), label);
        ASSERT_NE(field, nullptr);
        field->set(&comp, &value);
    }

    // ヒットストップを 0 にして、衝突の結果をその歩のうちに適用させる
    void SetInstantImpact(Rig& rig)
    {
        SetFloatField(*rig.impact, "ヒットストップ基準秒", 0.0f);
    }

    // 自機の移動が起きるまで回して掛かった歩数を返す。起きなければ maxSteps を返す
    int StepsUntilMovementActive(SceneNs::Scene& scene, const Rig& rig, int maxSteps)
    {
        for (int i = 0; i < maxSteps; ++i)
        {
            Step(scene);
            if (rig.movement->IsActiveSelf())
                return i + 1;
        }
        return maxSteps;
    }

    // 飛んで着地して滑り切るまでの道。狭いと端から落ちて停止の検証にならない
    constexpr std::int16_t k_FloorFirstX = -2;
    constexpr std::int16_t k_FloorLastX = 8;
    constexpr float k_BodyRestY = 1.0f; // 床の上面 0.5 に半分の高さ 0.5 を足した静止の高さ
    constexpr float k_LaunchGravity = -25.0f;
    constexpr int k_RestStepLimit = 600;

    struct BodyRig
    {
        SceneNs::GameObject* object = nullptr;
        LevelNs::LaunchedBodyComponent* body = nullptr;
        SceneNs::BoxColliderComponent* box = nullptr;
        std::size_t restingAabbs = 0;
    };

    // 床を 1 列並べ、その上へ飛ばされる物を 1 個置く検証台。自機は要らない
    BodyRig BuildBody(SceneNs::Scene& scene)
    {
        NS::Core::FrameTimer::SetFixedDelta(k_FixedDt);

        SceneNs::SceneData data;
        for (std::int16_t x = k_FloorFirstX; x <= k_FloorLastX; ++x)
            data.objects.push_back(LevelNs::MakeCellObject(x, 0, 0));

        SceneNs::ObjectData target = LevelNs::MakeCellObject(0, 1, 0);
        target.components.push_back(SceneNs::MakeComponentEntry("LaunchedBodyComponent"));
        data.objects.push_back(target);
        scene.LoadFromData(std::move(data));

        BodyRig rig;
        scene.World().ForEachComponent<LevelNs::LaunchedBodyComponent>(
            [&rig](LevelNs::LaunchedBodyComponent& body) { rig.body = &body; });
        if (rig.body != nullptr)
        {
            rig.object = rig.body->Owner();
            rig.box = rig.object->FindComponent<SceneNs::BoxColliderComponent>();
        }
        rig.restingAabbs = scene.Physics().Aabbs().size();
        return rig;
    }

    // 飛ばされる物が乗る帯だけを回す
    void StepBody(SceneNs::Scene& scene)
    {
        scene.World().UpdateObjects(SceneNs::TickPriority::Update, SceneNs::TickPriority::LateUpdate);
    }

    // 止まるまで回して掛かった歩数を返す。止まらなければ maxSteps を返す
    int RunUntilRest(SceneNs::Scene& scene, LevelNs::LaunchedBodyComponent& body, int maxSteps)
    {
        for (int i = 0; i < maxSteps; ++i)
        {
            StepBody(scene);
            if (!body.IsFlying())
                return i + 1;
        }
        return maxSteps;
    }
} // namespace

TEST(CollisionImpact, ReboundsAwayFromApproachedBox)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    ASSERT_NE(rig.movement, nullptr);
    ASSERT_NE(rig.impact, nullptr);
    SetInstantImpact(rig);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);

    EXPECT_TRUE(rig.impact->DidRebound());
    EXPECT_LT(rig.movement->Velocity().x, 0.0f);
    EXPECT_GT(rig.movement->Velocity().y, 0.0f);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().z, 0.0f);
}

// 入りが速いほど返りも速い。速く行くほど損になると、勢いを作る意味が消える
TEST(CollisionImpact, FasterImpactReboundsFaster)
{
    SceneNs::Scene normalScene;
    Rig normal = Build(normalScene, Vector3{}, 1, 0, true);
    SetInstantImpact(normal);
    normal.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});
    Step(normalScene);

    SceneNs::Scene maxScene;
    Rig maxDash = Build(maxScene, Vector3{}, 1, 0, true);
    SetInstantImpact(maxDash);
    maxDash.movement->SetVelocity(Vector3{k_MaxDashSpeed, 0.0f, 0.0f});
    Step(maxScene);

    ASSERT_TRUE(normal.impact->DidRebound());
    ASSERT_TRUE(maxDash.impact->DidRebound());
    const float slow = HorizontalSpeed(normal.movement->Velocity());
    const float fast = HorizontalSpeed(maxDash.movement->Velocity());
    EXPECT_GT(slow, 0.0f);
    EXPECT_GT(fast, slow);
}

// 重い物ほど壁として返す。軽い物は勢いを持っていくので返りが弱い
TEST(CollisionImpact, HeavierTargetReboundsHarder)
{
    SceneNs::Scene lightScene;
    Rig light = Build(lightScene, Vector3{}, 1, 0, true);
    SetInstantImpact(light);
    ASSERT_NE(light.breakable, nullptr);
    light.breakable->SetMass(1.0f);
    light.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});
    Step(lightScene);

    SceneNs::Scene heavyScene;
    Rig heavy = Build(heavyScene, Vector3{}, 1, 0, true);
    SetInstantImpact(heavy);
    ASSERT_NE(heavy.breakable, nullptr);
    heavy.breakable->SetMass(8.0f);
    heavy.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});
    Step(heavyScene);

    const float weak = HorizontalSpeed(light.movement->Velocity());
    const float strong = HorizontalSpeed(heavy.movement->Velocity());
    EXPECT_GT(weak, 0.0f);
    EXPECT_GT(strong, weak);
}

TEST(CollisionImpact, ReboundSpeedIsCapped)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    SetInstantImpact(rig);
    SetFloatField(*rig.impact, "反発基準初速", 100.0f);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);

    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_FLOAT_EQ(HorizontalSpeed(rig.movement->Velocity()), k_ReboundSpeedCap);
}

TEST(CollisionImpact, ReboundAddsUpSpeed)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    SetInstantImpact(rig);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);

    EXPECT_FLOAT_EQ(rig.movement->Velocity().y, k_ReboundUpSpeed);
}

TEST(CollisionImpact, ReboundStartsMomentumGrace)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    ASSERT_NE(rig.momentum, nullptr);
    SetInstantImpact(rig);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});
    ASSERT_FALSE(rig.movement->IsGrounded());

    Step(scene);
    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_FLOAT_EQ(rig.momentum->GraceSeconds(), 0.0f);

    Step(scene);

    EXPECT_FLOAT_EQ(rig.momentum->GraceSeconds(), k_FixedDt);
}

TEST(CollisionImpact, ReboundDirectionFollowsBoxAxis)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 0, 1, true);
    SetInstantImpact(rig);
    rig.movement->SetVelocity(Vector3{0.0f, 0.0f, k_RunSpeed});

    Step(scene);

    EXPECT_TRUE(rig.impact->DidRebound());
    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, 0.0f);
    EXPECT_LT(rig.movement->Velocity().z, 0.0f);
}

TEST(CollisionImpact, ReboundsFromDeepOverlapWhenApproaching)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{0.55f, 0.0f, 0.0f}, 1, 0, true);
    SetInstantImpact(rig);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);

    EXPECT_TRUE(rig.impact->DidRebound());
    EXPECT_LT(rig.movement->Velocity().x, 0.0f);
}

TEST(CollisionImpact, DoesNotReapplyWhileSeparating)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{0.55f, 0.0f, 0.0f}, 1, 0, true);
    const Vector3 leaving{-k_ReboundSpeed, k_ReboundUpSpeed, 0.0f};
    rig.movement->SetVelocity(leaving);

    Step(scene);

    EXPECT_FALSE(rig.impact->DidRebound());
    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, leaving.x);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().y, leaving.y);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().z, leaving.z);
}

TEST(CollisionImpact, NoReboundWithoutBreakableMarks)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, false);
    const Vector3 running{k_RunSpeed, 0.0f, 0.0f};
    rig.movement->SetVelocity(running);

    Step(scene);

    EXPECT_FALSE(rig.impact->DidRebound());
    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, running.x);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().y, running.y);
}

// 世界を総当たりで回るので、重なりを見ないと離れた所に置いた物にも反発する
TEST(CollisionImpact, NoReboundAgainstDistantBox)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 8, 0, true);
    const Vector3 running{k_RunSpeed, 0.0f, 0.0f};
    rig.movement->SetVelocity(running);

    Step(scene);

    EXPECT_FALSE(rig.impact->DidRebound());
    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, running.x);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().y, running.y);
}

TEST(CollisionImpact, NoReboundAgainstTriggerBox)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    ASSERT_NE(rig.targetBox, nullptr);
    rig.targetBox->SetTrigger(true);
    const Vector3 running{k_RunSpeed, 0.0f, 0.0f};
    rig.movement->SetVelocity(running);

    Step(scene);

    EXPECT_FALSE(rig.impact->DidRebound());
    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, running.x);
}

TEST(CollisionImpact, ReboundFieldsDriveVelocity)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    SetInstantImpact(rig);
    SetFloatField(*rig.impact, "反発基準初速", 5.0f);
    SetFloatField(*rig.impact, "反発の上向き初速", 1.0f);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);

    // 返り = 基準 5 × 勢いの比 1 × 質量因子 1/(1+1)
    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, -2.5f);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().y, 1.0f);
}

TEST(CollisionImpact, IsCreatableFromTypeName)
{
    SceneNs::GameObject obj;
    SceneNs::Component* comp = SceneNs::CreateComponent("ImpactResolverComponent", obj);
    ASSERT_NE(comp, nullptr);
    ASSERT_NE(comp->GetReflection(), nullptr);
    EXPECT_STREQ(comp->GetReflection()->typeName, "ImpactResolverComponent");
    EXPECT_EQ(obj.FindComponent<LevelNs::ImpactResolverComponent>(), comp);
}

TEST(CollisionImpact, ReboundLaunchesHitBody)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    ASSERT_EQ(HitBody(rig), nullptr);
    SetInstantImpact(rig);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);

    ASSERT_TRUE(rig.impact->DidRebound());
    LevelNs::LaunchedBodyComponent* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(body->IsFlying());
}

TEST(CollisionImpact, LaunchDirectionFollowsApproach)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    SetInstantImpact(rig);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);

    LevelNs::LaunchedBodyComponent* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    const Vector3 toBox = rig.targetBox->WorldAABB().Center - Vector3{};
    const Vector3 launch = body->Velocity();
    EXPECT_GT(launch.x * toBox.x + launch.z * toBox.z, 0.0f);
}

TEST(CollisionImpact, LaunchLiftsHitBody)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    SetInstantImpact(rig);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);

    LevelNs::LaunchedBodyComponent* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_GT(body->Velocity().y, 0.0f);
    EXPECT_LT(body->Velocity().y, HorizontalSpeed(body->Velocity()));
}

TEST(CollisionImpact, HeavierBodyLaunchesSlower)
{
    SceneNs::Scene lightScene;
    Rig light = Build(lightScene, Vector3{}, 1, 0, true);
    SetInstantImpact(light);
    ASSERT_NE(light.breakable, nullptr);
    light.breakable->SetMass(1.0f);
    light.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});
    Step(lightScene);

    SceneNs::Scene heavyScene;
    Rig heavy = Build(heavyScene, Vector3{}, 1, 0, true);
    SetInstantImpact(heavy);
    ASSERT_NE(heavy.breakable, nullptr);
    heavy.breakable->SetMass(4.0f);
    heavy.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});
    Step(heavyScene);

    LevelNs::LaunchedBodyComponent* lightBody = HitBody(light);
    LevelNs::LaunchedBodyComponent* heavyBody = HitBody(heavy);
    ASSERT_NE(lightBody, nullptr);
    ASSERT_NE(heavyBody, nullptr);
    EXPECT_LT(HorizontalSpeed(heavyBody->Velocity()), HorizontalSpeed(lightBody->Velocity()));
}

TEST(CollisionImpact, FasterImpactLaunchesFarther)
{
    SceneNs::Scene normalScene;
    Rig normal = Build(normalScene, Vector3{}, 1, 0, true);
    SetInstantImpact(normal);
    normal.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});
    Step(normalScene);

    SceneNs::Scene maxScene;
    Rig maxDash = Build(maxScene, Vector3{}, 1, 0, true);
    SetInstantImpact(maxDash);
    maxDash.movement->SetVelocity(Vector3{k_MaxDashSpeed, 0.0f, 0.0f});
    Step(maxScene);

    LevelNs::LaunchedBodyComponent* normalBody = HitBody(normal);
    LevelNs::LaunchedBodyComponent* maxBody = HitBody(maxDash);
    ASSERT_NE(normalBody, nullptr);
    ASSERT_NE(maxBody, nullptr);
    EXPECT_GT(HorizontalSpeed(maxBody->Velocity()), HorizontalSpeed(normalBody->Velocity()));
}

TEST(CollisionImpact, LaunchFieldsDriveLaunchVelocity)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    SetInstantImpact(rig);
    SetFloatField(*rig.impact, "押し飛ばし基準初速", 20.0f);
    SetFloatField(*rig.impact, "押し飛ばしの浮き上がり", 0.5f);
    ASSERT_NE(rig.breakable, nullptr);
    rig.breakable->SetMass(2.0f);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);

    LevelNs::LaunchedBodyComponent* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_FLOAT_EQ(HorizontalSpeed(body->Velocity()), 10.0f);
    EXPECT_FLOAT_EQ(body->Velocity().y, 5.0f);
}

// 質量の下限 0.01 で割ると 100 倍になる。頭打ちが無いと画面の外へ消える
TEST(CollisionImpact, TinyMassCannotBlowLaunchSpeedUp)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    SetInstantImpact(rig);
    ASSERT_NE(rig.breakable, nullptr);
    rig.breakable->SetMass(0.0f);
    ASSERT_FLOAT_EQ(rig.breakable->Mass(), 0.01f);
    rig.movement->SetVelocity(Vector3{k_MaxDashSpeed, 0.0f, 0.0f});

    Step(scene);

    LevelNs::LaunchedBodyComponent* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_FLOAT_EQ(HorizontalSpeed(body->Velocity()), k_LaunchSpeedCap);
}

// 衝突の瞬間に自機が数歩止まる。止まっている間は反発も発射も適用されず、明けた歩にまとめて掛かる
TEST(CollisionImpact, HitStopFreezesPlayerAndDefersLaunch)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    ASSERT_NE(rig.breakable, nullptr);
    rig.breakable->SetMass(4.0f);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);

    // 検知の歩は移動を止めない。最後の 1 歩で自機が岩へ触れてから凍る
    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_TRUE(rig.movement->IsActiveSelf());
    EXPECT_EQ(HitBody(rig), nullptr);

    Step(scene);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    EXPECT_EQ(HitBody(rig), nullptr);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, k_RunSpeed);

    for (int i = 0; i < 3; ++i)
        Step(scene);
    EXPECT_FALSE(rig.movement->IsActiveSelf());
    EXPECT_EQ(HitBody(rig), nullptr);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, k_RunSpeed);

    const int steps = StepsUntilMovementActive(scene, rig, 60);
    EXPECT_LT(steps, 60);
    EXPECT_LT(rig.movement->Velocity().x, 0.0f);
    LevelNs::LaunchedBodyComponent* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(body->IsFlying());
}

// 重さは飛距離より止められた時間で出る。重い物ほど長く止まる
TEST(CollisionImpact, HeavierTargetStopsLonger)
{
    SceneNs::Scene lightScene;
    Rig light = Build(lightScene, Vector3{}, 1, 0, true);
    ASSERT_NE(light.breakable, nullptr);
    light.breakable->SetMass(1.0f);
    light.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});
    Step(lightScene);
    Step(lightScene);
    ASSERT_FALSE(light.movement->IsActiveSelf());
    const int lightSteps = StepsUntilMovementActive(lightScene, light, 60);

    SceneNs::Scene heavyScene;
    Rig heavy = Build(heavyScene, Vector3{}, 1, 0, true);
    ASSERT_NE(heavy.breakable, nullptr);
    heavy.breakable->SetMass(8.0f);
    heavy.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});
    Step(heavyScene);
    Step(heavyScene);
    ASSERT_FALSE(heavy.movement->IsActiveSelf());
    const int heavySteps = StepsUntilMovementActive(heavyScene, heavy, 60);

    EXPECT_GT(lightSteps, 0);
    EXPECT_LT(lightSteps, 60);
    EXPECT_LT(lightSteps, heavySteps);
}

TEST(CollisionImpact, FasterImpactStopsLonger)
{
    SceneNs::Scene normalScene;
    Rig normal = Build(normalScene, Vector3{}, 1, 0, true);
    normal.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});
    Step(normalScene);
    Step(normalScene);
    ASSERT_FALSE(normal.movement->IsActiveSelf());
    const int normalSteps = StepsUntilMovementActive(normalScene, normal, 60);

    SceneNs::Scene maxScene;
    Rig maxDash = Build(maxScene, Vector3{}, 1, 0, true);
    maxDash.movement->SetVelocity(Vector3{k_MaxDashSpeed, 0.0f, 0.0f});
    Step(maxScene);
    Step(maxScene);
    ASSERT_FALSE(maxDash.movement->IsActiveSelf());
    const int maxSteps = StepsUntilMovementActive(maxScene, maxDash, 60);

    EXPECT_GT(normalSteps, 0);
    EXPECT_LT(normalSteps, maxSteps);
}

// 猶予は明けた歩から数え始める。止まっている間に数えると、操作できないまま猶予が減る
TEST(CollisionImpact, HitStopDefersGraceUntilRelease)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    ASSERT_NE(rig.momentum, nullptr);
    ASSERT_NE(rig.breakable, nullptr);
    rig.breakable->SetMass(4.0f);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);
    ASSERT_TRUE(rig.impact->DidRebound());
    Step(scene);
    ASSERT_FALSE(rig.movement->IsActiveSelf());

    for (int i = 0; i < 4; ++i)
        Step(scene);
    EXPECT_FLOAT_EQ(rig.momentum->GraceSeconds(), 0.0f);

    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);
    EXPECT_FLOAT_EQ(rig.momentum->GraceSeconds(), 0.0f);

    Step(scene);
    EXPECT_FLOAT_EQ(rig.momentum->GraceSeconds(), k_FixedDt);
}

// 基準は秒で指定し、内部で歩数へ換算して凍結の長さを決める
TEST(CollisionImpact, HitStopBaseSecondsDrivesFreezeLength)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    SetFloatField(*rig.impact, "ヒットストップ基準秒", 8.0f / 60.0f);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);
    ASSERT_TRUE(rig.impact->DidRebound());
    Step(scene);
    ASSERT_FALSE(rig.movement->IsActiveSelf());

    // 質量 1 × 勢いの比 1 なので、秒 ÷ 固定ステップ = 8 歩ちょうど止まる
    EXPECT_EQ(StepsUntilMovementActive(scene, rig, 60), 8);
}

// 凍結中は自機が進行方向へ潰れる。反発の前半を潰れで見せる
TEST(CollisionImpact, FreezeSquashesPlayerShape)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    ASSERT_NE(rig.breakable, nullptr);
    rig.breakable->SetMass(4.0f);
    const Vector3 authored = rig.movement->Owner()->Root().Scale();
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);
    // 検知の歩はまだ元の形のまま
    const Vector3 detected = rig.movement->Owner()->Root().Scale();
    EXPECT_FLOAT_EQ(detected.x, authored.x);
    EXPECT_FLOAT_EQ(detected.y, authored.y);

    Step(scene);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    const Vector3 squashed = rig.movement->Owner()->Root().Scale();
    EXPECT_LT(squashed.x, authored.x);
    EXPECT_GT(squashed.y, authored.y);
    EXPECT_FLOAT_EQ(squashed.z, authored.z);
}

// 解放の歩に弾かれる方向へ伸びた形で飛び出し、数歩で配置で決めた元の形へ厳密に戻る
TEST(CollisionImpact, ReleaseStretchesThenRestoresScaleExactly)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    ASSERT_NE(rig.breakable, nullptr);
    rig.breakable->SetMass(4.0f);
    const Vector3 authored = rig.movement->Owner()->Root().Scale();
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);
    Step(scene);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);

    // 解放の歩は伸びた形。弾かれる軸は進行と同じ x で、高さは元に戻っている
    const Vector3 stretched = rig.movement->Owner()->Root().Scale();
    EXPECT_GT(stretched.x, authored.x);
    EXPECT_FLOAT_EQ(stretched.y, authored.y);
    EXPECT_FLOAT_EQ(stretched.z, authored.z);

    for (int i = 0; i < 10; ++i)
        Step(scene);

    const Vector3 restored = rig.movement->Owner()->Root().Scale();
    EXPECT_FLOAT_EQ(restored.x, authored.x);
    EXPECT_FLOAT_EQ(restored.y, authored.y);
    EXPECT_FLOAT_EQ(restored.z, authored.z);
}

// 潰れは絵だけ。凍結中も当たり判定と位置は変わらない
TEST(CollisionImpact, SquashLeavesPositionAndPhysicsAlone)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    ASSERT_NE(rig.breakable, nullptr);
    rig.breakable->SetMass(4.0f);
    const std::size_t aabbs = scene.Physics().Aabbs().size();
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);
    Step(scene);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    const Vector3 frozenPos = rig.movement->Owner()->Root().Position();

    Step(scene);
    const Vector3 stillPos = rig.movement->Owner()->Root().Position();
    EXPECT_FLOAT_EQ(stillPos.x, frozenPos.x);
    EXPECT_FLOAT_EQ(stillPos.y, frozenPos.y);
    EXPECT_FLOAT_EQ(stillPos.z, frozenPos.z);
    EXPECT_EQ(scene.Physics().Aabbs().size(), aabbs);
}

// 検知の歩では移動が最後の 1 歩を走り、次の歩で凍る。自機が岩へ押し付けられた構図で止まる
TEST(CollisionImpact, FreezeWaitsOneStepAfterDetection)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    ASSERT_NE(rig.breakable, nullptr);
    rig.breakable->SetMass(4.0f);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);
    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_TRUE(rig.movement->IsActiveSelf());

    Step(scene);
    EXPECT_FALSE(rig.movement->IsActiveSelf());
}

// 待ちの 1 歩と凍結中に同じ衝突を二重に検知しない
TEST(CollisionImpact, DetectsOnlyOncePerImpact)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    ASSERT_NE(rig.breakable, nullptr);
    rig.breakable->SetMass(4.0f);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    int detections = 0;
    for (int i = 0; i < 20; ++i)
    {
        Step(scene);
        if (rig.impact->DidRebound())
            ++detections;
    }

    EXPECT_EQ(detections, 1);
    EXPECT_TRUE(rig.movement->IsActiveSelf());
    LevelNs::LaunchedBodyComponent* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(body->IsFlying());
}

// 凍結が始まる歩で岩が発射方向へ食い込む。当たりは動かさない
TEST(CollisionImpact, HitStopPushesRockWhenFreezeBegins)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    ASSERT_NE(rig.breakable, nullptr);
    rig.breakable->SetMass(4.0f);
    const Vector3 home = rig.targetBox->Owner()->Root().Position();
    const std::size_t aabbs = scene.Physics().Aabbs().size();
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);
    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_FLOAT_EQ(rig.targetBox->Owner()->Root().Position().x, home.x);

    Step(scene);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    const Vector3 pushed = rig.targetBox->Owner()->Root().Position();
    EXPECT_GT(pushed.x, home.x + 1.0e-4f);
    EXPECT_FLOAT_EQ(pushed.y, home.y);
    EXPECT_FLOAT_EQ(pushed.z, home.z);
    EXPECT_EQ(scene.Physics().Aabbs().size(), aabbs);
    EXPECT_TRUE(rig.targetBox->IsActiveSelf());
}

// 凍結中は歩ごとに岩が発射軸に沿って往復する
TEST(CollisionImpact, RockVibratesWhileFrozen)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    ASSERT_NE(rig.breakable, nullptr);
    rig.breakable->SetMass(4.0f);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);
    Step(scene);
    ASSERT_FALSE(rig.movement->IsActiveSelf());

    Step(scene);
    const float x1 = rig.targetBox->Owner()->Root().Position().x;
    Step(scene);
    const float x2 = rig.targetBox->Owner()->Root().Position().x;
    ASSERT_FALSE(rig.movement->IsActiveSelf());

    EXPECT_GT(std::abs(x2 - x1), 1.0e-4f);
}

// 重い物は揺れない。振幅の差が質量の表現になる
TEST(CollisionImpact, HeavierRockVibratesLess)
{
    SceneNs::Scene lightScene;
    Rig light = Build(lightScene, Vector3{}, 1, 0, true);
    ASSERT_NE(light.breakable, nullptr);
    light.breakable->SetMass(0.5f);
    light.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});
    Step(lightScene);
    Step(lightScene);
    ASSERT_FALSE(light.movement->IsActiveSelf());
    float lightMin = light.targetBox->Owner()->Root().Position().x;
    float lightMax = lightMin;
    for (int i = 0; i < 30; ++i)
    {
        Step(lightScene);
        if (light.movement->IsActiveSelf())
            break;
        const float x = light.targetBox->Owner()->Root().Position().x;
        lightMin = std::min(lightMin, x);
        lightMax = std::max(lightMax, x);
    }

    SceneNs::Scene heavyScene;
    Rig heavy = Build(heavyScene, Vector3{}, 1, 0, true);
    ASSERT_NE(heavy.breakable, nullptr);
    heavy.breakable->SetMass(8.0f);
    heavy.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});
    Step(heavyScene);
    Step(heavyScene);
    ASSERT_FALSE(heavy.movement->IsActiveSelf());
    float heavyMin = heavy.targetBox->Owner()->Root().Position().x;
    float heavyMax = heavyMin;
    for (int i = 0; i < 30; ++i)
    {
        Step(heavyScene);
        if (heavy.movement->IsActiveSelf())
            break;
        const float x = heavy.targetBox->Owner()->Root().Position().x;
        heavyMin = std::min(heavyMin, x);
        heavyMax = std::max(heavyMax, x);
    }

    EXPECT_GT(lightMax - lightMin, 1.0e-4f);
    EXPECT_LT(heavyMax - heavyMin, lightMax - lightMin);
}

// 食い込みも振動も絵だけ。明けた歩に元位置へ厳密に戻してから発射する
TEST(CollisionImpact, ReleaseRestoresRockExactlyBeforeLaunch)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    ASSERT_NE(rig.breakable, nullptr);
    rig.breakable->SetMass(4.0f);
    const Vector3 home = rig.targetBox->Owner()->Root().Position();
    const std::size_t aabbs = scene.Physics().Aabbs().size();
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);
    Step(scene);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);

    const Vector3 restored = rig.targetBox->Owner()->Root().Position();
    EXPECT_FLOAT_EQ(restored.x, home.x);
    EXPECT_FLOAT_EQ(restored.y, home.y);
    EXPECT_FLOAT_EQ(restored.z, home.z);
    LevelNs::LaunchedBodyComponent* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(body->IsFlying());
    EXPECT_EQ(scene.Physics().Aabbs().size(), aabbs - 1);
}

// 凍結中だけカメラが揺れる。ImpactResolverComponent がシーンの CameraBrain へ揺れを渡す
TEST(CollisionImpact, HitStopShakesCamera)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    ASSERT_NE(rig.breakable, nullptr);
    rig.breakable->SetMass(4.0f);

    SceneNs::CameraBrainComponent* brain = scene.CameraBrain();
    ASSERT_NE(brain, nullptr);
    auto* placed = brain->Owner()->AddComponent<SceneNs::PlacedVirtualCamera>();
    // 据え置きカメラは進入まで非 active が既定。検証台では手で起こす
    placed->SetActive(true);
    placed->SetView(Vector3{0.0f, 3.0f, -6.0f}, Vector3{0.0f, 1.0f, 0.0f});
    brain->AddVirtualCamera(placed);
    brain->Evaluate(1.0f);
    const Vector3 before = brain->LastPose().position;

    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});
    Step(scene);
    ASSERT_TRUE(rig.impact->DidRebound());
    brain->Evaluate(1.0f);
    EXPECT_FLOAT_EQ(brain->LastPose().position.y, before.y);

    Step(scene);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    brain->Evaluate(1.0f);
    const Vector3 during = brain->LastPose().position;
    EXPECT_GT(std::abs(during.y - before.y), 1.0e-4f);
}

TEST(LaunchedBody, LaunchSleepsColliderAndDropsItFromPhysics)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    ASSERT_NE(rig.box, nullptr);
    ASSERT_TRUE(rig.box->IsActiveSelf());

    rig.body->Launch(Vector3{10.0f, 4.0f, 0.0f});

    EXPECT_TRUE(rig.body->IsFlying());
    EXPECT_FALSE(rig.box->IsActiveSelf());
    EXPECT_EQ(scene.Physics().Aabbs().size(), rig.restingAabbs - 1);
}

TEST(LaunchedBody, AdvancesHorizontallyByVelocityPerStep)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    const Vector3 start = rig.object->Root().Position();
    rig.body->Launch(Vector3{10.0f, 4.0f, 0.0f});

    StepBody(scene);

    const Vector3 moved = rig.object->Root().Position();
    EXPECT_FLOAT_EQ(moved.x - start.x, 10.0f * k_FixedDt);
    EXPECT_FLOAT_EQ(moved.z, start.z);
}

TEST(LaunchedBody, GravityReducesVerticalSpeedEachStep)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    rig.body->Launch(Vector3{0.0f, 6.0f, 0.0f});

    float expected = 6.0f;
    expected += k_LaunchGravity * k_FixedDt;
    StepBody(scene);
    EXPECT_FLOAT_EQ(rig.body->Velocity().y, expected);

    expected += k_LaunchGravity * k_FixedDt;
    StepBody(scene);
    EXPECT_FLOAT_EQ(rig.body->Velocity().y, expected);
    EXPECT_TRUE(rig.body->IsFlying());
}

TEST(LaunchedBody, LandsOnFloorTopAndZeroesVerticalSpeed)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    rig.body->Launch(Vector3{4.0f, 4.0f, 0.0f});
    ASSERT_TRUE(rig.body->IsFlying());

    const int steps = RunUntilRest(scene, *rig.body, k_RestStepLimit);

    EXPECT_GT(steps, 20);
    EXPECT_LT(steps, k_RestStepLimit);
    EXPECT_NEAR(rig.object->Root().Position().y, k_BodyRestY, 1.0e-4f);
    EXPECT_FLOAT_EQ(rig.body->Velocity().y, 0.0f);
}

TEST(LaunchedBody, GroundFrictionSlowsHorizontalSpeed)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    rig.body->Launch(Vector3{4.0f, 0.0f, 0.0f});

    StepBody(scene);
    const float first = rig.body->Velocity().x;
    StepBody(scene);
    const float second = rig.body->Velocity().x;

    EXPECT_LT(first, 4.0f);
    EXPECT_LT(second, first);
    EXPECT_GT(second, 0.0f);
}

TEST(LaunchedBody, RestWakesColliderBack)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    rig.body->Launch(Vector3{4.0f, 4.0f, 0.0f});
    ASSERT_FALSE(rig.box->IsActiveSelf());

    const int steps = RunUntilRest(scene, *rig.body, k_RestStepLimit);

    EXPECT_LT(steps, k_RestStepLimit);
    EXPECT_FALSE(rig.body->IsFlying());
    EXPECT_TRUE(rig.box->IsActiveSelf());
    EXPECT_EQ(scene.Physics().Aabbs().size(), rig.restingAabbs);
    EXPECT_FLOAT_EQ(rig.body->Velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(rig.body->Velocity().z, 0.0f);
}

TEST(LaunchedBody, RestsAwayFromLaunchPosition)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    const Vector3 start = rig.object->Root().Position();
    rig.body->Launch(Vector3{4.0f, 4.0f, 0.0f});

    const int steps = RunUntilRest(scene, *rig.body, k_RestStepLimit);
    ASSERT_LT(steps, k_RestStepLimit);

    const float travelled = rig.object->Root().Position().x - start.x;
    EXPECT_GT(travelled, 1.5f);
    EXPECT_LT(travelled, 3.0f);
}

TEST(LaunchedBody, IdleStaysPutAndKeepsCollider)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    const Vector3 start = rig.object->Root().Position();

    for (int i = 0; i < 60; ++i)
        StepBody(scene);

    const Vector3 now = rig.object->Root().Position();
    EXPECT_FALSE(rig.body->IsFlying());
    EXPECT_TRUE(rig.box->IsActiveSelf());
    EXPECT_FLOAT_EQ(now.x, start.x);
    EXPECT_FLOAT_EQ(now.y, start.y);
    EXPECT_FLOAT_EQ(now.z, start.z);
    EXPECT_EQ(scene.Physics().Aabbs().size(), rig.restingAabbs);
}

TEST(LaunchedBody, NonFiniteLaunchIsIgnored)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);

    rig.body->Launch(Vector3{std::numeric_limits<float>::quiet_NaN(), 4.0f, 0.0f});

    EXPECT_FALSE(rig.body->IsFlying());
    EXPECT_TRUE(rig.box->IsActiveSelf());
    EXPECT_EQ(scene.Physics().Aabbs().size(), rig.restingAabbs);
}
