#include "Game/Level/BlockObject.h"
#include "Game/Player.h"

#include <Game/Level/BreakableComponent.h>
#include <Game/Level/CollisionInputComponent.h>
#include <Game/Level/ImpactInputJudge.h>
#include <Game/Level/ImpactMarkComponent.h>
#include <Game/Level/ImpactResolverComponent.h>
#include <Game/Level/LaunchedBodyComponent.h>
#include <Game/Level/MomentumComponent.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/BoxColliderComponent.h>
#include <Runtime/Object/Components/CameraBrainComponent.h>
#include <Runtime/Object/Components/CharacterMovementComponent.h>
#include <Runtime/Object/Components/MeshRendererComponent.h>
#include <Runtime/Object/Components/PlacedVirtualCamera.h>
#include <Runtime/Object/Components/PlayerInputComponent.h>
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
#include <vector>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Object;

namespace
{
    using NS::Core::Vector3;

    constexpr float k_FixedDt = 1.0f / 60.0f;
    constexpr float k_ReboundUpSpeed = 3.0f;
    constexpr float k_RunSpeed = 8.0f;
    constexpr float k_DashSpeed = 12.0f;
    constexpr float k_MaxDashSpeed = 16.0f;
    constexpr float k_SlamSpeed = 20.0f;
    constexpr float k_TapSlamSpeed = 10.0f;
    constexpr float k_LaunchSpeedCap = 60.0f;
    constexpr float k_ReboundSpeedCap = 24.0f;
    // 破壊は耐久 ≤ 最終威力なので、反発と押し飛ばしを見る台は壊れない高さを既定にする
    constexpr float k_UnbreakableToughness = 99.0f;

    struct Rig
    {
        SceneNs::CharacterMovementComponent* movement = nullptr;
        LevelNs::MomentumComponent* momentum = nullptr;
        LevelNs::ImpactResolverComponent* impact = nullptr;
        LevelNs::CollisionInputComponent* input = nullptr;
        SceneNs::BoxColliderComponent* targetBox = nullptr;
        LevelNs::BreakableComponent* breakable = nullptr;
    };

    struct SlamCourse
    {
        float start = 0.0f;
        std::int16_t targetCell = 1;
        bool withBreakable = true;
        bool withCollisionInput = true;
        bool floorUnderTarget = true;
        bool alongZ = false;
    };

    Rig BuildSlam(SceneNs::Scene& scene, const SlamCourse& course)
    {
        NS::Core::FrameTimer::SetFixedDelta(k_FixedDt);

        SceneNs::SceneData data;
        Vector3 spawn{course.start, Player::k_DefaultSpawnY, 0.0f};
        if (course.alongZ)
            spawn = Vector3{0.0f, Player::k_DefaultSpawnY, course.start};
        SceneNs::ObjectData player = MakePlayerObject(spawn, NS::Core::Quaternion{});
        player.components.push_back(SceneNs::MakeComponentEntry("MomentumComponent"));
        player.components.push_back(SceneNs::MakeComponentEntry("ImpactResolverComponent"));
        if (course.withCollisionInput)
            player.components.push_back(SceneNs::MakeComponentEntry("CollisionInputComponent"));
        data.objects.push_back(player);

        for (std::int16_t i = -3; i <= static_cast<std::int16_t>(course.targetCell + 3); ++i)
        {
            if (!course.floorUnderTarget && i == course.targetCell)
                continue;
            if (course.alongZ)
                data.objects.push_back(LevelNs::MakeCellObject(0, 0, i));
            else
                data.objects.push_back(LevelNs::MakeCellObject(i, 0, 0));
        }

        SceneNs::ObjectData target = LevelNs::MakeCellObject(course.targetCell, 1, 0);
        if (course.alongZ)
            target = LevelNs::MakeCellObject(0, 1, course.targetCell);
        if (course.withBreakable)
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
            rig.input = live->FindComponent<LevelNs::CollisionInputComponent>();
            // 起こしたままだと実機の入力が毎歩 0 を書き込むため、走行入力と向きが検証台から消える
            if (auto* input = live->FindComponent<SceneNs::PlayerInputComponent>())
                input->SetActive(false);
        }
        scene.World().ForEachComponent<LevelNs::BreakableComponent>(
            [&rig](LevelNs::BreakableComponent& breakable) { rig.breakable = &breakable; });
        if (rig.breakable != nullptr)
        {
            rig.breakable->SetToughness(k_UnbreakableToughness);
            rig.targetBox = rig.breakable->Owner()->FindComponent<SceneNs::BoxColliderComponent>();
        }
        if (rig.targetBox == nullptr)
        {
            scene.World().ForEachComponent<SceneNs::BoxColliderComponent>([&rig](SceneNs::BoxColliderComponent& box) {
                if (box.Owner()->Root().Position().y > 0.9f)
                    rig.targetBox = &box;
            });
        }
        return rig;
    }

    LevelNs::LaunchedBodyComponent* HitBody(const Rig& rig)
    {
        if (rig.targetBox == nullptr)
            return nullptr;
        return rig.targetBox->Owner()->FindComponent<LevelNs::LaunchedBodyComponent>();
    }

    // 帯の範囲は半開なので Update (200) の移動は入らない。押し飛ばされた物と破片を動かさずに済む
    void StepWorld(SceneNs::Scene& scene)
    {
        scene.World().UpdateObjects(SceneNs::TickPriority::EarlyUpdate, SceneNs::TickPriority::Update);
    }

    void Step(SceneNs::Scene& scene, const Rig& rig)
    {
        StepWorld(scene);
        if (rig.movement != nullptr)
            rig.movement->OnUpdate();
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

    // 空中でも出せるが、落下が混ざると当たる歩が揺れる。先に床へ着けて接地からの発動に揃える
    void SettleOnFloor(SceneNs::Scene& scene, const Rig& rig)
    {
        for (int i = 0; i < 30 && !rig.movement->IsGrounded(); ++i)
            Step(scene, rig);
    }

    void BeginSlam(SceneNs::Scene& scene, const Rig& rig, float entrySpeed, float charge01, bool alongZ = false)
    {
        SettleOnFloor(scene, rig);
        float axisSign = 1.0f;
        if (entrySpeed < 0.0f)
            axisSign = -1.0f;
        Vector3 velocity{entrySpeed, 0.0f, 0.0f};
        Vector3 aim{axisSign, 0.0f, 0.0f};
        if (alongZ)
        {
            velocity = Vector3{0.0f, 0.0f, entrySpeed};
            aim = Vector3{0.0f, 0.0f, axisSign};
        }
        rig.movement->SetVelocity(velocity);
        // 向きの解決は入力が最優先。入力なしだとカメラの前へ逸れるので、走りながら押した状況を入力で再現する
        rig.movement->SetDesiredMove(aim, 0.0f);
        rig.movement->RequestBodySlam(charge01);
        Step(scene, rig);
    }

    // 裁定が書いた速度をそのまま読むため、裁定が起きた歩は移動を走らせずに返す
    int StepUntilImpact(SceneNs::Scene& scene, const Rig& rig, int maxSteps)
    {
        for (int i = 0; i < maxSteps; ++i)
        {
            StepWorld(scene);
            if (rig.impact->DidRebound() || rig.impact->DidBreak())
                return i + 1;
            rig.movement->OnUpdate();
        }
        return maxSteps;
    }

    // 解放が書いた速度をそのまま読むため、移動が起きた歩は移動を走らせずに返す
    int StepsUntilMovementActive(SceneNs::Scene& scene, const Rig& rig, int maxSteps)
    {
        for (int i = 0; i < maxSteps; ++i)
        {
            StepWorld(scene);
            if (rig.movement->IsActiveSelf())
                return i + 1;
            rig.movement->OnUpdate();
        }
        return maxSteps;
    }

    // 突進の中ほどで当たる並び。ここでしか突進位置係数がピークしきい値を超えない
    constexpr SlamCourse k_PeakCourse{.start = -0.5f, .targetCell = 4};
    constexpr SlamCourse k_NearCourse{.start = 0.0f, .targetCell = 1};

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

    // リフレクションの int 欄へ値を入れる
    void SetIntField(SceneNs::Component& comp, std::string_view label, int value)
    {
        const SceneNs::FieldDesc* field = SceneNs::FindField(comp.GetReflection(), label);
        ASSERT_NE(field, nullptr);
        field->set(&comp, &value);
    }

    // 一時オブジェクトとして湧いた破片だけ集める。押し飛ばされた配置物は数えない
    std::vector<LevelNs::LaunchedBodyComponent*> DebrisBodies(SceneNs::Scene& scene)
    {
        std::vector<LevelNs::LaunchedBodyComponent*> out;
        scene.World().ForEachComponent<LevelNs::LaunchedBodyComponent>([&out](LevelNs::LaunchedBodyComponent& body) {
            if (body.Owner()->IsTransient())
                out.push_back(&body);
        });
        return out;
    }

    int MarkCount(SceneNs::Scene& scene)
    {
        int count = 0;
        scene.World().ForEachComponent<LevelNs::ImpactMarkComponent>(
            [&count](LevelNs::ImpactMarkComponent&) { ++count; });
        return count;
    }
} // namespace

