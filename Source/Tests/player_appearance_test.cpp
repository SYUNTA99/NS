#include "Game/Player/PlayerAppearance.h"

#include <Game/Level/CollisionInput.h>
#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerStateManager.h>
#include <Runtime/Core/AABB.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Graphics/Mesh.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Object/AssetManager.h>
#include <Runtime/Object/Components/CapsuleCollider.h>
#include <Runtime/Object/Components/MeshRenderer.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/ObjectJson.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Reflection/ObjectBuilder.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Platform/Clock.h>
#include <Runtime/Platform/Filesystem.h>
#include <Runtime/Platform/Window.h>

#include "entity_test_stage.h"
#include "jolt_test_scene.h"
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <string>

namespace
{
    using NS::Core::Vector3;
    using NS::Game::Player::PlayerAppearance;
    using NS::Obj::CapsuleCollider;
    using NS::Obj::MeshRenderer;

    // 同梱シーンの自機と同じ当たりの欄。見た目はここから寸法を引く
    constexpr float k_Radius = 0.65f;
    constexpr float k_HalfHeight = 0.5f;

    // 見た目の mesh は GPU に作るので device が要る。作れない環境では飛ばす
    class PlayerAppearanceTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            NS::Platform::WindowDesc windowDesc{};
            windowDesc.title = "ns_player_appearance";
            windowDesc.size = NS::Core::Size2D{320, 240};
            windowDesc.visible = false;
            m_window = std::make_unique<NS::Platform::Window>(windowDesc);
            ASSERT_TRUE(m_window->IsValid());

