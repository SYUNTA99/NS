#include <gtest/gtest.h>

#include <Framework/Scene/HazardComponent.h>
#include <Framework/Scene/MeshComponent.h>
#include <Framework/Scene/StaticColliderComponent.h>
#include <Game/Blocks/DecorationBlock.h>
#include <Game/Blocks/HazardBlock.h>
#include <Game/Blocks/WaterBlock.h>
#include <Game/Editor/BlockRegistry.h>

#include <algorithm>

namespace
{
    using NS::Game::Editor::IsCollidable;
    using NS::Game::Editor::kBlockIdDecoration;
    using NS::Game::Editor::kBlockIdHazard;
    using NS::Game::Editor::kBlockIdWater;
} // namespace

TEST(DecorationBlockTest, NoColliderAttached)
{
    DecorationBlock deco(nullptr, nullptr);

    // GameObject::Components() に登録された Component を走査し、
    // StaticColliderComponent 型が混入していないことを保証する。 dynamic_cast を使うのは
    // 「ヘッダから collider 型が消えていることを runtime でも検証する」 ための明示的な型確認。
    const auto& components = deco.Components();
    for (const auto* comp : components)
    {
        const auto* collider = dynamic_cast<const NS::Scene::StaticColliderComponent*>(comp);
        EXPECT_EQ(collider, nullptr);
    }
}

TEST(DecorationBlockTest, WaterBlockHasNoCollider)
{
    WaterBlock water(nullptr, nullptr);
    const auto& components = water.Components();
    for (const auto* comp : components)
    {
        const auto* collider = dynamic_cast<const NS::Scene::StaticColliderComponent*>(comp);
        EXPECT_EQ(collider, nullptr);
    }
}

TEST(DecorationBlockTest, HazardBlockHasColliderAndHazardComponent)
{
    HazardBlock hazard(nullptr, nullptr, NS::Core::Vector3{0.5f, 0.5f, 0.5f});
    const auto& components = hazard.Components();

    bool hasCollider = false;
    bool hasHazard = false;
    for (const auto* comp : components)
    {
        if (dynamic_cast<const NS::Scene::StaticColliderComponent*>(comp) != nullptr)
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
