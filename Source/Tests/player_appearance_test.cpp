#include "Game/Player/PlayerAppearance.h"

#include <Game/Level/CollisionInput.h>
#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerStateManager.h>
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
#include <Runtime/Object/Transform.h>
#include <Runtime/Platform/Clock.h>
#include <Runtime/Platform/Filesystem.h>
#include <Runtime/Platform/Window.h>

#include "entity_test_stage.h"
#include "jolt_test_scene.h"
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
    struct SlamRig
    {
        NsTest::EntityStage stage;
        MeshRenderer& renderer = *stage.owner.AddComponent<MeshRenderer>();
        PlayerAppearance& appearance = *stage.owner.AddComponent<PlayerAppearance>();
        NS::Game::Player::PlayerStateManager& manager =
            *stage.owner.AddComponent<NS::Game::Player::PlayerStateManager>();
        NS::Game::Player::PlayerComponent& player = *stage.owner.AddComponent<NS::Game::Player::PlayerComponent>();

        SlamRig()
        {
            player.OnStart();
            manager.OnStart();
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

// 突進が終わっても丸まっている間は、突進の軸と速さのまま転がり続ける
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

// 立ち姿に戻ったフレームは、前のフレームの回転も捨てる。補間の途中でも立ち姿が傾いて描かれない
TEST(PlayerAppearanceSpinTest, StandingUpDrawsTheStandingLookUpright)
{
    SpinRig rig;
    rig.player.SetCurled(true);
    rig.input.Judge().Step(true);
    rig.renderer.OnUpdate();
    rig.appearance.OnUpdate();
    ASSERT_GT(rig.appearance.SpinDegreesThisFrame(), 0.0f);

    // 1 フレームの中の並びと同じく、描く側が前の値を控えてから見た目が書く
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

// 描く側が前のフレームの回転を控えてから、見た目が今の回転を書く。逆の並びでは前 = 今になり補間が消える
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