            NS::Gfx::RendererDesc rendererDesc{};
            rendererDesc.vsync = false;
            rendererDesc.enableDebugLayer = false;
            m_renderer = std::make_unique<NS::Gfx::Renderer>(rendererDesc, *m_window);
            if (!m_renderer->IsValid())
            {
                GTEST_SKIP() << "Device 確立不可 (headless)";
            }
            m_assets = std::make_unique<NS::Obj::AssetManager>(NS::Platform::FileSystem::ContentRoot());
        }

        void TearDown() override
        {
            // GPU の資源は Renderer より先に手放す
            m_assets.reset();
            m_renderer.reset();
            m_window.reset();
        }

        // 自機から見た目と寸法に関わる component だけを抜き出してデータから組む
        // 見た目の component を先に書いても、並びは優先度が決める
        [[nodiscard]] std::unique_ptr<NS::Obj::GameObject> BuildPlayer(const std::string& standingRef,
                                                                       const std::string& ballRef)
        {
            nlohmann::json appearance = NS::Obj::MakeComponentEntry("PlayerAppearance");
            NS::Obj::SetField(appearance, "立ち姿のメッシュ", standingRef);
            NS::Obj::SetField(appearance, "玉のメッシュ", ballRef);
            nlohmann::json capsule = NS::Obj::MakeComponentEntry("CapsuleCollider");
            NS::Obj::SetField(capsule, "半径", k_Radius);
            NS::Obj::SetField(capsule, "半分の高さ", k_HalfHeight);

            nlohmann::json object = NS::Obj::MakeObjectJson();
            NS::Obj::ObjectJsonComponents(object).push_back(appearance);
            NS::Obj::ObjectJsonComponents(object).push_back(NS::Obj::MakeComponentEntry("MeshRenderer"));
            NS::Obj::ObjectJsonComponents(object).push_back(capsule);
            NS::Obj::ObjectJsonComponents(object).push_back(NS::Obj::MakeComponentEntry("PlayerComponent"));
            return NS::Obj::ObjectFromJson(object, m_assets.get());
        }

        std::unique_ptr<NS::Platform::Window> m_window;
        std::unique_ptr<NS::Gfx::Renderer> m_renderer;
        std::unique_ptr<NS::Obj::AssetManager> m_assets;
    };

    [[nodiscard]] const NS::Gfx::Mesh* ShownMesh(NS::Obj::GameObject& object)
    {
        const MeshRenderer* renderer = object.FindComponent<MeshRenderer>();
        if (renderer == nullptr)
        {
            return nullptr;
        }
        return renderer->GetMesh();
    }

    // 回転の試しは mesh を描かないので device が要らない。見た目・描画・移動・溜めの入力だけを積む
    struct SpinRig
    {
        NS::Obj::GameObject object;
        MeshRenderer& renderer = *object.AddComponent<MeshRenderer>();
        PlayerAppearance& appearance = *object.AddComponent<PlayerAppearance>();
        NS::Game::Player::PlayerComponent& player = *object.AddComponent<NS::Game::Player::PlayerComponent>();
        NS::Game::Level::CollisionInput& input = *object.AddComponent<NS::Game::Level::CollisionInput>();

        SpinRig()
        {
            input.OnStart();
            appearance.OnStart();
        }
    };

    // 突進の試しは床の上で本物の突進を出す。+X へ倒して押し、突進に入ったフレームで止める
    // 溜めの入力は押しているかを見せるだけで、発動は試しが PlayerComponent へ直に頼む
    struct SlamRig
    {
        NsTest::EntityStage stage;
        MeshRenderer& renderer = *stage.owner.AddComponent<MeshRenderer>();
        PlayerAppearance& appearance = *stage.owner.AddComponent<PlayerAppearance>();
        NS::Game::Player::PlayerStateManager& manager =
            *stage.owner.AddComponent<NS::Game::Player::PlayerStateManager>();
        NS::Game::Player::PlayerComponent& player = *stage.owner.AddComponent<NS::Game::Player::PlayerComponent>();
        NS::Game::Level::CollisionInput& input = *stage.owner.AddComponent<NS::Game::Level::CollisionInput>();

        SlamRig()
        {
            player.OnStart();
            manager.OnStart();
            input.OnStart();
            appearance.OnStart();
            NsTest::AddBox(stage.physics, NS::Core::AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{64.0f, 0.5f, 64.0f}});
            stage.physics.OptimizeBroadPhase();
            stage.owner.Root().SetPosition(Vector3{0.0f, 1.0f, 0.0f});
            for (int i = 0; i < 30 && !player.IsGrounded(); ++i)
            {
                player.OnUpdate();
            }
            player.OnUpdate();

            player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
            player.RequestBodySlam(1.0f);
            player.OnUpdate();
        }
    };

    // 1 フレームに回る角度 (度)。欄の速さは度/秒
    [[nodiscard]] float DegreesPerFrame(float degreesPerSecond)
    {
        return degreesPerSecond * NS::Platform::FrameTimer::FixedDelta();
    }

    [[nodiscard]] NS::Core::Quaternion TurnDegrees(const Vector3& axis, float degrees)
    {
        return NS::Core::Quaternion::CreateFromAxisAngle(axis, NS::Core::ToRadians(NS::Core::Degrees{degrees}).value);
    }

    void ExpectSameRotation(const NS::Core::Quaternion& actual, const NS::Core::Quaternion& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, 1e-5f);
        EXPECT_NEAR(actual.y, expected.y, 1e-5f);
        EXPECT_NEAR(actual.z, expected.z, 1e-5f);
        EXPECT_NEAR(actual.w, expected.w, 1e-5f);
    }

    void ExpectSameAxis(const Vector3& actual, const Vector3& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, 1e-6f);
        EXPECT_NEAR(actual.y, expected.y, 1e-6f);
        EXPECT_NEAR(actual.z, expected.z, 1e-6f);
    }

    // 真正面の当たりの反動の向き。突進 (+X) と逆の水平
    constexpr Vector3 k_HeadOnRebound{-1.0f, 0.0f, 0.0f};

    // 当たりの検知の後、突進を終えて止めの 1 フレームを過ごす。見た目は止めのフレームを更新し終えている
    void HoldTheHitStop(SlamRig& rig)
    {
        rig.player.CancelBodySlam();
        rig.player.SetActive(false);
        rig.appearance.OnUpdate();
        rig.player.SetActive(true);
    }

    // 止めが明けたフレームに direction の水平へ弾き、自機を 1 フレーム動かす。見た目はこのフレームをまだ更新していない
    // 反動に入れなかった場合 false
    [[nodiscard]] bool ReboundToward(SlamRig& rig, const Vector3& direction)
    {
        const NS::Game::Player::ReboundArc arc{.direction = direction, .apexHeight = 2.15f, .distance = 1.1f};
        if (!rig.player.BeginRebound(arc))
        {
            return false;
        }
        rig.player.OnUpdate();
        return rig.player.IsRebounding();
    }

    // 1 フレームを本番と同じ並びで回す。移動 (Update) → MeshRenderer が前の値を控える (Update) → 見た目 (Update + 50)
    void StepFrame(SlamRig& rig)
    {
        rig.player.OnUpdate();
        rig.renderer.OnUpdate();
        rig.appearance.OnUpdate();
    }

    // 真正面の反動で弾き、反動のまま接地したフレームを回し終えるまで進める。空中の間は潰れていないことも見る
    // 着地した場合 true
    [[nodiscard]] bool FlyToTheReboundLanding(SlamRig& rig)
    {
        rig.appearance.OnUpdate();
        HoldTheHitStop(rig);
        if (!ReboundToward(rig, k_HeadOnRebound))
        {
            return false;
        }
        rig.renderer.OnUpdate();
        rig.appearance.OnUpdate();
        constexpr int k_FrameLimit = 300;
        for (int i = 0; i < k_FrameLimit; ++i)
        {
            EXPECT_EQ(rig.renderer.DrawScale(), Vector3(1.0f, 1.0f, 1.0f)) << i;
            StepFrame(rig);
            if (rig.player.IsRebounding() && rig.player.IsGrounded())
            {
                return true;
            }
        }
        return false;
    }

    // 着地の潰れの縦の倍率。水平は体積を保つ 1 ÷ √縦
    constexpr float k_LandingSquash = 0.8f;
    // 着地の潰れから元の形へ戻すフレーム数
    constexpr int k_RecoverFrames = 6;

    // 着地の潰れから frame フレーム戻した所の縦の倍率
    [[nodiscard]] float RecoveringVertical(int frame)
    {
        return k_LandingSquash +
               (1.0f - k_LandingSquash) * static_cast<float>(frame) / static_cast<float>(k_RecoverFrames);
    }

    void ExpectLandingSquash(const Vector3& scale, float vertical)
    {
        const float horizontal = 1.0f / std::sqrt(vertical);
        EXPECT_NEAR(scale.x, horizontal, 1e-5f);
        EXPECT_NEAR(scale.y, vertical, 1e-5f);
        EXPECT_NEAR(scale.z, horizontal, 1e-5f);
    }

    // 描く形の下端の高さ。玉は中心の周りで回すので縦の半分は変わらず、立ち姿は回さない
    // どちらも縦の半分は境界の縦の半分 × 倍率
    [[nodiscard]] float DrawnBottom(const MeshRenderer& renderer)
    {
        const NS::Core::AABB& bounds = renderer.GetMesh()->LocalBounds();
        const Vector3 center{bounds.Center.x, bounds.Center.y, bounds.Center.z};
        const Vector3 drawnCenter = Vector3::Transform(center, renderer.DrawWorldMatrix(1.0f));
        return drawnCenter.y - bounds.Extents.y * renderer.DrawScale().y;
    }
} // namespace

