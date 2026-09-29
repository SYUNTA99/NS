#include "Editor/EditorObjects.h"
#include "Game/Player.h"

#include <Game/Entity/EntityComponent.h>
#include <Game/Level/Breakable.h>
#include <Game/Level/HitTier.h>
#include <Game/Level/ImpactResolver.h>
#include <Game/Level/LaunchedBody.h>
#include <Game/Player/EffectLayerList.h>
#include <Game/Player/ImpactEffects.h>
#include <Game/Player/PlayerComponent.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/PlayerInput.h>
#include <Runtime/Object/Components/RigidBody.h>
#include <Runtime/Object/Components/TransformComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/ObjectList.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Platform/Clock.h>

#include "hit_zones_entry.h"
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using NS::Core::Vector3;
    using NS::Game::Level::HitTier;
    using NS::Game::Level::ImpactRecord;
    using NS::Game::Level::ImpactResolver;
    using NS::Game::Player::EffectLayerRecord;
    using NS::Game::Player::ImpactEffects;
    using NS::Game::Player::ImpactShape;
    using NS::Game::Player::PlayerComponent;

    constexpr float k_FixedDt = 1.0f / 60.0f;
    constexpr float k_RunSpeed = 8.0f;
    // 壊れない高さの耐久。押し飛ばしと反動を見る
    constexpr float k_Unbreakable = 99.0f;

    struct Rig
    {
        PlayerComponent* movement = nullptr;
        ImpactResolver* impact = nullptr;
        ImpactEffects* effects = nullptr;
    };

    struct Course
    {
        float start = -0.5f;
        float lateral = 0.0f;
        std::int16_t targetCell = 6;
        float mass = 1.0f;
        // 0 より大きければ、1 m の箱を並べる代わりに x = -3 からここまでを 1 枚の床にする
        // 飛ばした相手が落ちる所まで敷く
        // 箱を並べると、落ちた相手が継ぎ目の角に当たって斜めの法線で止められる
        std::int16_t floorLastX = 0;
        // 0 でなければ、この x に床から 8 段の壁を立てる。飛ばした相手が床より先に当たる
        std::int16_t wallX = 0;
    };

    // 中心近くは 6 マス先の的へ長く走って当て、止めを上限の 12 まで伸ばす
    // 横ずれ 0.7 は、的の半幅 0.5 + 自機の半径 0.4 で割ると 0.78 で大きな外れ
    // 長く走ると寄せで横ずれが縮むので、大きな外れは 1 マス先の的に当てる
    constexpr Course k_CenterCourse{.lateral = 0.0f};
    constexpr Course k_WideCourse{.start = 0.0f, .lateral = 0.7f, .targetCell = 1};

    Rig Build(NS::Obj::Scene& scene, const Course& course)
    {
        NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt);
        nlohmann::json data = NS::Obj::MakeSceneJson();
        nlohmann::json player = MakePlayerObject(Vector3{course.start, 1.41f, course.lateral}, NS::Core::Quaternion{});
        NS::Obj::ObjectJsonComponents(player).push_back(NS::Obj::MakeComponentEntry("ImpactResolver"));
        NS::Obj::ObjectJsonComponents(player).push_back(NS::Obj::MakeComponentEntry("CollisionInput"));
        NS::Obj::ObjectJsonComponents(player).push_back(NS::Obj::MakeComponentEntry("ImpactEffects"));
        NS::Obj::SceneJsonObjects(data).push_back(player);

        std::int16_t lateralCell = 0;
        if (course.lateral > 0.0f)
        {
            lateralCell = 1;
        }
        if (course.floorLastX > 0)
        {
            nlohmann::json floor = NS::Editor::MakeCellObject(0, 0, 0);
            const float cells = static_cast<float>(course.floorLastX + 3 + 1);
            NS::Obj::SetObjectPosition(floor, Vector3{0.5f * static_cast<float>(course.floorLastX - 3), 0.0f, 0.0f});
            NS::Obj::SetObjectScale(floor, Vector3{cells, 1.0f, 9.0f});
            NS::Obj::SceneJsonObjects(data).push_back(floor);
        }
        else
        {
            for (std::int16_t i = -3; i <= static_cast<std::int16_t>(course.targetCell + 3); ++i)
            {
                NS::Obj::SceneJsonObjects(data).push_back(NS::Editor::MakeCellObject(i, 0, 0));
                if (lateralCell != 0)
                {
                    NS::Obj::SceneJsonObjects(data).push_back(NS::Editor::MakeCellObject(i, 0, lateralCell));
                }
            }
        }
        if (course.wallX != 0)
        {
            for (std::int16_t y = 1; y <= 8; ++y)
            {
                for (std::int16_t z = -2; z <= 2; ++z)
                {
                    NS::Obj::SceneJsonObjects(data).push_back(NS::Editor::MakeCellObject(course.wallX, y, z));
                }
            }
        }
        nlohmann::json target = NS::Editor::MakeCellObject(course.targetCell, 1, 0);
        nlohmann::json body = NS::Obj::MakeComponentEntry("RigidBody");
        NS::Obj::SetField(body, "キネマティック", true);
        NS::Obj::SetField(body, "質量", course.mass);
        NS::Obj::ObjectJsonComponents(target).push_back(body);
        NS::Obj::ObjectJsonComponents(target).push_back(NS::Obj::MakeComponentEntry("Breakable"));
        NsTest::AddTestHitZones(NS::Obj::ObjectJsonComponents(target));
        NS::Obj::SceneJsonObjects(data).push_back(target);
        scene.LoadJson(std::move(data));

        Rig rig;
        Player* live = FindPlayer(scene.Objects());
        EXPECT_NE(live, nullptr);
        if (live == nullptr)
        {
            return rig;
        }
        rig.movement = live->FindComponent<PlayerComponent>();
        rig.impact = live->FindComponent<ImpactResolver>();
        rig.effects = live->FindComponent<ImpactEffects>();
        // 起こしたままだと実機の入力が毎フレーム 0 を書き込み、走る速さと向きが台から消える
        if (NS::Obj::PlayerInput* input = live->FindComponent<NS::Obj::PlayerInput>())
        {
            input->SetActive(false);
        }
        scene.Objects().ForEachComponent<NS::Game::Level::Breakable>(
            [](NS::Game::Level::Breakable& breakable) { breakable.SetToughness(k_Unbreakable); });
        return rig;
    }

    // 帯の範囲は半開なので Update の移動と Update + 60 の部品は入らない。ゲームの帯と同じ順で後から回す
    void Step(NS::Obj::Scene& scene, const Rig& rig)
    {
        scene.Objects().UpdateObjects(NS::Obj::TickPriority::EarlyUpdate, NS::Obj::TickPriority::Update);
        rig.movement->OnUpdate();
        rig.effects->OnUpdate();
    }

    void BeginSlam(NS::Obj::Scene& scene, const Rig& rig, float charge01)
    {
        for (int i = 0; i < 30 && !rig.movement->IsGrounded(); ++i)
        {
            Step(scene, rig);
        }
        rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});
        rig.movement->SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.0f);
        rig.movement->RequestBodySlam(charge01);
        Step(scene, rig);
    }

    // 止めの頭と明けの記録のフレーム。立たなければ空
    struct Beats
    {
        std::optional<int> freeze;
        std::optional<int> release;
    };

    // 止めの頭から明けの 12 フレーム後まで進める。止めが無ければ steps だけ進める
    Beats RunHit(NS::Obj::Scene& scene, const Rig& rig, int steps)
    {
        Beats beats;
        for (int i = 0; i < steps; ++i)
        {
            Step(scene, rig);
            if (rig.impact->FreezeBeganThisStep() && !beats.freeze.has_value())
            {
                beats.freeze = rig.effects->Layers().Step();
            }
            if (rig.impact->ReleasedThisStep() && !beats.release.has_value())
            {
                beats.release = rig.effects->Layers().Step();
            }
            if (beats.release.has_value() && rig.effects->Layers().Step() > beats.release.value() + 12)
            {
                break;
            }
        }
        return beats;
    }

    // 名前の層の記録を出した順に返す
    std::vector<EffectLayerRecord> Named(const ImpactEffects& effects, std::string_view name)
    {
        std::vector<EffectLayerRecord> found;
        for (const EffectLayerRecord& record : effects.Layers().Records())
        {
            if (record.name == name)
            {
                found.push_back(record);
            }
        }
        return found;
    }

    // 名前の層が 1 つだけ出ていれば、止めの頭から数えた出たフレームを返す
    std::optional<int> StartFrom(const ImpactEffects& effects, std::string_view name, int freeze)
    {
        const std::vector<EffectLayerRecord> found = Named(effects, name);
        if (found.size() != 1)
        {
            return std::nullopt;
        }
        return found[0].startStep - freeze;
    }

    ImpactRecord MakeRecord(HitTier tier, float power, int hitStopSteps, float mass)
    {
        ImpactRecord record;
        record.tier = tier;
        record.power = power;
        record.hitStopSteps = hitStopSteps;
        record.targetMass = mass;
        record.targetPlaced = true;
        // 押し飛ばしの質量指数 0.35 と反動の質量因子。ImpactResolver が書く比と同じ式
        record.launchScale = power / std::pow(mass, 0.35f);
        record.reboundScale = power * 2.0f * mass / (mass + 1.0f);
        return record;
    }
} // namespace