TEST(CollisionImpact, LightContactDoesNothing)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    ASSERT_NE(rig.movement, nullptr);
    SettleOnFloor(scene, rig);

    for (int i = 0; i < 20; ++i)
    {
        rig.movement->SetVelocity(Vector3{k_MaxDashSpeed, 0.0f, 0.0f});
        Step(scene, rig);
        ASSERT_FALSE(rig.impact->DidRebound());
        ASSERT_FALSE(rig.impact->DidBreak());
    }

    EXPECT_EQ(HitBody(rig), nullptr);
    EXPECT_TRUE(rig.breakable->IsActiveSelf());
    EXPECT_TRUE(rig.movement->IsActiveSelf());
}

TEST(CollisionImpact, ReboundsAwayFromApproachedBox)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);
    ASSERT_TRUE(rig.movement->IsBodySlamming());

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    EXPECT_TRUE(rig.impact->DidRebound());
    EXPECT_LT(rig.movement->Velocity().x, 0.0f);
    EXPECT_GT(rig.movement->Velocity().y, 0.0f);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().z, 0.0f);
    EXPECT_FALSE(rig.movement->IsBodySlamming());
}

// 入りが速いほど返りも速い。速く行くほど損になると、勢いを作る意味が消える
TEST(CollisionImpact, FasterImpactReboundsFaster)
{
    SceneNs::Scene normalScene;
    Rig normal = BuildSlam(normalScene, k_NearCourse);
    SetInstantImpact(normal);
    BeginSlam(normalScene, normal, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(normalScene, normal, 30), 30);

    SceneNs::Scene maxScene;
    Rig maxDash = BuildSlam(maxScene, k_NearCourse);
    SetInstantImpact(maxDash);
    BeginSlam(maxScene, maxDash, k_MaxDashSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(maxScene, maxDash, 30), 30);

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
    Rig light = BuildSlam(lightScene, k_NearCourse);
    SetInstantImpact(light);
    light.breakable->SetMass(1.0f);
    BeginSlam(lightScene, light, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(lightScene, light, 30), 30);

    SceneNs::Scene heavyScene;
    Rig heavy = BuildSlam(heavyScene, k_NearCourse);
    SetInstantImpact(heavy);
    heavy.breakable->SetMass(8.0f);
    BeginSlam(heavyScene, heavy, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(heavyScene, heavy, 30), 30);

    const float weak = HorizontalSpeed(light.movement->Velocity());
    const float strong = HorizontalSpeed(heavy.movement->Velocity());
    EXPECT_GT(weak, 0.0f);
    EXPECT_GT(strong, weak);
}

TEST(CollisionImpact, ReboundSpeedIsCapped)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    SetFloatField(*rig.impact, "反発基準初速", 100.0f);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_FLOAT_EQ(HorizontalSpeed(rig.movement->Velocity()), k_ReboundSpeedCap);
}

TEST(CollisionImpact, ReboundAddsUpSpeed)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    EXPECT_FLOAT_EQ(rig.movement->Velocity().y, k_ReboundUpSpeed);
}

TEST(CollisionImpact, ReboundStartsMomentumGrace)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    ASSERT_NE(rig.momentum, nullptr);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_FLOAT_EQ(rig.momentum->GraceSeconds(), 0.0f);

    Step(scene, rig);

    EXPECT_FLOAT_EQ(rig.momentum->GraceSeconds(), k_FixedDt);
}

TEST(CollisionImpact, ReboundDirectionFollowsBoxAxis)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.targetCell = 1, .alongZ = true});
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f, true);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    EXPECT_TRUE(rig.impact->DidRebound());
    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, 0.0f);
    EXPECT_LT(rig.movement->Velocity().z, 0.0f);
}

TEST(CollisionImpact, ReboundsFromDeepOverlapWhenApproaching)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.start = 0.55f, .targetCell = 1});
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    EXPECT_TRUE(rig.impact->DidRebound());
    EXPECT_LT(rig.movement->Velocity().x, 0.0f);
}

TEST(CollisionImpact, DoesNotApplyWhileSeparating)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.start = 0.1f, .targetCell = 1});
    SetInstantImpact(rig);
    BeginSlam(scene, rig, -k_RunSpeed, 0.0f);

    for (int i = 0; i < 20; ++i)
    {
        Step(scene, rig);
        ASSERT_FALSE(rig.impact->DidRebound());
        ASSERT_FALSE(rig.impact->DidBreak());
    }
    EXPECT_EQ(HitBody(rig), nullptr);
}

TEST(CollisionImpact, NoReboundWithoutBreakableMark)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.targetCell = 1, .withBreakable = false});
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    EXPECT_EQ(StepUntilImpact(scene, rig, 30), 30);
    EXPECT_FALSE(rig.impact->DidRebound());
}

// 世界を総当たりで回るので、重なりを見ないと離れた所に置いた物にも反発する
TEST(CollisionImpact, NoReboundAgainstDistantBox)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.targetCell = 12});
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    EXPECT_EQ(StepUntilImpact(scene, rig, 30), 30);
    EXPECT_FALSE(rig.impact->DidRebound());
}

