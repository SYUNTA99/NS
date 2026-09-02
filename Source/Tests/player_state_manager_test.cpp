#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerStateManagerComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <gtest/gtest.h>

#include <string>

namespace
{
    using NS::Game::Player::PlayerComponent;
    using NS::Game::Player::PlayerStateManagerComponent;
    using NS::Object::FieldDesc;
    using NS::Object::FieldType;
    using NS::Object::GameObject;

    constexpr float k_FixedDt = 1.0f / 60.0f;
    constexpr const char* k_StateListField = "状態一覧";
    constexpr const char* k_DefaultStateList = "Idle;Walk;Fall;LedgeHanging;LedgeClimbing;BodySlam";

    const FieldDesc* FindField(const NS::Object::ReflectionInfo* info, const char* name)
    {
        if (info == nullptr)
            return nullptr;
        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            if (std::string(info->fields[i].name) == name)
                return &info->fields[i];
        }
        return nullptr;
    }

    std::string ReadStateList(PlayerStateManagerComponent& manager)
    {
        const FieldDesc* field = FindField(manager.GetReflection(), k_StateListField);
        if (field == nullptr)
            return {};
        std::string out;
        field->get(&manager, &out);
        return out;
    }

    void WriteStateList(PlayerStateManagerComponent& manager, const std::string& list)
    {
        const FieldDesc* field = FindField(manager.GetReflection(), k_StateListField);
        ASSERT_NE(field, nullptr);
        field->set(&manager, &list);
    }

    // 組んだ直後を見たい所は Step でなく EnsureBuilt を呼ぶ。Step は状態を 1 歩走らせるので、
    // 床の無いこの検証台では立ちから落下へ移ってしまう
    struct Rig
    {
        GameObject owner;
        PlayerStateManagerComponent* manager = nullptr;
        PlayerComponent* player = nullptr;

        Rig()
        {
            manager = owner.AddComponent<PlayerStateManagerComponent>();
            player = owner.AddComponent<PlayerComponent>();
            player->SetDebugDrawEnabled(false);
            player->OnStart();
            manager->OnStart();
        }
    };
} // namespace

TEST(PlayerStateManagerTest, StateListDefaultsToTheSixNames)
{
    Rig rig;

    EXPECT_EQ(ReadStateList(*rig.manager), k_DefaultStateList);
}

TEST(PlayerStateManagerTest, NothingIsBuiltBeforeTheFirstStep)
{
    Rig rig;

    EXPECT_FALSE(rig.manager->IsBuilt());
    EXPECT_STREQ(rig.manager->CurrentName(), "");
}

TEST(PlayerStateManagerTest, BuildingEntersTheFirstNameInTheList)
{
    Rig rig;

    rig.manager->EnsureBuilt(*rig.player);

    EXPECT_TRUE(rig.manager->IsBuilt());
    EXPECT_STREQ(rig.manager->CurrentName(), PlayerComponent::k_IdleStateName);
}

TEST(PlayerStateManagerTest, FirstStepBuildsTheMachine)
{
    Rig rig;

    rig.manager->Step(*rig.player, k_FixedDt);

    EXPECT_TRUE(rig.manager->IsBuilt());
}

TEST(PlayerStateManagerTest, UnknownNamesAreSkipped)
{
    Rig rig;
    WriteStateList(*rig.manager, "Idle;無い状態;Fall");

    rig.manager->EnsureBuilt(*rig.player);

    ASSERT_TRUE(rig.manager->IsBuilt());
    EXPECT_STREQ(rig.manager->CurrentName(), PlayerComponent::k_IdleStateName);
    EXPECT_TRUE(rig.manager->ChangeByName("Fall"));
    EXPECT_FALSE(rig.manager->ChangeByName("Walk"));
}

TEST(PlayerStateManagerTest, AllUnknownNamesFallBackToTheDefaultList)
{
    Rig rig;
    WriteStateList(*rig.manager, "無い状態;もう無い状態");

    rig.manager->EnsureBuilt(*rig.player);

    ASSERT_TRUE(rig.manager->IsBuilt());
    EXPECT_STREQ(rig.manager->CurrentName(), PlayerComponent::k_IdleStateName);
    EXPECT_TRUE(rig.manager->ChangeByName(PlayerComponent::k_BodySlamStateName));
}

TEST(PlayerStateManagerTest, ChangeByNameMovesToTheNamedState)
{
    Rig rig;
    rig.manager->EnsureBuilt(*rig.player);

    EXPECT_TRUE(rig.manager->ChangeByName("Fall"));
    EXPECT_STREQ(rig.manager->CurrentName(), "Fall");
}

TEST(PlayerStateManagerTest, ResetToFirstReturnsToTheFirstState)
{
    Rig rig;
    rig.manager->EnsureBuilt(*rig.player);
    ASSERT_TRUE(rig.manager->ChangeByName("Fall"));

    rig.manager->ResetToFirst();

    EXPECT_STREQ(rig.manager->CurrentName(), PlayerComponent::k_IdleStateName);
}

TEST(PlayerStateManagerTest, ChangeByNameFailsBeforeTheMachineIsBuilt)
{
    GameObject owner;
    PlayerStateManagerComponent& manager = *owner.AddComponent<PlayerStateManagerComponent>();

    EXPECT_FALSE(manager.ChangeByName("Fall"));
}

TEST(PlayerStateManagerTest, ReflectedStateListIsReadableAndWritable)
{
    Rig rig;
    const FieldDesc* field = FindField(rig.manager->GetReflection(), k_StateListField);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->type, FieldType::String);

    WriteStateList(*rig.manager, "Idle;Fall");

    EXPECT_EQ(ReadStateList(*rig.manager), "Idle;Fall");
    rig.manager->EnsureBuilt(*rig.player);
    EXPECT_FALSE(rig.manager->ChangeByName("Walk"));
}