// 中心近くは止めの頭に核と照り、次のフレームに光条、3 フレーム目に輪と火花、核が落ちるフレームに火の粉、明けに粉、
// 明けの 4 フレーム後に弾かれ線。核の親を止めるのは止めと結んだ留まりの終わり。光条は核が落ちた後も細いまま残り、
// 留まりの終わりの 3 フレーム後に親を止めて薄れ、その 3 フレーム後に消える
TEST(ImpactEffects, CenterHitStartsEachLayerOnItsFrame)
{
    NS::Obj::Scene scene;
    const Rig rig = Build(scene, k_CenterCourse);
    ASSERT_NE(rig.effects, nullptr);
    BeginSlam(scene, rig, 1.0f);
    const Beats beats = RunHit(scene, rig, 90);
    ASSERT_TRUE(beats.freeze.has_value());
    ASSERT_TRUE(beats.release.has_value());
    ASSERT_EQ(rig.impact->LastImpact().tier, HitTier::Center);
    const int c = beats.freeze.value();
    const int r = beats.release.value();
    const int stop = rig.impact->LastImpact().hitStopSteps;
    ASSERT_EQ(r - c, stop);
    const int e = std::min(10, stop - 1);

    EXPECT_EQ(StartFrom(*rig.effects, "impact.core", c), 0);
    EXPECT_EQ(StartFrom(*rig.effects, "impact.sparks", c), 3);
    EXPECT_EQ(StartFrom(*rig.effects, "impact.streak", c), 1);
    EXPECT_EQ(StartFrom(*rig.effects, "impact.glow", c), 0);
    EXPECT_EQ(StartFrom(*rig.effects, "impact.ring", c), 3);
    EXPECT_EQ(StartFrom(*rig.effects, "impact.embers", c), e + 1);
    EXPECT_EQ(StartFrom(*rig.effects, "impact.dust", c), r - c);
    EXPECT_EQ(StartFrom(*rig.effects, "impact.recoil", c), r - c + 4);

    const std::vector<EffectLayerRecord> core = Named(*rig.effects, "impact.core");
    ASSERT_EQ(core.size(), 1u);
    ASSERT_TRUE(core[0].rootStopStep.has_value());
    EXPECT_EQ(core[0].rootStopStep.value() - c, e);
    const std::vector<EffectLayerRecord> streak = Named(*rig.effects, "impact.streak");
    ASSERT_EQ(streak.size(), 1u);
    ASSERT_TRUE(streak[0].rootStopStep.has_value());
    EXPECT_EQ(streak[0].rootStopStep.value() - c, e + 3);
    ASSERT_TRUE(streak[0].endStep.has_value());
    EXPECT_EQ(streak[0].endStep.value() - c, e + 6);
    const std::vector<EffectLayerRecord> ring = Named(*rig.effects, "impact.ring");
    ASSERT_EQ(ring.size(), 1u);
    ASSERT_TRUE(ring[0].endStep.has_value());
    EXPECT_EQ(ring[0].endStep.value() - c, 10);

    // 火花の量は粒の数 × 速さ、粉の量は塊の大きさ
    const ImpactShape shape = rig.effects->ShapeFor(rig.impact->LastImpact());
    const std::vector<EffectLayerRecord> sparks = Named(*rig.effects, "impact.sparks");
    ASSERT_EQ(sparks.size(), 1u);
    ASSERT_TRUE(sparks[0].amount.has_value());
    EXPECT_FLOAT_EQ(sparks[0].amount.value(), static_cast<float>(shape.sparkCount) * shape.sparkSpeed);
    EXPECT_FLOAT_EQ(shape.sparkAmount, static_cast<float>(shape.sparkCount) * shape.sparkSpeed);
    const std::vector<EffectLayerRecord> dust = Named(*rig.effects, "impact.dust");
    ASSERT_EQ(dust.size(), 1u);
    ASSERT_TRUE(dust[0].amount.has_value());
    EXPECT_FLOAT_EQ(dust[0].amount.value(), shape.dustScale);
}

