#include "Game/Player/PlayerAppearance.h"

#include <Game/Player/PlayerComponent.h>
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
#include <Runtime/Platform/Filesystem.h>
#include <Runtime/Platform/Window.h>

#include <gtest/gtest.h>
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