// 立ち姿は当たりのカプセルと同じ寸法。上端は円柱の半長 + 半径
TEST_F(PlayerAppearanceTest, StandingLookMatchesTheCollisionCapsule)
{
    std::unique_ptr<NS::Obj::GameObject> player = BuildPlayer("", "");
    ASSERT_NE(player, nullptr);
    const CapsuleCollider* capsule = player->FindComponent<CapsuleCollider>();
    ASSERT_NE(capsule, nullptr);

    const NS::Gfx::Mesh* shown = ShownMesh(*player);
    ASSERT_NE(shown, nullptr);
    const PlayerAppearance* appearance = player->FindComponent<PlayerAppearance>();
    ASSERT_NE(appearance, nullptr);
    EXPECT_FALSE(appearance->IsCurled());
    EXPECT_NEAR(shown->LocalBounds().Extents.x, capsule->Radius(), 1e-5f);
    EXPECT_NEAR(shown->LocalBounds().Extents.y, capsule->HalfHeight() + capsule->Radius(), 1e-5f);
    EXPECT_NEAR(shown->LocalBounds().Extents.z, capsule->Radius(), 1e-5f);
}

// 玉の直径は当たりのカプセルの直径と同じ
TEST_F(PlayerAppearanceTest, CurledLookIsABallAsWideAsTheCollisionCapsule)
{
    std::unique_ptr<NS::Obj::GameObject> player = BuildPlayer("", "");
    ASSERT_NE(player, nullptr);
    const CapsuleCollider* capsule = player->FindComponent<CapsuleCollider>();
    PlayerAppearance* appearance = player->FindComponent<PlayerAppearance>();
    ASSERT_NE(capsule, nullptr);
    ASSERT_NE(appearance, nullptr);
    const NS::Gfx::Mesh* standing = ShownMesh(*player);
    ASSERT_NE(standing, nullptr);

    appearance->Curl();

    const NS::Gfx::Mesh* shown = ShownMesh(*player);
    ASSERT_NE(shown, nullptr);
    EXPECT_NE(shown, standing);
    EXPECT_TRUE(appearance->IsCurled());
    EXPECT_NEAR(shown->LocalBounds().Extents.x * 2.0f, capsule->Radius() * 2.0f, 1e-5f);
    EXPECT_NEAR(shown->LocalBounds().Extents.y * 2.0f, capsule->Radius() * 2.0f, 1e-5f);
    EXPECT_NEAR(shown->LocalBounds().Extents.z * 2.0f, capsule->Radius() * 2.0f, 1e-5f);
}

TEST_F(PlayerAppearanceTest, UncurlReturnsToTheStandingLook)
{
    std::unique_ptr<NS::Obj::GameObject> player = BuildPlayer("", "");
    ASSERT_NE(player, nullptr);
    PlayerAppearance* appearance = player->FindComponent<PlayerAppearance>();
    ASSERT_NE(appearance, nullptr);
    const NS::Gfx::Mesh* standing = ShownMesh(*player);
    ASSERT_NE(standing, nullptr);

    appearance->Curl();
    appearance->Uncurl();

    EXPECT_EQ(ShownMesh(*player), standing);
    EXPECT_FALSE(appearance->IsCurled());
}

// 丸まりの正は PlayerComponent が持つ。見た目は毎フレームそれを写す
TEST_F(PlayerAppearanceTest, FollowsTheCurlOfThePlayerComponent)
{
    std::unique_ptr<NS::Obj::GameObject> player = BuildPlayer("", "");
    ASSERT_NE(player, nullptr);
    PlayerAppearance* appearance = player->FindComponent<PlayerAppearance>();
    NS::Game::Player::PlayerComponent* movement = player->FindComponent<NS::Game::Player::PlayerComponent>();
    ASSERT_NE(appearance, nullptr);
    ASSERT_NE(movement, nullptr);
    const NS::Gfx::Mesh* standing = ShownMesh(*player);
    const NS::Gfx::Mesh* ball = m_assets->GetOrMakeCapsuleMesh(k_Radius, 0.0f);
    ASSERT_NE(standing, nullptr);
    ASSERT_NE(ball, nullptr);
    appearance->OnStart();

    movement->SetCurled(true);
    appearance->OnUpdate();
    EXPECT_EQ(ShownMesh(*player), ball);

    movement->SetCurled(false);
    appearance->OnUpdate();
    EXPECT_EQ(ShownMesh(*player), standing);
}