TEST(CollisionImpact, NoReboundAgainstTriggerBox)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    ASSERT_NE(rig.targetBox, nullptr);
    rig.targetBox->SetTrigger(true);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    EXPECT_EQ(StepUntilImpact(scene, rig, 30), 30);
    EXPECT_FALSE(rig.impact->DidRebound());
}

TEST(CollisionImpact, ReboundFieldsDriveVelocity)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    SetFloatField(*rig.impact, "反発基準初速", 5.0f);
    SetFloatField(*rig.impact, "反発の上向き初速", 1.0f);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, -5.0f * rig.impact->LastPower() * 0.5f);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().y, 1.0f);
}

TEST(CollisionImpact, WithoutCollisionInputPowerIsRatioOnly)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.targetCell = 1, .withCollisionInput = false});
    ASSERT_EQ(rig.input, nullptr);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_FLOAT_EQ(rig.impact->LastPower(), 1.0f);
    EXPECT_FALSE(rig.impact->WasPeakImpact());
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
    Rig rig = BuildSlam(scene, k_NearCourse);
    ASSERT_EQ(HitBody(rig), nullptr);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    ASSERT_TRUE(rig.impact->DidRebound());
    LevelNs::LaunchedBodyComponent* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(body->IsFlying());
}

TEST(CollisionImpact, LaunchDirectionFollowsApproach)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    LevelNs::LaunchedBodyComponent* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_GT(body->Velocity().x, 0.0f);
    EXPECT_NEAR(body->Velocity().z, 0.0f, 1.0e-4f);
}

TEST(CollisionImpact, LaunchLiftsHitBody)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    LevelNs::LaunchedBodyComponent* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_GT(body->Velocity().y, 0.0f);
    EXPECT_LT(body->Velocity().y, HorizontalSpeed(body->Velocity()));
}

TEST(CollisionImpact, HeavierBodyLaunchesSlower)
{
    SceneNs::Scene lightScene;
    Rig light = BuildSlam(lightScene, k_NearCourse);
    SetInstantImpact(light);
    light.breakable->SetMass(1.0f);
    BeginSlam(lightScene, light, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(lightScene, light, 30), 30);

    SceneNs::Scene heavyScene;
    Rig heavy = BuildSlam(heavyScene, k_NearCourse);
    SetInstantImpact(heavy);
    heavy.breakable->SetMass(4.0f);
    BeginSlam(heavyScene, heavy, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(heavyScene, heavy, 30), 30);

    LevelNs::LaunchedBodyComponent* lightBody = HitBody(light);
    LevelNs::LaunchedBodyComponent* heavyBody = HitBody(heavy);
    ASSERT_NE(lightBody, nullptr);
    ASSERT_NE(heavyBody, nullptr);
    EXPECT_LT(HorizontalSpeed(heavyBody->Velocity()), HorizontalSpeed(lightBody->Velocity()));
}

TEST(CollisionImpact, FasterImpactLaunchesFarther)
{
    SceneNs::Scene normalScene;
    Rig normal = BuildSlam(normalScene, k_NearCourse);
    SetInstantImpact(normal);
    BeginSlam(normalScene, normal, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(normalScene, normal, 30), 30);

    SceneNs::Scene maxScene;
    Rig maxDash = BuildSlam(maxScene, k_NearCourse);
    SetInstantImpact(maxDash);
    BeginSlam(maxScene, maxDash, k_MaxDashSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(maxScene, maxDash, 30), 30);

    LevelNs::LaunchedBodyComponent* normalBody = HitBody(normal);
    LevelNs::LaunchedBodyComponent* maxBody = HitBody(maxDash);
    ASSERT_NE(normalBody, nullptr);
    ASSERT_NE(maxBody, nullptr);
    EXPECT_GT(HorizontalSpeed(maxBody->Velocity()), HorizontalSpeed(normalBody->Velocity()));
}

TEST(CollisionImpact, LaunchFieldsDriveLaunchVelocity)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    SetFloatField(*rig.impact, "押し飛ばし基準初速", 20.0f);
    SetFloatField(*rig.impact, "押し飛ばしの浮き上がり", 0.5f);
    rig.breakable->SetMass(2.0f);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    LevelNs::LaunchedBodyComponent* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    const float expected = 20.0f * rig.impact->LastPower() / 2.0f;
    EXPECT_FLOAT_EQ(HorizontalSpeed(body->Velocity()), expected);
    EXPECT_FLOAT_EQ(body->Velocity().y, expected * 0.5f);
}

// 質量の下限 0.01 で割ると 100 倍になる。頭打ちが無いと画面の外へ消える
TEST(CollisionImpact, TinyMassCannotBlowLaunchSpeedUp)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    rig.breakable->SetMass(0.0f);
    ASSERT_FLOAT_EQ(rig.breakable->Mass(), 0.01f);
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    LevelNs::LaunchedBodyComponent* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_FLOAT_EQ(HorizontalSpeed(body->Velocity()), k_LaunchSpeedCap);
}

// 衝突の瞬間に自機が数歩止まる。止まっている間は反発も発射も適用されず、明けた歩にまとめて掛かる
TEST(CollisionImpact, HitStopFreezesPlayerAndDefersLaunch)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.breakable->SetMass(4.0f);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    // 検知の歩は移動を止めない。最後の 1 歩で自機が岩へ触れてから凍る
    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_TRUE(rig.movement->IsActiveSelf());
    EXPECT_EQ(HitBody(rig), nullptr);

    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    EXPECT_EQ(HitBody(rig), nullptr);
    EXPECT_GE(rig.movement->Velocity().x, 0.0f);

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
    Rig light = BuildSlam(lightScene, k_NearCourse);
    light.breakable->SetMass(1.0f);
    BeginSlam(lightScene, light, k_MaxDashSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(lightScene, light, 30), 30);
    Step(lightScene, light);
    ASSERT_FALSE(light.movement->IsActiveSelf());
    const int lightSteps = StepsUntilMovementActive(lightScene, light, 60);

    SceneNs::Scene heavyScene;
    Rig heavy = BuildSlam(heavyScene, k_NearCourse);
    heavy.breakable->SetMass(8.0f);
    BeginSlam(heavyScene, heavy, k_MaxDashSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(heavyScene, heavy, 30), 30);
    Step(heavyScene, heavy);
    ASSERT_FALSE(heavy.movement->IsActiveSelf());
    const int heavySteps = StepsUntilMovementActive(heavyScene, heavy, 60);

    EXPECT_GT(lightSteps, 0);
    EXPECT_LT(lightSteps, 60);
    EXPECT_LT(lightSteps, heavySteps);
}

TEST(CollisionImpact, FasterImpactStopsLonger)
{
    SceneNs::Scene normalScene;
    Rig normal = BuildSlam(normalScene, k_NearCourse);
    BeginSlam(normalScene, normal, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(normalScene, normal, 30), 30);
    Step(normalScene, normal);
    ASSERT_FALSE(normal.movement->IsActiveSelf());
    const int normalSteps = StepsUntilMovementActive(normalScene, normal, 60);

    SceneNs::Scene maxScene;
    Rig maxDash = BuildSlam(maxScene, k_NearCourse);
    BeginSlam(maxScene, maxDash, k_MaxDashSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(maxScene, maxDash, 30), 30);
    Step(maxScene, maxDash);
    ASSERT_FALSE(maxDash.movement->IsActiveSelf());
    const int maxSteps = StepsUntilMovementActive(maxScene, maxDash, 60);

    EXPECT_GT(normalSteps, 0);
    EXPECT_LT(normalSteps, maxSteps);
}

