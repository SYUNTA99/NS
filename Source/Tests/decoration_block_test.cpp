#include <gtest/gtest.h>

#include <Framework/Scene/Components/HazardComponent.h>
#include <Framework/Scene/Components/MeshRendererComponent.h>
#include <Framework/Scene/Components/BoxColliderComponent.h>
#include <Game/Blocks/DecorationBlock.h>
#include <Game/Blocks/HazardBlock.h>
#include <Game/Blocks/WaterBlock.h>
#include <Game/Blocks/BlockRegistry.h>

#include <algorithm>

namespace
{
    using NS::Game::Blocks::IsCollidable;
    using NS::Game::Blocks::kBlockIdDecoration;
    using NS::Game::Blocks::kBlockIdHazard;
    using NS::Game::Blocks::kBlockIdWater;
} // namespace

TEST(DecorationBlockTest, NoColliderAttached)
{
    DecorationBlock deco(nullptr, nullptr);

    // GameObject::Components() に登録された Component を走査し、
    // BoxColliderComponent 型が混入していないことを保証する。 dynamic_cast を使うのは
    // 「ヘッダから collider 型が消えていることを runtime でも検証する」 ための明示的な型確認
    const auto& components = deco.Components();
    for (const auto* comp : components)
    {
        const auto* collider = dynamic_cast<const NS::Scene::BoxColliderComponent*>(comp);
        EXPECT_EQ(collider, nullptr);
    }
}

TEST(DecorationBlockTest, WaterBlockHasNoCollider)
{
    WaterBlock water(nullptr, nullptr);
    const auto& components = water.Components();
    for (const auto* comp : components)
    {
        const auto* collider = dynamic_cast<const NS::Scene::BoxColliderComponent*>(comp);
        EXPECT_EQ(collider, nullptr);
    }
}

TEST(DecorationBlockTest, HazardBlockHasColliderAndHazardComponent)
{
    HazardBlock hazard(nullptr, nullptr, NS::Math::Vector3{0.5f, 0.5f, 0.5f});
    const auto& components = hazard.Components();

    bool hasCollider = false;
    bool hasHazard = false;
    for (const auto* comp : components)
    {
        if (dynamic_cast<const NS::Scene::BoxColliderComponent*>(comp) != nullptr)
            hasCollider = true;
        if (dynamic_cast<const NS::Scene::HazardComponent*>(comp) != nullptr)
            hasHazard = true;
    }
    EXPECT_TRUE(hasCollider);
    EXPECT_TRUE(hasHazard);
}

TEST(DecorationBlockTest, IsCollidableReturnsFalseForDecorationAndWater)
{
    EXPECT_FALSE(IsCollidable(kBlockIdDecoration));
    EXPECT_FALSE(IsCollidable(kBlockIdWater));
    EXPECT_TRUE(IsCollidable(kBlockIdHazard));
}