// 大きな外れは光条・輪・照りを出さず、火花は横ずれの側と相手の飛ぶ向きと上の間へ擦れる。核は 4 フレーム目まで留まる
TEST(ImpactEffects, WideHitLeavesOutTheStreakRingAndGlowAndScrapesTheSparksSideways)
{
    NS::Obj::Scene scene;
    const Rig rig = Build(scene, k_WideCourse);
    ASSERT_NE(rig.effects, nullptr);
    BeginSlam(scene, rig, 1.0f);
    const Beats beats = RunHit(scene, rig, 90);
    ASSERT_TRUE(beats.freeze.has_value());
    ASSERT_EQ(rig.impact->LastImpact().tier, HitTier::Wide);
    const int c = beats.freeze.value();

    EXPECT_TRUE(Named(*rig.effects, "impact.streak").empty());
    EXPECT_TRUE(Named(*rig.effects, "impact.ring").empty());
    EXPECT_TRUE(Named(*rig.effects, "impact.glow").empty());
    EXPECT_TRUE(Named(*rig.effects, "impact.embers").empty());
    EXPECT_EQ(StartFrom(*rig.effects, "impact.core", c), 0);
    const std::vector<EffectLayerRecord> core = Named(*rig.effects, "impact.core");
    ASSERT_EQ(core.size(), 1u);
    ASSERT_TRUE(core[0].rootStopStep.has_value());
    EXPECT_EQ(core[0].rootStopStep.value() - c, 4);
    EXPECT_EQ(StartFrom(*rig.effects, "impact.sparks", c), 0);

    // 自機は的の +Z 側を通る。火花は飛ぶ向き (+X) と、それに直角な +Z と、上の 3 つを同じ重みで合わせた向きへ擦れる
    const Vector3 spark = rig.effects->LastAim().sparkDir;
    Vector3 launch = rig.impact->LastImpact().impactDir;
    launch.y = 0.0f;
    launch.Normalize();
    const float third = std::sqrt(1.0f / 3.0f);
    EXPECT_NEAR(NS::Core::Dot(spark, launch), third, 1e-3f);
    EXPECT_NEAR(spark.z, third, 1e-2f);
    EXPECT_NEAR(spark.y, third, 1e-3f);
}