// 猶予は明けた歩から数え始める。止まっている間に数えると、操作できないまま猶予が減る
TEST(CollisionImpact, HitStopDefersGraceUntilRelease)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    ASSERT_NE(rig.momentum, nullptr);
    rig.breakable->SetMass(4.0f);
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());

    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);
    EXPECT_FLOAT_EQ(rig.momentum->GraceSeconds(), 0.0f);

    Step(scene, rig);
    EXPECT_FLOAT_EQ(rig.momentum->GraceSeconds(), k_FixedDt);
}

// 基準は秒で指定し、内部で歩数へ換算して凍結の長さを決める
TEST(CollisionImpact, HitStopBaseSecondsDrivesFreezeLength)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetFloatField(*rig.impact, "ヒットストップ基準秒", 8.0f / 60.0f);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());

    const int expected = static_cast<int>(std::lround(8.0f * rig.impact->LastPower()));
    EXPECT_EQ(StepsUntilMovementActive(scene, rig, 60), expected);
}

// 凍結中は自機が進行方向へ潰れる。反発の前半を潰れで見せる
TEST(CollisionImpact, FreezeSquashesPlayerShape)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.breakable->SetMass(4.0f);
    const Vector3 authored = rig.movement->Owner()->Root().Scale();
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    const Vector3 detected = rig.movement->Owner()->Root().Scale();
    EXPECT_FLOAT_EQ(detected.x, authored.x);
    EXPECT_FLOAT_EQ(detected.y, authored.y);

    Step(scene, rig);
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
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.breakable->SetMass(4.0f);
    const Vector3 authored = rig.movement->Owner()->Root().Scale();
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);

    const Vector3 stretched = rig.movement->Owner()->Root().Scale();
    EXPECT_GT(stretched.x, authored.x);
    EXPECT_FLOAT_EQ(stretched.y, authored.y);
    EXPECT_FLOAT_EQ(stretched.z, authored.z);

    for (int i = 0; i < 10; ++i)
        Step(scene, rig);

    const Vector3 restored = rig.movement->Owner()->Root().Scale();
    EXPECT_FLOAT_EQ(restored.x, authored.x);
    EXPECT_FLOAT_EQ(restored.y, authored.y);
    EXPECT_FLOAT_EQ(restored.z, authored.z);
}

// 潰れは絵だけ。凍結中も当たり判定と位置は変わらない
TEST(CollisionImpact, SquashLeavesPositionAndPhysicsAlone)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.breakable->SetMass(4.0f);
    const std::size_t aabbs = scene.Physics().Aabbs().size();
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    const Vector3 frozenPos = rig.movement->Owner()->Root().Position();

    Step(scene, rig);
    const Vector3 stillPos = rig.movement->Owner()->Root().Position();
    EXPECT_FLOAT_EQ(stillPos.x, frozenPos.x);
    EXPECT_FLOAT_EQ(stillPos.y, frozenPos.y);
    EXPECT_FLOAT_EQ(stillPos.z, frozenPos.z);
    EXPECT_EQ(scene.Physics().Aabbs().size(), aabbs);
}

// 耐久が最終威力以下なら壊して貫通する。当たりだけ外れて配置物は残る
TEST(CollisionImpact, MaxDashBreaksThroughSoftTarget)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.breakable->SetToughness(1.0f);
    const std::size_t aabbs = scene.Physics().Aabbs().size();
    rig.momentum->SetLevel(LevelNs::MomentumLevel::MaxDash);
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidBreak());
    EXPECT_FALSE(rig.impact->DidRebound());
    EXPECT_TRUE(rig.movement->IsActiveSelf());

    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);

    EXPECT_FALSE(rig.breakable->IsActiveSelf());
    EXPECT_FALSE(rig.targetBox->IsActiveSelf());
    EXPECT_EQ(scene.Physics().Aabbs().size(), aabbs - 1);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, k_TapSlamSpeed * 0.75f);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().z, 0.0f);
}

// 貫通は段を落とさない。壊しながら走り続けるループを守る
TEST(CollisionImpact, BreakKeepsLevel)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.breakable->SetToughness(1.0f);
    rig.momentum->SetLevel(LevelNs::MomentumLevel::MaxDash);
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);
    Step(scene, rig);

    EXPECT_EQ(rig.momentum->Level(), LevelNs::MomentumLevel::MaxDash);
}

TEST(CollisionImpact, MaxDashReboundsOffToughTarget)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.breakable->SetToughness(99.0f);
    rig.momentum->SetLevel(LevelNs::MomentumLevel::MaxDash);
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_FALSE(rig.impact->DidBreak());

    Step(scene, rig);
    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);

    EXPECT_TRUE(rig.breakable->IsActiveSelf());
    EXPECT_LT(rig.movement->Velocity().x, 0.0f);
}

TEST(CollisionImpact, DashHitAtStartCannotBreakToughTwo)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.breakable->SetToughness(2.0f);
    rig.momentum->SetLevel(LevelNs::MomentumLevel::Dash);
    BeginSlam(scene, rig, k_DashSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    EXPECT_TRUE(rig.impact->DidRebound());
    EXPECT_FALSE(rig.impact->DidBreak());
    EXPECT_TRUE(rig.breakable->IsActiveSelf());
}

TEST(CollisionImpact, ChargedPeakBeatsMaxDashPlainHit)
{
    SceneNs::Scene chargedScene;
    Rig charged = BuildSlam(chargedScene, k_PeakCourse);
    charged.breakable->SetToughness(2.0f);
    charged.momentum->SetLevel(LevelNs::MomentumLevel::Dash);
    BeginSlam(chargedScene, charged, k_DashSpeed, 1.0f);
    ASSERT_LT(StepUntilImpact(chargedScene, charged, 30), 30);

    SceneNs::Scene plainScene;
    Rig plain = BuildSlam(plainScene, k_NearCourse);
    plain.breakable->SetToughness(2.0f);
    plain.momentum->SetLevel(LevelNs::MomentumLevel::MaxDash);
    BeginSlam(plainScene, plain, k_MaxDashSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(plainScene, plain, 30), 30);

    EXPECT_TRUE(charged.impact->WasPeakImpact());
    EXPECT_TRUE(charged.impact->DidBreak());
    EXPECT_FALSE(plain.impact->DidBreak());
    EXPECT_TRUE(plain.impact->DidRebound());
    EXPECT_GT(charged.impact->LastPower(), plain.impact->LastPower());
}

