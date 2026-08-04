#include <Game/Level/HealthComponent.h>
#include <Game/Player.h>
#include <gtest/gtest.h>
#include <Runtime/Object/Components/CharacterMovementComponent.h>
#include <Runtime/Object/Components/MeshRendererComponent.h>
#include <Runtime/Object/Components/PlayerInputComponent.h>
#include <Runtime/Object/Components/ShadowComponent.h>
#include <Runtime/Object/Components/TransformComponent.h>

TEST(PlayerTest, ConstructsWithDefaultComposition)
{
    Player player{};
    // 器が積む transform に素の 5 つ (mesh / movement / input / health / shadow) と判定への応答 4 つ
    // (fade / respawner / finisher / area camera) を足した 10 つ。 デバッグ表示は出荷では積まれない
    std::size_t expected = 10u;
#if !defined(NS_SHIPPING)
    expected += 1u; // コヨーテ時間のデバッグ描画
#endif
    EXPECT_EQ(player.Components().size(), expected);
}

TEST(PlayerTest, DefaultComponentsResolveByType)
{
    Player player{};
    // priority 昇順 + 同 priority 内は宣言順:
    //   [0] input     (Input,      0)
    //   [1] transform (Update,   200) — 器のコンストラクタが最初に積む
    //   [2] mesh      (Update,   200) — コンストラクタで movement より前に登録
    //   [3] movement  (Update,   200)
    //   [4] health    (Update,   200) — movement の後に登録
    //   [5] shadow    (Update,   200) — 同 priority 内で最後に登録
    // 応答はこの後ろの LateUpdate 帯に並ぶ (fade +5、 respawner / finisher / area camera +10)
    EXPECT_EQ(player.FindComponent<NS::Object::PlayerInputComponent>(), player.Components()[0]);
    EXPECT_EQ(player.FindComponent<NS::Object::TransformComponent>(), player.Components()[1]);
    EXPECT_EQ(player.FindComponent<NS::Object::MeshRendererComponent>(), player.Components()[2]);
    EXPECT_EQ(player.FindComponent<NS::Object::CharacterMovementComponent>(), player.Components()[3]);
    EXPECT_EQ(player.FindComponent<NS::Game::Level::HealthComponent>(), player.Components()[4]);
    EXPECT_EQ(player.FindComponent<NS::Object::ShadowComponent>(), player.Components()[5]);
}

TEST(PlayerTest, InputComponentResolvesMovementOnStart)
{
    Player player{};
    player.OnStart();
    auto* input = player.FindComponent<NS::Object::PlayerInputComponent>();
    ASSERT_NE(input, nullptr);
    EXPECT_EQ(input->Movement(), player.FindComponent<NS::Object::CharacterMovementComponent>());
}