// 火花は相手の飛ぶ向き、弾かれ線は自機の反動の初速の向きへ出す
TEST(ImpactEffects, SparksFollowTheImpactDirAndRecoilLinesFollowTheSelfVelocity)
{
    NS::Obj::Scene scene;
    const Rig rig = Build(scene, k_CenterCourse);
    ASSERT_NE(rig.effects, nullptr);
    BeginSlam(scene, rig, 1.0f);
    const Beats beats = RunHit(scene, rig, 90);
    ASSERT_TRUE(beats.release.has_value());

    const ImpactRecord& impact = rig.impact->LastImpact();
    Vector3 self = impact.selfVelocity;
    self.Normalize();
    const Vector3 spark = rig.effects->LastAim().sparkDir;
    const Vector3 recoil = rig.effects->LastAim().recoilDir;
    EXPECT_NEAR(NS::Core::Dot(spark, impact.impactDir), 1.0f, 1e-4f);
    EXPECT_NEAR(NS::Core::Dot(recoil, self), 1.0f, 1e-4f);
}

// 弾かれ線は、出すフレームの自機の玉の縁のうち、反動の向きの逆の点から出す。接触点の横に出すと、
// 線の出た側を玉の後ろと読まれ、反動の向きと逆に見えた
TEST(ImpactEffects, RecoilLinesStartFromTheBallRimOppositeTheKnockback)
{
    NS::Obj::Scene scene;
    const Rig rig = Build(scene, k_CenterCourse);
    ASSERT_NE(rig.effects, nullptr);
    BeginSlam(scene, rig, 1.0f);
    std::optional<Vector3> ball;
    for (int i = 0; i < 90 && !ball.has_value(); ++i)
    {
        Step(scene, rig);
        if (!Named(*rig.effects, "impact.recoil").empty())
        {
            ball = rig.movement->Owner()->Root().Position();
        }
    }
    ASSERT_TRUE(ball.has_value());
    const NS::Game::Entity::EntityComponent* entity =
        rig.movement->Owner()->FindComponent<NS::Game::Entity::EntityComponent>();
    ASSERT_NE(entity, nullptr);

    const Vector3 recoil = rig.effects->LastAim().recoilDir;
    const Vector3 expected = ball.value() - recoil * entity->CapsuleRadius();
    const Vector3 origin = rig.effects->LastAim().recoilOrigin;
    EXPECT_NEAR(origin.x, expected.x, 1e-4f);
    EXPECT_NEAR(origin.y, expected.y, 1e-4f);
    EXPECT_NEAR(origin.z, expected.z, 1e-4f);
}

// 当たりの粉は明けに相手が居た床から舞うが、明けの 3 フレーム後まで自機の輪郭に掛からない所に出す
// 塊は輪の半径 (大きさの 0.6 倍まで) に生まれ、明けの 3 フレーム後に大きさの 0.35 倍の半径まで膨らむ (絵の定義)
// 相手の居た所に輪の真ん中を置くと、塊が隣の自機の縁を越え、横からの絵で自機の輪郭を覆った
TEST(ImpactEffects, HitDustStaysOffTheBallOutline)
{
    NS::Obj::Scene scene;
    const Rig rig = Build(scene, k_CenterCourse);
    ASSERT_NE(rig.effects, nullptr);
    BeginSlam(scene, rig, 1.0f);
    std::optional<Vector3> ball;
    for (int i = 0; i < 90 && !ball.has_value(); ++i)
    {
        Step(scene, rig);
        if (!Named(*rig.effects, "impact.dust").empty())
        {
            ball = rig.movement->Owner()->Root().Position();
        }
    }
    ASSERT_TRUE(ball.has_value());
    const std::vector<EffectLayerRecord> dust = Named(*rig.effects, "impact.dust");
    ASSERT_TRUE(dust.front().amount.has_value());
    const NS::Game::Entity::EntityComponent* entity =
        rig.movement->Owner()->FindComponent<NS::Game::Entity::EntityComponent>();
    ASSERT_NE(entity, nullptr);

    const Vector3 origin = rig.effects->LastAim().dustOrigin;
    const float apart = std::hypot(origin.x - ball->x, origin.z - ball->z);
    const float reach = (0.6f + 0.35f) * dust.front().amount.value();
    EXPECT_GE(apart - reach, entity->CapsuleRadius());
}

// 核の留まりの終わりは止めと結ぶ。止め 12 で 10、止め 7 で 6
TEST(ImpactEffects, CoreHoldsUntilOneFrameBeforeTheReleaseUpToTen)
{
    const ImpactEffects effects;

    EXPECT_EQ(effects.ShapeFor(MakeRecord(HitTier::Center, 2.0f, 12, 1.0f)).holdLastFrame, 10);
    EXPECT_EQ(effects.ShapeFor(MakeRecord(HitTier::Center, 2.0f, 7, 1.0f)).holdLastFrame, 6);
    EXPECT_EQ(effects.ShapeFor(MakeRecord(HitTier::Center, 2.0f, 30, 1.0f)).holdLastFrame, 10);
}