TEST(CollisionImpact, PeakFlagFollowsRushPosition)
{
    SceneNs::Scene peakScene;
    Rig peak = BuildSlam(peakScene, k_PeakCourse);
    SetInstantImpact(peak);
    BeginSlam(peakScene, peak, k_RunSpeed, 1.0f);
    ASSERT_LT(StepUntilImpact(peakScene, peak, 30), 30);

    SceneNs::Scene nearScene;
    Rig nearHit = BuildSlam(nearScene, k_NearCourse);
    SetInstantImpact(nearHit);
    BeginSlam(nearScene, nearHit, k_RunSpeed, 1.0f);
    ASSERT_LT(StepUntilImpact(nearScene, nearHit, 30), 30);

    EXPECT_TRUE(peak.impact->WasPeakImpact());
    EXPECT_FALSE(nearHit.impact->WasPeakImpact());
    EXPECT_GT(peak.impact->LastPositionFactor(), nearHit.impact->LastPositionFactor());
    EXPECT_GT(peak.impact->LastPower(), nearHit.impact->LastPower());
}

TEST(CollisionImpact, StoresChargeAndPositionForNextPhase)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_PeakCourse);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    EXPECT_FLOAT_EQ(rig.impact->LastCharge01(), 1.0f);
    EXPECT_GT(rig.impact->LastPositionFactor(), 0.0f);
    EXPECT_FLOAT_EQ(rig.impact->LastPower(), 1.0f * 2.0f * rig.impact->LastPositionFactor());
}

TEST(CollisionImpact, ChargeScalesPowerByCurve)
{
    SceneNs::Scene fullScene;
    Rig full = BuildSlam(fullScene, k_NearCourse);
    SetInstantImpact(full);
    BeginSlam(fullScene, full, k_RunSpeed, 1.0f);
    ASSERT_LT(StepUntilImpact(fullScene, full, 30), 30);

    SceneNs::Scene halfScene;
    Rig half = BuildSlam(halfScene, k_NearCourse);
    SetInstantImpact(half);
    BeginSlam(halfScene, half, k_RunSpeed, 0.5f);
    ASSERT_LT(StepUntilImpact(halfScene, half, 30), 30);

    EXPECT_FLOAT_EQ(full.impact->LastPower(), half.impact->LastPower() * 2.0f / 1.5f);
    LevelNs::LaunchedBodyComponent* fullBody = HitBody(full);
    LevelNs::LaunchedBodyComponent* halfBody = HitBody(half);
    ASSERT_NE(fullBody, nullptr);
    ASSERT_NE(halfBody, nullptr);
    EXPECT_GT(HorizontalSpeed(fullBody->Velocity()), HorizontalSpeed(halfBody->Velocity()));
}

TEST(CollisionImpact, TapImpactIsWeakerThanCharged)
{
    SceneNs::Scene tapScene;
    Rig tap = BuildSlam(tapScene, k_NearCourse);
    SetInstantImpact(tap);
    BeginSlam(tapScene, tap, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(tapScene, tap, 30), 30);

    SceneNs::Scene chargedScene;
    Rig charged = BuildSlam(chargedScene, k_NearCourse);
    SetInstantImpact(charged);
    BeginSlam(chargedScene, charged, k_RunSpeed, 1.0f);
    ASSERT_LT(StepUntilImpact(chargedScene, charged, 30), 30);

    EXPECT_FLOAT_EQ(tap.impact->LastCharge01(), 0.0f);
    EXPECT_LT(tap.impact->LastPower(), charged.impact->LastPower());
}

// 発動時速度 0 で威力が 0 になると、壊せず止めも揺れも出ず衝突が無かったように見える。比の下限を見張る
TEST(CollisionImpact, StandingChargedSlamStillCarriesPower)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    rig.breakable->SetToughness(0.6f);
    SettleOnFloor(scene, rig);
    rig.movement->SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.0f);
    rig.movement->RequestBodySlam(1.0f);
    Step(scene, rig);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    EXPECT_GT(rig.impact->LastPower(), 0.0f);
    EXPECT_TRUE(rig.impact->DidBreak());
}

// 反発後の後ろ滑りなど残った速度が向きに勝つと狙いと食い違う方へ飛ぶ。入力が無い発動はカメラの前へ出す
TEST(CollisionImpact, SlamWithoutInputAimsCameraForward)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SettleOnFloor(scene, rig);
    rig.movement->SetVelocity(Vector3{-2.0f, 0.0f, 0.0f});
    rig.movement->RequestBodySlam(1.0f);
    Step(scene, rig);

    ASSERT_TRUE(rig.movement->IsBodySlamming());
    const Vector3 velocity = rig.movement->BodySlamVelocity();
    EXPECT_GT(velocity.z, 0.0f);
    EXPECT_NEAR(velocity.x, 0.0f, 1e-3f);
}

TEST(CollisionImpact, PeakStretchesHitStop)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_PeakCourse);
    // 既定の基準秒では上限 12 歩で頭打ちになるため、ピーク倍率が歩数に出るまで基準を下げる
    SetFloatField(*rig.impact, "ヒットストップ基準秒", 2.0f / 60.0f);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->WasPeakImpact());
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());

    const int expected = static_cast<int>(std::lround(2.0f * rig.impact->LastPower() * 2.0f));
    EXPECT_EQ(StepsUntilMovementActive(scene, rig, 60), expected);
}

TEST(CollisionImpact, ButtonReleaseStartsBodySlam)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    ASSERT_NE(rig.input, nullptr);
    SettleOnFloor(scene, rig);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    for (int i = 0; i < 15; ++i)
        rig.input->Judge().Step(true);
    ASSERT_TRUE(rig.input->IsCharging());

    Step(scene, rig);

    EXPECT_TRUE(rig.movement->IsBodySlamming());
    EXPECT_GT(rig.movement->BodySlamCharge01(), 0.0f);
}

// 壊した物へもう一度向かっても何も起きない。印が寝ているので探索から外れる
TEST(CollisionImpact, BrokenTargetIsIgnoredAfterwards)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.breakable->SetToughness(1.0f);
    rig.momentum->SetLevel(LevelNs::MomentumLevel::MaxDash);
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);
    for (int i = 0; i < 10; ++i)
        Step(scene, rig);

    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);
    EXPECT_EQ(StepUntilImpact(scene, rig, 30), 30);
    EXPECT_FALSE(rig.impact->DidBreak());
    EXPECT_FALSE(rig.impact->DidRebound());
}

// 貫通は相手を飛ばさない。破片は別の仕組みが出す
TEST(CollisionImpact, BreakDoesNotLaunchTarget)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.breakable->SetToughness(1.0f);
    rig.momentum->SetLevel(LevelNs::MomentumLevel::MaxDash);
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);

    EXPECT_EQ(HitBody(rig), nullptr);
}

// 貫通の止め秒を 0 にすると凍結を挟まず、その歩のうちに壊れて減速する
TEST(CollisionImpact, BreakStopZeroAppliesInstantly)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetFloatField(*rig.impact, "貫通の止め秒", 0.0f);
    rig.breakable->SetToughness(1.0f);
    rig.momentum->SetLevel(LevelNs::MomentumLevel::MaxDash);
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    ASSERT_TRUE(rig.impact->DidBreak());
    EXPECT_TRUE(rig.movement->IsActiveSelf());
    EXPECT_FALSE(rig.targetBox->IsActiveSelf());
    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, k_TapSlamSpeed * 0.75f);
}

