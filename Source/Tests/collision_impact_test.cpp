#include "Game/Level/BlockObject.h"
#include "Game/Player.h"

#include <Game/Level/BreakableComponent.h>
#include <Game/Level/ImpactResolverComponent.h>
#include <Game/Level/MomentumComponent.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/BoxColliderComponent.h>
#include <Runtime/Object/Components/CharacterMovementComponent.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Reflection/TypeRegistry.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/World.h>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
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

    struct Rig
    {
        SceneNs::CharacterMovementComponent* movement = nullptr;
        LevelNs::MomentumComponent* momentum = nullptr;
        LevelNs::ImpactResolverComponent* impact = nullptr;
        SceneNs::BoxColliderComponent* targetBox = nullptr;
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
        return rig;
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
} // namespace

TEST(CollisionImpact, ReboundsAwayFromApproachedBox)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    ASSERT_NE(rig.movement, nullptr);
    ASSERT_NE(rig.impact, nullptr);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);

    EXPECT_TRUE(rig.impact->DidRebound());
    EXPECT_LT(rig.movement->Velocity().x, 0.0f);
    EXPECT_GT(rig.movement->Velocity().y, 0.0f);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().z, 0.0f);
}

TEST(CollisionImpact, ReboundHorizontalSpeedMatchesReboundSpeed)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);

    EXPECT_FLOAT_EQ(HorizontalSpeed(rig.movement->Velocity()), k_ReboundSpeed);
}

TEST(CollisionImpact, ReboundAddsUpSpeed)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);

    EXPECT_FLOAT_EQ(rig.movement->Velocity().y, k_ReboundUpSpeed);
}

TEST(CollisionImpact, ReboundStartsMomentumGrace)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{}, 1, 0, true);
    ASSERT_NE(rig.momentum, nullptr);
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
    rig.movement->SetVelocity(Vector3{0.0f, 0.0f, k_RunSpeed});

    Step(scene);

    EXPECT_TRUE(rig.impact->DidRebound());
    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().z, -k_ReboundSpeed);
}

TEST(CollisionImpact, ReboundsFromDeepOverlapWhenApproaching)
{
    SceneNs::Scene scene;
    Rig rig = Build(scene, Vector3{0.55f, 0.0f, 0.0f}, 1, 0, true);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);

    EXPECT_TRUE(rig.impact->DidRebound());
    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, -k_ReboundSpeed);
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
    SetFloatField(*rig.impact, "反発初速", 5.0f);
    SetFloatField(*rig.impact, "反発の上向き初速", 1.0f);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    Step(scene);

    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, -5.0f);
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