// 火花の数と速さ、火の粉の数、核の直径、光条の長さ、輪の半径、粉の大きさは威力で減らない
TEST(ImpactEffects, LayersGrowWithThePower)
{
    const ImpactEffects effects;
    const float powers[] = {0.7f, 1.0f, 1.4f, 1.8f, 2.0f};

    ImpactShape previous = effects.ShapeFor(MakeRecord(HitTier::Center, powers[0], 12, 1.0f));
    for (const float power : powers)
    {
        const ImpactShape shape = effects.ShapeFor(MakeRecord(HitTier::Center, power, 12, 1.0f));
        EXPECT_GE(shape.sparkCount, previous.sparkCount) << power;
        EXPECT_GE(shape.sparkSpeed, previous.sparkSpeed) << power;
        EXPECT_GE(shape.coreDiameter, previous.coreDiameter) << power;
        EXPECT_GE(shape.streakLength, previous.streakLength) << power;
        EXPECT_GE(shape.ringRadius, previous.ringRadius) << power;
        EXPECT_GE(shape.emberCount, previous.emberCount) << power;
        EXPECT_GE(shape.dustScale, previous.dustScale) << power;
        previous = shape;
    }
    const ImpactShape weakest = effects.ShapeFor(MakeRecord(HitTier::Center, 0.7f, 12, 1.0f));
    const ImpactShape strongest = effects.ShapeFor(MakeRecord(HitTier::Center, 2.0f, 12, 1.0f));
    EXPECT_LT(weakest.sparkCount, strongest.sparkCount);
    EXPECT_LT(weakest.sparkSpeed, strongest.sparkSpeed);
    EXPECT_LT(weakest.emberCount, strongest.emberCount);
    // 粉の大きさは段の見分けの数表に並ぶ量。走行の威力 (1.5〜2.0) でも外れが中心近くより小さい
    const float wideDust = effects.ShapeFor(MakeRecord(HitTier::Wide, 1.5f, 6, 1.0f)).dustScale;
    EXPECT_LT(wideDust, strongest.dustScale);
    // 核は玉の直径 1.3 m の 54% (0.7 m) を超えない
    EXPECT_LE(strongest.coreDiameter, 0.7f);
    // 火花の量は威力の順に並ぶ。質量 8 に溜めきりで当てた火花は遅いが、質量 1 のタップより量が多い
    const ImpactShape heavyCharged = effects.ShapeFor(MakeRecord(HitTier::Center, 1.93f, 12, 8.0f));
    const ImpactShape tap = effects.ShapeFor(MakeRecord(HitTier::Center, 1.0f, 8, 1.0f));
    EXPECT_LT(heavyCharged.sparkSpeed, tap.sparkSpeed);
    EXPECT_GT(heavyCharged.sparkAmount, tap.sparkAmount);
}

// 同じ当たりで質量 8 の相手は質量 1 より、弾かれ線が長く、火花が遅い
TEST(ImpactEffects, HeavierTargetLengthensTheRecoilLinesAndSlowsTheSparks)
{
    NS::Obj::Scene lightScene;
    const Rig light = Build(lightScene, Course{.mass = 1.0f});
    NS::Obj::Scene heavyScene;
    const Rig heavy = Build(heavyScene, Course{.mass = 8.0f});
    ASSERT_NE(light.effects, nullptr);
    ASSERT_NE(heavy.effects, nullptr);
    BeginSlam(lightScene, light, 1.0f);
    BeginSlam(heavyScene, heavy, 1.0f);
    ASSERT_TRUE(RunHit(lightScene, light, 90).release.has_value());
    ASSERT_TRUE(RunHit(heavyScene, heavy, 90).release.has_value());

    const std::vector<EffectLayerRecord> lightRecoil = Named(*light.effects, "impact.recoil");
    const std::vector<EffectLayerRecord> heavyRecoil = Named(*heavy.effects, "impact.recoil");
    const std::vector<EffectLayerRecord> lightSparks = Named(*light.effects, "impact.sparks");
    const std::vector<EffectLayerRecord> heavySparks = Named(*heavy.effects, "impact.sparks");
    ASSERT_EQ(lightRecoil.size(), 1u);
    ASSERT_EQ(heavyRecoil.size(), 1u);
    ASSERT_EQ(lightSparks.size(), 1u);
    ASSERT_EQ(heavySparks.size(), 1u);
    EXPECT_GT(heavyRecoil[0].amount.value_or(0.0f), lightRecoil[0].amount.value_or(0.0f));
    EXPECT_LT(heavySparks[0].amount.value_or(0.0f), lightSparks[0].amount.value_or(0.0f));
}

// 続けて当てると、2 回目の止めの頭に 2 回目の核が出て、前の当たりの粉の親を止める
TEST(ImpactEffects, SecondHitStartsItsOwnCoreAndStopsThePreviousDust)
{
    NS::Obj::Scene scene;
    const Rig rig = Build(scene, k_CenterCourse);
    ASSERT_NE(rig.effects, nullptr);
    BeginSlam(scene, rig, 1.0f);
    ASSERT_TRUE(RunHit(scene, rig, 90).release.has_value());
    const std::vector<EffectLayerRecord> firstDust = Named(*rig.effects, "impact.dust");
    ASSERT_EQ(firstDust.size(), 1u);
    ASSERT_FALSE(firstDust[0].rootStopStep.has_value());

    // 飛ばした相手は LateUpdate を回さないので元の所に残る。反動で着地してから、もう一度突進して同じ相手に当てる
    for (int i = 0; i < 180 && (rig.movement->IsRebounding() || !rig.movement->IsGrounded()); ++i)
    {
        Step(scene, rig);
    }
    ASSERT_FALSE(rig.movement->IsRebounding());
    BeginSlam(scene, rig, 1.0f);
    const Beats second = RunHit(scene, rig, 180);
    ASSERT_TRUE(second.freeze.has_value());

    const std::vector<EffectLayerRecord> cores = Named(*rig.effects, "impact.core");
    ASSERT_EQ(cores.size(), 2u);
    EXPECT_EQ(cores[1].startStep, second.freeze.value());
    const std::vector<EffectLayerRecord> dusts = Named(*rig.effects, "impact.dust");
    ASSERT_GE(dusts.size(), 1u);
    ASSERT_TRUE(dusts[0].rootStopStep.has_value());
    EXPECT_EQ(dusts[0].rootStopStep.value(), second.freeze.value());
}