// 凍結の途中で裁定が外れても移動は止まったまま残らない
TEST(CollisionImpact, OnEndPlayWakesFrozenMovement)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.momentum->SetLevel(LevelNs::MomentumLevel::MaxDash);
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());

    rig.impact->OnEndPlay();

    EXPECT_TRUE(rig.movement->IsActiveSelf());
}

// 貫通は潰れない。潰れは押し返されている反発だけの絵で、貫通は前へ伸びるだけ
TEST(CollisionImpact, BreakSkipsSquashButStretchesForward)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.breakable->SetToughness(1.0f);
    const Vector3 authored = rig.movement->Owner()->Root().Scale();
    rig.momentum->SetLevel(LevelNs::MomentumLevel::MaxDash);
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    const Vector3 frozen = rig.movement->Owner()->Root().Scale();
    EXPECT_FLOAT_EQ(frozen.x, authored.x);
    EXPECT_FLOAT_EQ(frozen.y, authored.y);
    EXPECT_FLOAT_EQ(frozen.z, authored.z);

    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);
    const Vector3 stretched = rig.movement->Owner()->Root().Scale();
    EXPECT_GT(stretched.x, authored.x);
    EXPECT_FLOAT_EQ(stretched.y, authored.y);

    for (int i = 0; i < 10; ++i)
        Step(scene, rig);
    const Vector3 restored = rig.movement->Owner()->Root().Scale();
    EXPECT_FLOAT_EQ(restored.x, authored.x);
    EXPECT_FLOAT_EQ(restored.y, authored.y);
    EXPECT_FLOAT_EQ(restored.z, authored.z);
}

// 検知の歩では移動が最後の 1 歩を走り、次の歩で凍る。自機が岩へ押し付けられた構図で止まる
TEST(CollisionImpact, FreezeWaitsOneStepAfterDetection)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.breakable->SetMass(4.0f);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_TRUE(rig.movement->IsActiveSelf());

    Step(scene, rig);
    EXPECT_FALSE(rig.movement->IsActiveSelf());
}

// 待ちの 1 歩と凍結中に同じ衝突を二重に検知しない
TEST(CollisionImpact, DetectsOnlyOncePerImpact)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.breakable->SetMass(4.0f);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    int detections = 0;
    for (int i = 0; i < 40; ++i)
    {
        Step(scene, rig);
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
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.breakable->SetMass(4.0f);
    const Vector3 home = rig.targetBox->Owner()->Root().Position();
    const std::size_t aabbs = scene.Physics().Aabbs().size();
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_FLOAT_EQ(rig.targetBox->Owner()->Root().Position().x, home.x);

    Step(scene, rig);
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
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.breakable->SetMass(4.0f);
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());

    Step(scene, rig);
    const float x1 = rig.targetBox->Owner()->Root().Position().x;
    Step(scene, rig);
    const float x2 = rig.targetBox->Owner()->Root().Position().x;
    ASSERT_FALSE(rig.movement->IsActiveSelf());

    EXPECT_GT(std::abs(x2 - x1), 1.0e-4f);
}

// 重い物は揺れない。振幅の差が質量の表現になる
TEST(CollisionImpact, HeavierRockVibratesLess)
{
    SceneNs::Scene lightScene;
    Rig light = BuildSlam(lightScene, k_NearCourse);
    light.breakable->SetMass(0.5f);
    BeginSlam(lightScene, light, k_MaxDashSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(lightScene, light, 30), 30);
    Step(lightScene, light);
    ASSERT_FALSE(light.movement->IsActiveSelf());
    float lightMin = light.targetBox->Owner()->Root().Position().x;
    float lightMax = lightMin;
    for (int i = 0; i < 30; ++i)
    {
        Step(lightScene, light);
        if (light.movement->IsActiveSelf())
            break;
        const float x = light.targetBox->Owner()->Root().Position().x;
        lightMin = std::min(lightMin, x);
        lightMax = std::max(lightMax, x);
    }

    SceneNs::Scene heavyScene;
    Rig heavy = BuildSlam(heavyScene, k_NearCourse);
    heavy.breakable->SetMass(8.0f);
    BeginSlam(heavyScene, heavy, k_MaxDashSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(heavyScene, heavy, 30), 30);
    Step(heavyScene, heavy);
    ASSERT_FALSE(heavy.movement->IsActiveSelf());
    float heavyMin = heavy.targetBox->Owner()->Root().Position().x;
    float heavyMax = heavyMin;
    for (int i = 0; i < 30; ++i)
    {
        Step(heavyScene, heavy);
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
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.breakable->SetMass(4.0f);
    const Vector3 home = rig.targetBox->Owner()->Root().Position();
    const std::size_t aabbs = scene.Physics().Aabbs().size();
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
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
    Rig rig = BuildSlam(scene, k_NearCourse);
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

    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidRebound());
    brain->Evaluate(1.0f);
    EXPECT_FLOAT_EQ(brain->LastPose().position.y, before.y);

    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    brain->Evaluate(1.0f);
    const Vector3 during = brain->LastPose().position;
    EXPECT_GT(std::abs(during.y - before.y), 1.0e-4f);
}

// 壊した瞬間に破片と跡が出る。破片は壊れた物の位置から飛び始める
TEST(CollisionImpact, BreakScattersDebrisAndLeavesMark)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetFloatField(*rig.impact, "貫通の止め秒", 0.0f);
    rig.breakable->SetToughness(1.0f);
    rig.momentum->SetLevel(LevelNs::MomentumLevel::MaxDash);
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);
    const std::size_t before = scene.World().ObjectCount();

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    ASSERT_TRUE(rig.impact->DidBreak());
    EXPECT_EQ(scene.World().ObjectCount(), before + 6);
    EXPECT_EQ(MarkCount(scene), 1);
    const std::vector<LevelNs::LaunchedBodyComponent*> debris = DebrisBodies(scene);
    ASSERT_EQ(debris.size(), 5u);
    const Vector3 home = rig.targetBox->Owner()->Root().Position();
    for (LevelNs::LaunchedBodyComponent* body : debris)
    {
        EXPECT_TRUE(body->IsFlying());
        const Vector3 pos = body->Owner()->Root().Position();
        EXPECT_FLOAT_EQ(pos.x, home.x);
        EXPECT_FLOAT_EQ(pos.y, home.y);
        EXPECT_FLOAT_EQ(pos.z, home.z);
    }
}

// 破片は同じ速さで別の向きへ散る。1 方向に固まると壊れた量が見えない
TEST(CollisionImpact, DebrisScatterDirectionsDifferButShareSpeed)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetFloatField(*rig.impact, "貫通の止め秒", 0.0f);
    rig.breakable->SetToughness(1.0f);
    rig.momentum->SetLevel(LevelNs::MomentumLevel::MaxDash);
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    const std::vector<LevelNs::LaunchedBodyComponent*> debris = DebrisBodies(scene);
    ASSERT_EQ(debris.size(), 5u);
    for (LevelNs::LaunchedBodyComponent* body : debris)
        EXPECT_NEAR(HorizontalSpeed(body->Velocity()), 6.0f, 0.001f);
    const Vector3 first = debris[0]->Velocity();
    const Vector3 second = debris[1]->Velocity();
    EXPECT_GT(std::abs(first.x - second.x) + std::abs(first.z - second.z), 0.1f);
}