// 丸まっている間に資産を引き直しても、立ち姿は立ち姿の寸法で作る。当たりの半長は玉の間 0 なので、
// それで作ると解いた後の立ち姿が玉のまま残る
TEST_F(PlayerAppearanceTest, ResolvingWhileCurledKeepsTheStandingLookFullHeight)
{
    std::unique_ptr<NS::Obj::GameObject> player = BuildPlayer("", "");
    ASSERT_NE(player, nullptr);
    PlayerAppearance* appearance = player->FindComponent<PlayerAppearance>();
    NS::Game::Player::PlayerComponent* movement = player->FindComponent<NS::Game::Player::PlayerComponent>();
    ASSERT_NE(appearance, nullptr);
    ASSERT_NE(movement, nullptr);
    appearance->OnStart();
    movement->SetCurled(true);
    appearance->OnUpdate();

    appearance->ResolveAssets(*m_assets);
    movement->SetCurled(false);
    appearance->OnUpdate();

    const NS::Gfx::Mesh* shown = ShownMesh(*player);
    ASSERT_NE(shown, nullptr);
    EXPECT_NEAR(shown->LocalBounds().Extents.y, k_HalfHeight + k_Radius, 1e-5f);
}

// 根の位置とスケールは移動と潰れの持ち物。構えで縮んでいる最中に持ち替えても 1 ビットも動かさない
TEST_F(PlayerAppearanceTest, SwappingLooksLeavesTheRootTransformUntouched)
{
    std::unique_ptr<NS::Obj::GameObject> player = BuildPlayer("", "");
    ASSERT_NE(player, nullptr);
    PlayerAppearance* appearance = player->FindComponent<PlayerAppearance>();
    ASSERT_NE(appearance, nullptr);
    const Vector3 position{23.25f, 1.65f, -2.5f};
    const Vector3 squashed{1.0f, 0.95f, 1.0f};
    player->Root().SetPosition(position);
    player->Root().SetScale(squashed);
    const NS::Gfx::Mesh* standing = ShownMesh(*player);
    ASSERT_NE(standing, nullptr);

    appearance->Curl();
    ASSERT_NE(ShownMesh(*player), standing) << "持ち替えが起きていないと、根を書かないことを確かめられない";
    EXPECT_EQ(player->Root().Position(), position);
    EXPECT_EQ(player->Root().Scale(), squashed);

    appearance->Uncurl();
    EXPECT_EQ(player->Root().Position(), position);
    EXPECT_EQ(player->Root().Scale(), squashed);
}

// 本物のモデルは欄に参照を書くだけで載る
TEST_F(PlayerAppearanceTest, MeshRefsInTheFieldsReplaceThePlaceholderShapes)
{
    const std::string standingRef = "Assets/Models/Soldier.glb";
    const std::string ballRef = "Assets/Models/Xbot.glb";
    std::unique_ptr<NS::Obj::GameObject> player = BuildPlayer(standingRef, ballRef);
    ASSERT_NE(player, nullptr);
    PlayerAppearance* appearance = player->FindComponent<PlayerAppearance>();
    ASSERT_NE(appearance, nullptr);
    const NS::Gfx::Mesh* standingFile = NS::Obj::ResolveMeshFromRef(*m_assets, standingRef);
    const NS::Gfx::Mesh* ballFile = NS::Obj::ResolveMeshFromRef(*m_assets, ballRef);
    ASSERT_NE(standingFile, nullptr);
    ASSERT_NE(ballFile, nullptr);

    EXPECT_EQ(ShownMesh(*player), standingFile);
    appearance->Curl();
    EXPECT_EQ(ShownMesh(*player), ballFile);
}

// 引き当てられない参照は描けない自機を作らず、仮の形で描く
TEST_F(PlayerAppearanceTest, UnresolvableMeshRefFallsBackToThePlaceholderShape)
{
    std::unique_ptr<NS::Obj::GameObject> player = BuildPlayer("Assets/Models/__ns_missing_standing__.glb", "");
    ASSERT_NE(player, nullptr);
    const NS::Gfx::Mesh* placeholder = m_assets->GetOrMakeCapsuleMesh(k_Radius, k_HalfHeight);
    ASSERT_NE(placeholder, nullptr);

    EXPECT_EQ(ShownMesh(*player), placeholder);
}

// 押した瞬間から回り、溜めきると欄の速さまで上がる。溜め量は溜めの判定を直接進めて作る
TEST(PlayerAppearanceSpinTest, ChargeRaisesTheSpinFromTheEmptyToTheFullSpeed)
{
    SpinRig rig;
    rig.player.SetCurled(true);

    rig.input.Judge().Step(true);
    ASSERT_FLOAT_EQ(rig.input.Judge().Charge01(), 0.0f);
    rig.appearance.OnUpdate();
    EXPECT_NEAR(rig.appearance.SpinDegreesThisFrame(), DegreesPerFrame(360.0f), 1e-4f);

    for (int i = 1; i < rig.input.Judge().chargeMaxSteps; ++i)
    {
        rig.input.Judge().Step(true);
    }
    ASSERT_FLOAT_EQ(rig.input.Judge().Charge01(), 1.0f);
    rig.appearance.OnUpdate();
    EXPECT_NEAR(rig.appearance.SpinDegreesThisFrame(), DegreesPerFrame(1440.0f), 1e-4f);
}