// 止めが 0 の当たりは返りを出さないのと同じく、層を 1 つも出さない
TEST(ImpactEffects, HitWithoutAFreezeStartsNoLayer)
{
    NS::Obj::Scene scene;
    const Rig rig = Build(scene, k_CenterCourse);
    ASSERT_NE(rig.effects, nullptr);
    const NS::Obj::FieldDesc* field = NS::Obj::FindField(rig.impact->GetReflection(), "ヒットストップ基準秒");
    ASSERT_NE(field, nullptr);
    const float zero = 0.0f;
    field->set(rig.impact, &zero);
    BeginSlam(scene, rig, 1.0f);
    bool detected = false;
    for (int i = 0; i < 60; ++i)
    {
        Step(scene, rig);
        if (rig.impact->DidRebound())
        {
            detected = true;
        }
    }
    ASSERT_TRUE(detected);
    EXPECT_TRUE(rig.effects->Layers().Records().empty());
}

// 飛んでいた相手の下に床は無い。粉と照りを出さない
TEST(ImpactEffects, FlyingTargetGetsNoDustNorGlow)
{
    const ImpactEffects effects;
    ImpactRecord flying = MakeRecord(HitTier::Center, 2.0f, 12, 1.0f);
    flying.targetPlaced = false;

    const ImpactShape placedShape = effects.ShapeFor(MakeRecord(HitTier::Center, 2.0f, 12, 1.0f));
    const ImpactShape flyingShape = effects.ShapeFor(flying);

    EXPECT_GT(placedShape.dustCount, 0);
    EXPECT_GT(placedShape.glowDiameter, 0.0f);
    EXPECT_EQ(flyingShape.dustCount, 0);
    EXPECT_EQ(flyingShape.glowDiameter, 0.0f);
}

namespace
{
    // 飛ばした相手が曲線から剛体へ渡るまで見る台。物理と LateUpdate の帯も回すので、ゲームと同じ 1 フレーム
    void FullStep(NS::Obj::Scene& scene)
    {
        scene.OnUpdate();
    }

    // BeginSlam と同じ突進を、物理も回す 1 フレームで出す
    void BeginSlamWithPhysics(NS::Obj::Scene& scene, const Rig& rig)
    {
        for (int i = 0; i < 30 && !rig.movement->IsGrounded(); ++i)
        {
            FullStep(scene);
        }
        rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});
        rig.movement->SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.0f);
        rig.movement->RequestBodySlam(1.0f);
        FullStep(scene);
    }

    // 明けのフレームと、飛ばした相手が曲線を離れたフレーム。どちらも記録のフレームで数え、立たなければ空
    // 離れるのは物理の後の LateUpdate なので、部品が読むのはその次のフレーム
    struct Flight
    {
        std::optional<int> release;
        std::optional<int> handed;
    };

    Flight RunFlight(NS::Obj::Scene& scene, const Rig& rig, int steps)
    {
        Flight flight;
        const NS::Game::Level::LaunchedBody* body = nullptr;
        for (int i = 0; i < steps && !flight.handed.has_value(); ++i)
        {
            FullStep(scene);
            const int step = rig.effects->Layers().Step();
            if (!flight.release.has_value() && rig.impact->ReleasedThisStep())
            {
                flight.release = step;
                const NS::Obj::GameObject* target =
                    scene.Objects().FindObject(NS::Obj::ObjectRef{rig.impact->LastImpact().targetId});
                if (target != nullptr)
                {
                    body = target->FindComponent<NS::Game::Level::LaunchedBody>();
                }
                continue;
            }
            if (body != nullptr && body->Phase() != NS::Game::Level::LaunchPhase::Arc)
            {
                flight.handed = step;
            }
        }
        return flight;
    }
} // namespace

// 明けに、飛ばした相手の中心へ飛び出しの尾、自機の玉へ反動の尾を出す。飛び出しの尾の量は帯が残るフレーム数
TEST(ImpactEffects, ReleaseStartsTheLaunchTrailOnTheTargetAndTheReboundTrailOnTheBall)
{
    NS::Obj::Scene scene;
    const Rig rig = Build(scene, k_CenterCourse);
    ASSERT_NE(rig.effects, nullptr);
    BeginSlam(scene, rig, 1.0f);
    const Beats beats = RunHit(scene, rig, 90);
    ASSERT_TRUE(beats.freeze.has_value());
    ASSERT_TRUE(beats.release.has_value());
    const int c = beats.freeze.value();
    const int r = beats.release.value();

    EXPECT_EQ(StartFrom(*rig.effects, "launch.trail", c), r - c);
    EXPECT_EQ(StartFrom(*rig.effects, "rebound.trail", c), r - c);
    const ImpactShape shape = rig.effects->ShapeFor(rig.impact->LastImpact());
    const std::vector<EffectLayerRecord> launch = Named(*rig.effects, "launch.trail");
    ASSERT_EQ(launch.size(), 1u);
    ASSERT_TRUE(launch[0].amount.has_value());
    EXPECT_FLOAT_EQ(launch[0].amount.value(), static_cast<float>(shape.launchTrailFrames));
    EXPECT_FALSE(launch[0].rootStopStep.has_value());
}