// 散り方は決定論。同じ状況で 2 回壊すと同じ向きへ散る
TEST(CollisionImpact, DebrisScatterIsDeterministic)
{
    SceneNs::Scene firstScene;
    Rig first = BuildSlam(firstScene, k_NearCourse);
    SetFloatField(*first.impact, "貫通の止め秒", 0.0f);
    first.breakable->SetToughness(1.0f);
    first.momentum->SetLevel(LevelNs::MomentumLevel::MaxDash);
    BeginSlam(firstScene, first, k_MaxDashSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(firstScene, first, 30), 30);

    SceneNs::Scene secondScene;
    Rig second = BuildSlam(secondScene, k_NearCourse);
    SetFloatField(*second.impact, "貫通の止め秒", 0.0f);
    second.breakable->SetToughness(1.0f);
    second.momentum->SetLevel(LevelNs::MomentumLevel::MaxDash);
    BeginSlam(secondScene, second, k_MaxDashSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(secondScene, second, 30), 30);

    const std::vector<LevelNs::LaunchedBodyComponent*> firstDebris = DebrisBodies(firstScene);
    const std::vector<LevelNs::LaunchedBodyComponent*> secondDebris = DebrisBodies(secondScene);
    ASSERT_EQ(firstDebris.size(), secondDebris.size());
    ASSERT_EQ(firstDebris.size(), 5u);
    for (std::size_t i = 0; i < firstDebris.size(); ++i)
    {
        const Vector3 a = firstDebris[i]->Velocity();
        const Vector3 b = secondDebris[i]->Velocity();
        EXPECT_FLOAT_EQ(a.x, b.x);
        EXPECT_FLOAT_EQ(a.y, b.y);
        EXPECT_FLOAT_EQ(a.z, b.z);
    }
}

// 重い物の破片は飛ばない。破片の飛び方も質量の表示にする
TEST(CollisionImpact, HeavierTargetScattersSlowerDebris)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.breakable->SetMass(4.0f);
    rig.breakable->SetToughness(1.0f);
    SetFloatField(*rig.impact, "貫通の止め秒", 0.0f);
    rig.momentum->SetLevel(LevelNs::MomentumLevel::MaxDash);
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    ASSERT_TRUE(rig.impact->DidBreak());
    const std::vector<LevelNs::LaunchedBodyComponent*> debris = DebrisBodies(scene);
    ASSERT_EQ(debris.size(), 5u);
    for (LevelNs::LaunchedBodyComponent* body : debris)
        EXPECT_NEAR(HorizontalSpeed(body->Velocity()), 1.5f, 0.001f);
}

// 押し飛ばしは跡だけ出す。破片は貫通の絵
TEST(CollisionImpact, LaunchLeavesMarkWithoutDebris)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);
    const std::size_t before = scene.World().ObjectCount();

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_EQ(scene.World().ObjectCount(), before + 1);
    EXPECT_EQ(MarkCount(scene), 1);
    EXPECT_TRUE(DebrisBodies(scene).empty());
}

// 破片の数 0 は破片を出さない指定
TEST(CollisionImpact, ZeroDebrisCountScattersNone)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetIntField(*rig.impact, "破片の数", 0);
    SetFloatField(*rig.impact, "貫通の止め秒", 0.0f);
    rig.breakable->SetToughness(1.0f);
    rig.momentum->SetLevel(LevelNs::MomentumLevel::MaxDash);
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);
    const std::size_t before = scene.World().ObjectCount();

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    ASSERT_TRUE(rig.impact->DidBreak());
    EXPECT_EQ(scene.World().ObjectCount(), before + 1);
    EXPECT_TRUE(DebrisBodies(scene).empty());
    EXPECT_EQ(MarkCount(scene), 1);
}

// 真下に床が無ければ跡を出さない
TEST(CollisionImpact, NoMarkWithoutFloorBelow)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.targetCell = 2, .floorUnderTarget = false});
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);
    const std::size_t before = scene.World().ObjectCount();

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_EQ(scene.World().ObjectCount(), before);
    EXPECT_EQ(MarkCount(scene), 0);
}

// 破片は壊れた物より小さい cube。大きいと壊れた本体と見分けがつかない
TEST(CollisionImpact, DebrisLooksLikeSmallCube)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetFloatField(*rig.impact, "貫通の止め秒", 0.0f);
    rig.breakable->SetToughness(1.0f);
    rig.momentum->SetLevel(LevelNs::MomentumLevel::MaxDash);
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    const std::vector<LevelNs::LaunchedBodyComponent*> debris = DebrisBodies(scene);
    ASSERT_EQ(debris.size(), 5u);
    auto* mesh = debris[0]->Owner()->FindComponent<SceneNs::MeshRendererComponent>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->MeshRef(), "cube");
    const Vector3 scale = debris[0]->Owner()->Root().Scale();
    const Vector3 targetScale = rig.targetBox->Owner()->Root().Scale();
    EXPECT_LT(scale.x, targetScale.x);
    EXPECT_FLOAT_EQ(scale.x, 0.25f);
}

// 破片は転がって止まり、しばらくして描画ごと消える。配置物は破棄しない
TEST(CollisionImpact, DebrisRestsThenExpires)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetFloatField(*rig.impact, "貫通の止め秒", 0.0f);
    SetFloatField(*rig.impact, "破片の初速", 1.0f);
    SetFloatField(*rig.impact, "破片の残る秒", 0.05f);
    rig.breakable->SetToughness(1.0f);
    rig.momentum->SetLevel(LevelNs::MomentumLevel::MaxDash);
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    const std::vector<LevelNs::LaunchedBodyComponent*> debris = DebrisBodies(scene);
    ASSERT_EQ(debris.size(), 5u);
    auto anyFlying = [&debris]() {
        for (LevelNs::LaunchedBodyComponent* body : debris)
        {
            if (body->IsFlying())
                return true;
        }
        return false;
    };
    int guard = 0;
    while (anyFlying() && guard < 300)
    {
        StepBody(scene);
        ++guard;
    }
    ASSERT_LT(guard, 300);

    for (int i = 0; i < 5; ++i)
        StepBody(scene);
    for (LevelNs::LaunchedBodyComponent* body : debris)
    {
        auto* mesh = body->Owner()->FindComponent<SceneNs::MeshRendererComponent>();
        ASSERT_NE(mesh, nullptr);
        EXPECT_FALSE(mesh->IsActiveSelf());
        EXPECT_FALSE(body->IsActiveSelf());
    }
}

// 破片と跡は解放の歩に出る。止まった 1 枚の横で破片だけが飛ばない
TEST(CollisionImpact, DebrisWaitForRelease)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.breakable->SetToughness(1.0f);
    rig.momentum->SetLevel(LevelNs::MomentumLevel::MaxDash);
    BeginSlam(scene, rig, k_MaxDashSpeed, 0.0f);
    const std::size_t before = scene.World().ObjectCount();

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidBreak());
    EXPECT_EQ(scene.World().ObjectCount(), before);

    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    EXPECT_EQ(scene.World().ObjectCount(), before);

    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);
    EXPECT_EQ(scene.World().ObjectCount(), before + 6);
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