// 既に回った姿勢の上に、今の軸まわりの回転を足す。軸が変わっても姿勢が跳ばない
TEST(PlayerAppearanceSpinTest, ANewAxisTurnsOnTopOfThePoseAlreadyTurned)
{
    SpinRig rig;
    rig.player.SetCurled(true);
    for (int i = 0; i < rig.input.Judge().chargeMaxSteps; ++i)
    {
        rig.input.Judge().Step(true);
    }
    const float step = DegreesPerFrame(1440.0f);

    // 狙いが +Z なら軸は +X、+X なら -Z
    rig.player.SetDesiredMove(Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    rig.appearance.OnUpdate();
    rig.player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    rig.appearance.OnUpdate();

    const Vector3 up{0.0f, 1.0f, 0.0f};
    const Vector3 firstTurned = Vector3::Transform(up, TurnDegrees(Vector3{1.0f, 0.0f, 0.0f}, step));
    const Vector3 expected = Vector3::Transform(firstTurned, TurnDegrees(Vector3{0.0f, 0.0f, -1.0f}, step));
    const Vector3 actual = Vector3::Transform(up, rig.renderer.LocalRotation());
    EXPECT_NEAR(actual.x, expected.x, 1e-5f);
    EXPECT_NEAR(actual.y, expected.y, 1e-5f);
    EXPECT_NEAR(actual.z, expected.z, 1e-5f);
}

// 狙いが採れないフレーム (ゼロ・非数・無限大) は、前の軸のまま回り続ける
TEST(PlayerAppearanceSpinTest, AnAimThatCannotBeTakenKeepsThePreviousAxis)
{
    SpinRig rig;
    rig.player.SetCurled(true);
    rig.input.Judge().Step(true);
    const float step = DegreesPerFrame(360.0f);

    // 狙いが +X なら軸は -Z。始めの軸 (1, 0, 0) と違う軸にして、保ったのか始めへ戻ったのかを見分ける
    rig.player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    rig.appearance.OnUpdate();
    rig.player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    rig.appearance.OnUpdate();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    rig.player.SetDesiredMove(Vector3{nan, 0.0f, nan}, 1.0f);
    rig.appearance.OnUpdate();
    const float infinity = std::numeric_limits<float>::infinity();
    rig.player.SetDesiredMove(Vector3{infinity, 0.0f, 0.0f}, 1.0f);
    rig.appearance.OnUpdate();

    ExpectSameRotation(rig.renderer.LocalRotation(), TurnDegrees(Vector3{0.0f, 0.0f, -1.0f}, step * 4.0f));
}

// 突進中は進む向きに直交する水平の軸まわりに、転がる速さで前へ回る
TEST(PlayerAppearanceSpinTest, BodySlamRollsForwardAboutTheHorizontalAxisAcrossTheTravel)
{
    SlamRig rig;
    ASSERT_TRUE(rig.player.IsBodySlamming());
    ASSERT_TRUE(rig.player.IsCurled());
    rig.appearance.OnUpdate();

    // 進む向き (1, 0, 0) から (forward.z, 0, -forward.x) = (0, 0, -1)
    EXPECT_NEAR(rig.appearance.SpinDegreesThisFrame(), DegreesPerFrame(1800.0f), 1e-4f);
    ExpectSameRotation(rig.renderer.LocalRotation(), TurnDegrees(Vector3{0.0f, 0.0f, -1.0f}, DegreesPerFrame(1800.0f)));
    // 式を写しただけでは符号ごと逆でも通る。上面が進む向き (+X) の側へ倒れることを見る
    const Vector3 top = Vector3::Transform(Vector3{0.0f, 1.0f, 0.0f}, rig.renderer.LocalRotation());
    EXPECT_GT(top.x, 0.0f);
}

// 反動に入らずに突進が終わった時は、丸まっている間、突進の軸と速さのまま転がり続ける
TEST(PlayerAppearanceSpinTest, AfterTheBodySlamEndsTheBallKeepsRollingTheSameWay)
{
    SlamRig rig;
    ASSERT_TRUE(rig.player.IsBodySlamming());
    rig.appearance.OnUpdate();

    rig.player.CancelBodySlam();
    ASSERT_FALSE(rig.player.IsBodySlamming());
    ASSERT_TRUE(rig.player.IsCurled());
    rig.appearance.OnUpdate();

    EXPECT_NEAR(rig.appearance.SpinDegreesThisFrame(), DegreesPerFrame(1800.0f), 1e-4f);
    ExpectSameRotation(rig.renderer.LocalRotation(),
                       TurnDegrees(Vector3{0.0f, 0.0f, -1.0f}, DegreesPerFrame(1800.0f) * 2.0f));
}

// 真正面の当たりでは、止めが明けたフレームに軸が突進の軸と逆を向き、そのフレームから突進と同じ速さで回る
TEST(PlayerAppearanceSpinTest, TheBallTurnsTheOtherWayOnTheFrameTheHitStopEnds)
{
    SlamRig rig;
    ASSERT_TRUE(rig.player.IsBodySlamming());
    rig.appearance.OnUpdate();
    const Vector3 rushAxis = rig.appearance.SpinAxis();
    // 進む向き (1, 0, 0) から (forward.z, 0, -forward.x) = (0, 0, -1)
    ExpectSameAxis(rushAxis, Vector3{0.0f, 0.0f, -1.0f});
    const NS::Core::Quaternion before = rig.renderer.LocalRotation();

    HoldTheHitStop(rig);
    // 止めの間は突進の軸のまま。切り替わるのは明けのフレーム
    ExpectSameAxis(rig.appearance.SpinAxis(), rushAxis);
    ASSERT_TRUE(ReboundToward(rig, k_HeadOnRebound));
    rig.appearance.OnUpdate();

    EXPECT_LT(rig.appearance.SpinAxis().Dot(rushAxis), 0.0f);
    EXPECT_NEAR(rig.appearance.SpinDegreesThisFrame(), DegreesPerFrame(1800.0f), 1e-4f);
    // 弾かれる向き (-1, 0, 0) から (0, 0, 1)。止めの間は回らないので、突進の 1 フレームの上に明けの 1 フレームが乗る
    ExpectSameRotation(rig.renderer.LocalRotation(),
                       before * TurnDegrees(Vector3{0.0f, 0.0f, 1.0f}, DegreesPerFrame(1800.0f)));
}

// 横ずれのある当たりでは、弾かれた向きから軸を取る。突進の軸を裏返した軸とは違う
TEST(PlayerAppearanceSpinTest, AnOffCenterHitTurnsTheBallAlongTheReboundNotAgainstTheRush)
{
    SlamRig rig;
    ASSERT_TRUE(rig.player.IsBodySlamming());
    rig.appearance.OnUpdate();
    const Vector3 rushAxis = rig.appearance.SpinAxis();

    HoldTheHitStop(rig);
    ASSERT_TRUE(ReboundToward(rig, Vector3{-1.0f, 0.0f, 1.0f}));
    rig.appearance.OnUpdate();

    // 弾かれる向き (-1, 0, 1) / √2 から (forward.z, 0, -forward.x) = (1, 0, 1) / √2
    const float half = 1.0f / std::sqrt(2.0f);
    ExpectSameAxis(rig.appearance.SpinAxis(), Vector3{half, 0.0f, half});
    // 突進の軸を裏返すだけなら (0, 0, 1) になる
    EXPECT_LT(rig.appearance.SpinAxis().Dot(-rushAxis), 0.9f);
}

// 反動の間は空中で横へ倒して速度の向きが変わっても、弾かれた向きから取った軸のまま回る
TEST(PlayerAppearanceSpinTest, TiltingDuringTheReboundKeepsTheAxis)
{
    SlamRig rig;
    ASSERT_TRUE(rig.player.IsBodySlamming());
    rig.appearance.OnUpdate();
    HoldTheHitStop(rig);
    ASSERT_TRUE(ReboundToward(rig, k_HeadOnRebound));
    rig.appearance.OnUpdate();
    const NS::Core::Quaternion start = rig.renderer.LocalRotation();

    constexpr int k_Frames = 10;
    rig.player.SetDesiredMove(Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int i = 0; i < k_Frames; ++i)
    {
        rig.player.OnUpdate();
        ASSERT_TRUE(rig.player.IsRebounding());
        rig.appearance.OnUpdate();
    }
    // 倒した向きが速度に効いていることを先に見る。効いていなければ軸が変わらないのは当たり前になる
    ASSERT_GT(rig.player.Velocity().z, 0.1f);

    ExpectSameAxis(rig.appearance.SpinAxis(), Vector3{0.0f, 0.0f, 1.0f});
    ExpectSameRotation(rig.renderer.LocalRotation(),
                       start * TurnDegrees(Vector3{0.0f, 0.0f, 1.0f}, DegreesPerFrame(1800.0f) * k_Frames));
}

// 反動の空中で 1 発を出すと、その突進の進む向きへ前転する
TEST(PlayerAppearanceSpinTest, AnAirShotFromTheReboundRollsAlongItsOwnTravel)
{
    SlamRig rig;
    ASSERT_TRUE(rig.player.IsBodySlamming());
    rig.appearance.OnUpdate();
    HoldTheHitStop(rig);
    ASSERT_TRUE(ReboundToward(rig, k_HeadOnRebound));
    rig.appearance.OnUpdate();

    // 突進の軸 (0, 0, -1) とも反動の軸 (0, 0, 1) とも違う向きへ出し、どちらの軸が残ったのでもないことを見分ける
    rig.player.SetDesiredMove(Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    rig.player.RequestBodySlam(0.0f);
    rig.player.OnUpdate();
    ASSERT_TRUE(rig.player.IsBodySlamming());
    rig.appearance.OnUpdate();

    // 進む向き (0, 0, 1) から (1, 0, 0)
    ExpectSameAxis(rig.appearance.SpinAxis(), Vector3{1.0f, 0.0f, 0.0f});
    EXPECT_NEAR(rig.appearance.SpinDegreesThisFrame(), DegreesPerFrame(1800.0f), 1e-4f);
}

// 反動の間に押している間は、弾かれた向きでなく狙いへ向けて溜めの速さで回る。溜め量の見え方は反動の中でも変わらない
TEST(PlayerAppearanceSpinTest, HoldingDuringTheReboundSpinsTowardTheAim)
{
    SlamRig rig;
    ASSERT_TRUE(rig.player.IsBodySlamming());
    rig.appearance.OnUpdate();
    HoldTheHitStop(rig);
    ASSERT_TRUE(ReboundToward(rig, k_HeadOnRebound));

    rig.player.SetDesiredMove(Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    rig.input.Judge().Step(true);
    ASSERT_FLOAT_EQ(rig.input.Judge().Charge01(), 0.0f);
    rig.appearance.OnUpdate();

    // 狙い (0, 0, 1) から (1, 0, 0)
    ExpectSameAxis(rig.appearance.SpinAxis(), Vector3{1.0f, 0.0f, 0.0f});
    EXPECT_NEAR(rig.appearance.SpinDegreesThisFrame(), DegreesPerFrame(360.0f), 1e-4f);
}

// 立ち姿に戻ったフレームは、前のフレームの回転も捨てる。補間の途中でも立ち姿が傾いて描かれない
TEST(PlayerAppearanceSpinTest, StandingUpDrawsTheStandingLookUpright)
{
    SpinRig rig;
    rig.player.SetCurled(true);
    rig.input.Judge().Step(true);
    rig.renderer.OnUpdate();
    rig.appearance.OnUpdate();
    ASSERT_GT(rig.appearance.SpinDegreesThisFrame(), 0.0f);

    // 1 フレームの中の並びと同じく、MeshRenderer が前の値を控えてから見た目が書く
    rig.player.SetCurled(false);
    rig.renderer.OnUpdate();
    rig.appearance.OnUpdate();

    EXPECT_FLOAT_EQ(rig.appearance.SpinDegreesThisFrame(), 0.0f);
    const NS::Core::Matrix drawn = rig.renderer.DrawWorldMatrix(0.5f);
    const NS::Core::Matrix root = rig.object.Root().InterpolatedWorldMatrix(0.5f);
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            EXPECT_NEAR(drawn.m[row][column], root.m[row][column], 1e-6f) << row << "," << column;
        }
    }
}

// 立ち姿は玉の軸と速さを捨てる。丸まり直すと、押すまでは回らず、押すと始めの軸から回る
TEST(PlayerAppearanceSpinTest, CurlingAgainStartsStillAboutTheFirstAxis)
{
    SpinRig rig;
    rig.player.SetCurled(true);
    rig.player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    rig.input.Judge().Step(true);
    rig.appearance.OnUpdate();
    rig.player.SetCurled(false);
    rig.input.Judge().Step(false);
    rig.appearance.OnUpdate();

    rig.player.SetCurled(true);
    rig.player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    rig.appearance.OnUpdate();
    EXPECT_FLOAT_EQ(rig.appearance.SpinDegreesThisFrame(), 0.0f);

    rig.input.Judge().Step(true);
    rig.appearance.OnUpdate();
    ExpectSameRotation(rig.renderer.LocalRotation(), TurnDegrees(Vector3{1.0f, 0.0f, 0.0f}, DegreesPerFrame(360.0f)));
}

// MeshRenderer が前のフレームの回転を控えてから、見た目が今の回転を書く。逆の並びでは前 = 今になり補間が消える
TEST(PlayerAppearanceSpinTest, WritesTheRotationAfterTheRendererKeepsThePreviousOne)
{
    SpinRig rig;
    EXPECT_LT(rig.renderer.Priority(), rig.appearance.Priority());
}

// 当たりの止めで移動が止まっている間は、絵も止まる
TEST(PlayerAppearanceSpinTest, FreezeHoldsTheSpin)
{
    SpinRig rig;
    rig.player.SetCurled(true);
    rig.input.Judge().Step(true);
    rig.appearance.OnUpdate();
    const NS::Core::Quaternion before = rig.renderer.LocalRotation();

    rig.player.SetActive(false);
    rig.input.Judge().Step(true);
    rig.appearance.OnUpdate();

    EXPECT_FLOAT_EQ(rig.appearance.SpinDegreesThisFrame(), 0.0f);
    ExpectSameRotation(rig.renderer.LocalRotation(), before);
}

// 反動のまま接地したフレームに、縦 0.8・水平 1 ÷ √0.8 へ潰れ、6 フレームで (1, 1, 1) ちょうどへ戻る
// 次のフレームに反動が明けても潰れは続く。根のスケールは変えない
TEST(PlayerAppearanceLandingTest, ReboundLandingSquashesThenRestoresInSixFrames)
{
    SlamRig rig;
    ASSERT_TRUE(rig.player.IsBodySlamming());
    const Vector3 rootScale = rig.stage.owner.Root().Scale();
    ASSERT_TRUE(FlyToTheReboundLanding(rig));

    ExpectLandingSquash(rig.renderer.DrawScale(), k_LandingSquash);
    EXPECT_EQ(rig.stage.owner.Root().Scale(), rootScale);

    for (int frame = 1; frame < k_RecoverFrames; ++frame)
    {
        StepFrame(rig);
        ASSERT_FALSE(rig.player.IsRebounding());
        ExpectLandingSquash(rig.renderer.DrawScale(), RecoveringVertical(frame));
        EXPECT_EQ(rig.stage.owner.Root().Scale(), rootScale);
    }
    StepFrame(rig);
    EXPECT_EQ(rig.renderer.DrawScale(), Vector3(1.0f, 1.0f, 1.0f));
    StepFrame(rig);
    EXPECT_EQ(rig.renderer.DrawScale(), Vector3(1.0f, 1.0f, 1.0f));
}

// 跳びの着地は潰さない。潰れは反動の着地だけの絵
TEST(PlayerAppearanceLandingTest, JumpLandingDoesNotSquash)
{
    SlamRig rig;
    rig.player.CancelBodySlam();
    rig.player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    StepFrame(rig);
    ASSERT_TRUE(rig.player.IsGrounded());

    rig.player.SetJumpPressed();
    rig.player.SetJumpHeld(true);
    StepFrame(rig);
    ASSERT_FALSE(rig.player.IsGrounded());

    constexpr int k_FrameLimit = 300;
    int landedAt = -1;
    for (int i = 0; i < k_FrameLimit && landedAt < 0; ++i)
    {
        StepFrame(rig);
        EXPECT_EQ(rig.renderer.DrawScale(), Vector3(1.0f, 1.0f, 1.0f)) << i;
        if (rig.player.IsGrounded())
        {
            landedAt = i;
        }
    }
    ASSERT_GE(landedAt, 0);
    for (int i = 0; i < k_RecoverFrames; ++i)
    {
        StepFrame(rig);
        EXPECT_EQ(rig.renderer.DrawScale(), Vector3(1.0f, 1.0f, 1.0f)) << i;
    }
}

// 当たりの止めで移動が止まっている間は、着地の潰れの戻しも止まる。明ければ残りのフレーム数から続ける
TEST(PlayerAppearanceLandingTest, FreezeHoldsTheLandingSquash)
{
    SlamRig rig;
    ASSERT_TRUE(FlyToTheReboundLanding(rig));
    constexpr int k_FramesBeforeTheFreeze = 2;
    for (int frame = 1; frame <= k_FramesBeforeTheFreeze; ++frame)
    {
        StepFrame(rig);
    }
    const Vector3 held = rig.renderer.DrawScale();
    ExpectLandingSquash(held, RecoveringVertical(k_FramesBeforeTheFreeze));

    // 止めの間は移動が回らない。MeshRenderer と見た目だけが回る
    rig.player.SetActive(false);
    for (int i = 0; i < 4; ++i)
    {
        rig.renderer.OnUpdate();
        rig.appearance.OnUpdate();
        EXPECT_EQ(rig.renderer.DrawScale(), held) << i;
    }
    rig.player.SetActive(true);

    for (int frame = k_FramesBeforeTheFreeze + 1; frame < k_RecoverFrames; ++frame)
    {
        StepFrame(rig);
        ExpectLandingSquash(rig.renderer.DrawScale(), RecoveringVertical(frame));
    }
    StepFrame(rig);
    EXPECT_EQ(rig.renderer.DrawScale(), Vector3(1.0f, 1.0f, 1.0f));
}

// 戻すフレーム数が 0 なら、反動の着地でも潰さない
TEST(PlayerAppearanceLandingTest, ZeroRecoverFramesDoesNotSquash)
{
    SlamRig rig;
    const NS::Obj::FieldDesc* recoverFrames =
        NS::Obj::FindField(rig.appearance.GetReflection(), "着地の潰れを戻すフレーム数");
    ASSERT_NE(recoverFrames, nullptr);
    const int zero = 0;
    recoverFrames->set(&rig.appearance, &zero);

    ASSERT_TRUE(FlyToTheReboundLanding(rig));
    EXPECT_EQ(rig.renderer.DrawScale(), Vector3(1.0f, 1.0f, 1.0f));
    for (int i = 0; i < k_RecoverFrames; ++i)
    {
        StepFrame(rig);
        EXPECT_EQ(rig.renderer.DrawScale(), Vector3(1.0f, 1.0f, 1.0f)) << i;
    }
}

// 着地のフレームの玉も、次のフレームに持ち替えた立ち姿も、潰れた描く形の下端は潰れていない形の下端 (床) のまま
TEST_F(PlayerAppearanceTest, ReboundLandingSquashKeepsTheDrawnBottomOnTheFloorAcrossTheLookSwap)
{
    SlamRig rig;
    rig.appearance.ResolveAssets(*m_assets);
    ASSERT_TRUE(FlyToTheReboundLanding(rig));
    ASSERT_TRUE(rig.appearance.IsCurled());
    ASSERT_NE(rig.renderer.GetMesh(), nullptr);
    const NS::Core::AABB& ballBounds = rig.renderer.GetMesh()->LocalBounds();
    const float ballBottom = rig.stage.owner.Root().Position().y + ballBounds.Center.y - ballBounds.Extents.y;
    ASSERT_LT(rig.renderer.DrawScale().y, 1.0f);
    EXPECT_NEAR(DrawnBottom(rig.renderer), ballBottom, 1e-4f);

    StepFrame(rig);
    ASSERT_FALSE(rig.appearance.IsCurled());
    ASSERT_LT(rig.renderer.DrawScale().y, 1.0f);
    const NS::Core::AABB& standingBounds = rig.renderer.GetMesh()->LocalBounds();
    const float standingBottom =
        rig.stage.owner.Root().Position().y + standingBounds.Center.y - standingBounds.Extents.y;
    EXPECT_NEAR(DrawnBottom(rig.renderer), standingBottom, 1e-4f);
    // 持ち替えても下端は同じ床の上
    EXPECT_NEAR(standingBottom, ballBottom, 1e-3f);
}