// 反動の尾は頂点のフレームで親を止め、8 フレーム後に消える。着地の粉は着地の潰れのフレームに出て、
// 大きさは着地の前のフレームの落ちる速さで決まり、30 フレーム後に消える
TEST(ImpactEffects, ReboundTrailStopsItsRootAtTheApexAndEndsEightFramesLaterAndLandingDustStartsOnTheLandingSquash)
{
    NS::Obj::Scene scene;
    const Rig rig = Build(scene, k_CenterCourse);
    ASSERT_NE(rig.effects, nullptr);
    BeginSlam(scene, rig, 1.0f);
    const Beats beats = RunHit(scene, rig, 90);
    ASSERT_TRUE(beats.release.has_value());

    std::optional<int> apex;
    std::optional<int> landing;
    float fallSpeed = 0.0f;
    for (int i = 0; i < 180 && !landing.has_value(); ++i)
    {
        const float verticalBefore = rig.movement->VerticalVelocity();
        Step(scene, rig);
        const int step = rig.effects->Layers().Step();
        const bool rebounding = rig.movement->IsRebounding();
        if (!apex.has_value() && rebounding && !(rig.movement->VerticalVelocity() > 0.0f))
        {
            apex = step;
        }
        if (rebounding && rig.movement->ShouldLand())
        {
            landing = step;
            fallSpeed = -verticalBefore;
        }
    }
    ASSERT_TRUE(apex.has_value());
    ASSERT_TRUE(landing.has_value());
    ASSERT_GT(landing.value(), apex.value());
    ASSERT_GT(fallSpeed, 1.0f);
    // 消えるフレームまで回す
    for (int i = 0; i < 30; ++i)
    {
        Step(scene, rig);
    }

    const std::vector<EffectLayerRecord> trail = Named(*rig.effects, "rebound.trail");
    ASSERT_EQ(trail.size(), 1u);
    ASSERT_TRUE(trail[0].rootStopStep.has_value());
    EXPECT_EQ(trail[0].rootStopStep.value(), apex.value());
    ASSERT_TRUE(trail[0].endStep.has_value());
    EXPECT_EQ(trail[0].endStep.value() - apex.value(), 8);
    EXPECT_LT(trail[0].endStep.value(), landing.value());

    const std::vector<EffectLayerRecord> dust = Named(*rig.effects, "land.dust");
    ASSERT_EQ(dust.size(), 1u);
    EXPECT_EQ(dust[0].startStep, landing.value());
    ASSERT_TRUE(dust[0].amount.has_value());
    EXPECT_NEAR(dust[0].amount.value(), rig.effects->LandDustRadiusFor(fallSpeed), 1e-4f);
    ASSERT_TRUE(dust[0].endStep.has_value());
    EXPECT_EQ(dust[0].endStep.value() - landing.value(), 30);
}

// 飛ばした相手が床に落ちて剛体へ渡ると、次のフレームに飛び出しの尾を消し、落ちた所に粉を出す
TEST(ImpactEffects, TargetHandedToTheRigidBodyOnTheFloorStopsTheLaunchTrailAndRaisesLandDust)
{
    NS::Obj::Scene scene;
    const Rig rig = Build(scene, Course{.floorLastX = 120});
    ASSERT_NE(rig.effects, nullptr);
    BeginSlamWithPhysics(scene, rig);
    const Flight flight = RunFlight(scene, rig, 400);
    ASSERT_TRUE(flight.release.has_value());
    ASSERT_TRUE(flight.handed.has_value());
    for (int i = 0; i < 30; ++i)
    {
        FullStep(scene);
    }
    const int read = flight.handed.value() + 1;

    const std::vector<EffectLayerRecord> trail = Named(*rig.effects, "launch.trail");
    ASSERT_EQ(trail.size(), 1u);
    ASSERT_TRUE(trail[0].endStep.has_value());
    EXPECT_EQ(trail[0].endStep.value(), read);

    const std::vector<EffectLayerRecord> dust = Named(*rig.effects, "launch.landDust");
    ASSERT_EQ(dust.size(), 1u);
    EXPECT_EQ(dust[0].startStep, read);
    const ImpactShape shape = rig.effects->ShapeFor(rig.impact->LastImpact());
    ASSERT_TRUE(dust[0].amount.has_value());
    EXPECT_FLOAT_EQ(dust[0].amount.value(), shape.launchLandDustScale);
    ASSERT_TRUE(dust[0].endStep.has_value());
    EXPECT_EQ(dust[0].endStep.value() - read, 24);
}

// 飛ばした相手が床より先に壁へ当たって剛体へ渡った時は、飛び出しの尾を消すが、粉は出さない
TEST(ImpactEffects, TargetHandedToTheRigidBodyOnAWallRaisesNoLandDust)
{
    NS::Obj::Scene scene;
    const Rig rig = Build(scene, Course{.floorLastX = 120, .wallX = 12});
    ASSERT_NE(rig.effects, nullptr);
    BeginSlamWithPhysics(scene, rig);
    const Flight flight = RunFlight(scene, rig, 400);
    ASSERT_TRUE(flight.release.has_value());
    ASSERT_TRUE(flight.handed.has_value());
    for (int i = 0; i < 30; ++i)
    {
        FullStep(scene);
    }

    const std::vector<EffectLayerRecord> trail = Named(*rig.effects, "launch.trail");
    ASSERT_EQ(trail.size(), 1u);
    ASSERT_TRUE(trail[0].endStep.has_value());
    EXPECT_EQ(trail[0].endStep.value(), flight.handed.value() + 1);
    EXPECT_TRUE(Named(*rig.effects, "launch.landDust").empty());
}