// 押し飛ばされた配置物は消えない。0 は消えない指定
TEST(LaunchedBody, RestWithZeroLifeStaysVisible)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    rig.body->Launch(Vector3{4.0f, 4.0f, 0.0f});
    const int steps = RunUntilRest(scene, *rig.body, k_RestStepLimit);
    ASSERT_LT(steps, k_RestStepLimit);

    for (int i = 0; i < 120; ++i)
        StepBody(scene);

    auto* mesh = rig.object->FindComponent<SceneNs::MeshRendererComponent>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_TRUE(mesh->IsActiveSelf());
    EXPECT_TRUE(rig.box->IsActiveSelf());
    EXPECT_TRUE(rig.body->IsActiveSelf());
}

// 寿命を入れた物は止まってから消え、当たりも外れる
TEST(LaunchedBody, SetRestLifeSecondsHidesAfterRest)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    rig.body->SetRestLifeSeconds(0.05f);
    rig.body->Launch(Vector3{4.0f, 4.0f, 0.0f});
    const int steps = RunUntilRest(scene, *rig.body, k_RestStepLimit);
    ASSERT_LT(steps, k_RestStepLimit);

    for (int i = 0; i < 5; ++i)
        StepBody(scene);

    auto* mesh = rig.object->FindComponent<SceneNs::MeshRendererComponent>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_FALSE(mesh->IsActiveSelf());
    EXPECT_FALSE(rig.box->IsActiveSelf());
    EXPECT_FALSE(rig.body->IsActiveSelf());
    EXPECT_EQ(scene.Physics().Aabbs().size(), rig.restingAabbs - 1);
}

// 壊れた値は捨てる。非有限値と負で寿命が入らない
TEST(LaunchedBody, RestLifeRejectsNonFiniteAndNegative)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    rig.body->SetRestLifeSeconds(std::numeric_limits<float>::quiet_NaN());
    rig.body->SetRestLifeSeconds(-2.0f);
    rig.body->Launch(Vector3{4.0f, 4.0f, 0.0f});
    const int steps = RunUntilRest(scene, *rig.body, k_RestStepLimit);
    ASSERT_LT(steps, k_RestStepLimit);

    for (int i = 0; i < 120; ++i)
        StepBody(scene);

    auto* mesh = rig.object->FindComponent<SceneNs::MeshRendererComponent>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_TRUE(mesh->IsActiveSelf());
}

// 跡は指定位置に出る一時オブジェクト。保存や凍結に写らない印が立つ
TEST(ImpactMark, SpawnAtPlacesTransientMark)
{
    NS::Core::FrameTimer::SetFixedDelta(k_FixedDt);
    SceneNs::Scene scene;
    const std::size_t before = scene.World().ObjectCount();

    SceneNs::GameObject* mark = LevelNs::ImpactMarkComponent::SpawnAt(&scene, Vector3{3.0f, 0.02f, 5.0f});

    ASSERT_NE(mark, nullptr);
    EXPECT_EQ(scene.World().ObjectCount(), before + 1);
    EXPECT_TRUE(mark->IsTransient());
    const Vector3 pos = mark->Root().Position();
    EXPECT_FLOAT_EQ(pos.x, 3.0f);
    EXPECT_FLOAT_EQ(pos.y, 0.02f);
    EXPECT_FLOAT_EQ(pos.z, 5.0f);
    EXPECT_EQ(LevelNs::ImpactMarkComponent::SpawnAt(nullptr, Vector3{}), nullptr);
}

// 跡は保存に写らない
TEST(ImpactMark, SkipsSaveCapture)
{
    NS::Core::FrameTimer::SetFixedDelta(k_FixedDt);
    SceneNs::Scene scene;
    SceneNs::GameObject* mark = LevelNs::ImpactMarkComponent::SpawnAt(&scene, Vector3{0.0f, 0.02f, 0.0f});
    ASSERT_NE(mark, nullptr);

    const SceneNs::SceneData data = scene.CaptureLiveToSceneData();

    EXPECT_TRUE(data.objects.empty());
}

// 見た目は床へ寝かせた半透明の板
TEST(ImpactMark, UsesShadowQuadLook)
{
    NS::Core::FrameTimer::SetFixedDelta(k_FixedDt);
    SceneNs::Scene scene;
    SceneNs::GameObject* mark = LevelNs::ImpactMarkComponent::SpawnAt(&scene, Vector3{0.0f, 0.02f, 0.0f});
    ASSERT_NE(mark, nullptr);

    auto* mesh = mark->FindComponent<SceneNs::MeshRendererComponent>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->MeshRef(), "shadowQuad");
    EXPECT_EQ(mesh->MaterialRef(), "shadow");
}

// 出た直後の水平の大きさが跡の直径
TEST(ImpactMark, StartsAtDiameter)
{
    NS::Core::FrameTimer::SetFixedDelta(k_FixedDt);
    SceneNs::Scene scene;
    SceneNs::GameObject* mark = LevelNs::ImpactMarkComponent::SpawnAt(&scene, Vector3{0.0f, 0.02f, 0.0f});
    ASSERT_NE(mark, nullptr);

    const Vector3 scale = mark->Root().Scale();
    EXPECT_FLOAT_EQ(scale.x, 1.5f);
    EXPECT_FLOAT_EQ(scale.z, 1.5f);
}

// 寿命の半分で大きさも半分。線形に縮む
TEST(ImpactMark, ShrinksToHalfAtHalfLife)
{
    NS::Core::FrameTimer::SetFixedDelta(k_FixedDt);
    SceneNs::Scene scene;
    SceneNs::GameObject* mark = LevelNs::ImpactMarkComponent::SpawnAt(&scene, Vector3{0.0f, 0.02f, 0.0f});
    ASSERT_NE(mark, nullptr);

    for (int i = 0; i < 180; ++i)
        StepBody(scene);

    const Vector3 scale = mark->Root().Scale();
    EXPECT_NEAR(scale.x, 0.75f, 0.02f);
    EXPECT_NEAR(scale.z, 0.75f, 0.02f);
}

// 寿命が尽きたら描画と更新を止める。配置物は破棄しない
TEST(ImpactMark, HidesAfterLifeWithoutDestroy)
{
    NS::Core::FrameTimer::SetFixedDelta(k_FixedDt);
    SceneNs::Scene scene;
    SceneNs::GameObject* mark = LevelNs::ImpactMarkComponent::SpawnAt(&scene, Vector3{0.0f, 0.02f, 0.0f});
    ASSERT_NE(mark, nullptr);
    const std::size_t after = scene.World().ObjectCount();

    for (int i = 0; i < 370; ++i)
        StepBody(scene);

    auto* mesh = mark->FindComponent<SceneNs::MeshRendererComponent>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_FALSE(mesh->IsActiveSelf());
    auto* comp = mark->FindComponent<LevelNs::ImpactMarkComponent>();
    ASSERT_NE(comp, nullptr);
    EXPECT_FALSE(comp->IsActiveSelf());
    EXPECT_EQ(scene.World().ObjectCount(), after);
}