// 着地の粉は落ちる速さ、飛ばした物の着地の粉は相手の質量と威力、飛び出しの尾が残るフレーム数は飛ばしの比で減らない
TEST(ImpactEffects, LandingDustsGrowWithTheFallSpeedTheMassAndThePower)
{
    const ImpactEffects effects;

    EXPECT_LT(effects.LandDustRadiusFor(2.0f), effects.LandDustRadiusFor(6.0f));
    EXPECT_LT(effects.LandDustRadiusFor(6.0f), effects.LandDustRadiusFor(12.0f));
    // 落ちる速さ 0 でも、出始めより大きく広がる
    EXPECT_GT(effects.LandDustRadiusFor(0.0f), 0.4f);

    const float light = effects.ShapeFor(MakeRecord(HitTier::Center, 2.0f, 12, 0.5f)).launchLandDustScale;
    const float middle = effects.ShapeFor(MakeRecord(HitTier::Center, 2.0f, 12, 1.0f)).launchLandDustScale;
    const float heavy = effects.ShapeFor(MakeRecord(HitTier::Center, 2.0f, 12, 8.0f)).launchLandDustScale;
    EXPECT_LT(light, middle);
    EXPECT_LT(middle, heavy);
    // 中心近く・大きな外れの台本の威力。同じ質量でも強く飛ばした物ほど大きい
    const float wide = effects.ShapeFor(MakeRecord(HitTier::Wide, 1.507f, 6, 1.0f)).launchLandDustScale;
    EXPECT_LT(wide, middle);

    const int shortTrail = effects.ShapeFor(MakeRecord(HitTier::Center, 2.0f, 12, 8.0f)).launchTrailFrames;
    const int longTrail = effects.ShapeFor(MakeRecord(HitTier::Center, 2.0f, 12, 0.5f)).launchTrailFrames;
    EXPECT_LT(shortTrail, longTrail);
    EXPECT_GE(shortTrail, 8);
}

// 反動の尾の筋は、自機が視線に沿って飛ぶ時だけ画面の上の向きを保って視線から起こし、一番太い真ん中を下塗りの幅ごと
// 玉の輪郭の外へ出す。視線に斜めに飛ぶ時とカメラが無い時は速度の向きのまま
TEST(ImpactEffects, ReboundStreakLeansOffTheLineOfSightOnlyWhileFlyingAlongIt)
{
    const Vector3 ball{0.0f, 3.3f, 0.0f};
    // 頂点の近くで自機がカメラへ向かって飛ぶ所 (溜めきりの当たりの 132 フレーム目の速度とカメラ)
    const Vector3 camera{5.03f, 4.43f, -0.09f};
    const Vector3 velocity{2.09f, 0.4f, 0.28f};
    const Vector3 sight = (ball - camera) / (ball - camera).Length();

    const Vector3 heading = ImpactEffects::ReboundTrailHeading(velocity, ball, camera);
    EXPECT_NEAR(heading.Length(), 1.0f, 1e-4f);
    const Vector3 back = heading * -1.0f;
    const Vector3 across = back - sight * NS::Core::Dot(back, sight);
    // 玉の半径 0.65 m と下塗りの幅の半分 0.15 m を、筋の真ん中までの 1.3 m で割った傾き
    EXPECT_GE(across.Length(), (0.65f + 0.15f) / 1.3f - 1e-4f);

    // 画面の上の向きと、視線の奥か手前かは変えない
    const Vector3 backBefore = velocity * (-1.0f / velocity.Length());
    const Vector3 acrossBefore = backBefore - sight * NS::Core::Dot(backBefore, sight);
    EXPECT_GT(NS::Core::Dot(across / across.Length(), acrossBefore / acrossBefore.Length()), 0.9999f);
    EXPECT_GT(NS::Core::Dot(back, sight), 0.0f);

    // 明けの上がり始め (97 フレーム目): 視線に斜めなので速度の向きのまま
    const Vector3 rising{2.09f, 7.18f, 0.28f};
    const Vector3 risingBall{0.0f, 1.27f, 0.0f};
    const Vector3 risingCamera{0.03f, 4.19f, -5.01f};
    const Vector3 kept = ImpactEffects::ReboundTrailHeading(rising, risingBall, risingCamera);
    const Vector3 risingDir = rising / rising.Length();
    EXPECT_NEAR(kept.x, risingDir.x, 1e-5f);
    EXPECT_NEAR(kept.y, risingDir.y, 1e-5f);
    EXPECT_NEAR(kept.z, risingDir.z, 1e-5f);

    const Vector3 noCamera = ImpactEffects::ReboundTrailHeading(velocity, ball, std::nullopt);
    const Vector3 velocityDir = velocity / velocity.Length();
    EXPECT_NEAR(noCamera.x, velocityDir.x, 1e-5f);
    EXPECT_NEAR(noCamera.y, velocityDir.y, 1e-5f);
    EXPECT_NEAR(noCamera.z, velocityDir.z, 1e-5f);
}
