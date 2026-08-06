#include <Runtime/Object/Components/CharacterMovementComponent.h>
#include <Runtime/Object/Components/ThirdPersonFollowComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Object.h>
#include <Runtime/Object/Reflection/ObjectRef.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/World.h>
#include <gtest/gtest.h>

namespace
{
    using NS::Object::GameObject;
    using NS::Object::ObjectIdAccess;
    using NS::Object::ObjectRef;
    using NS::Object::Scene;
    using NS::Object::World;

    /// リフレクションフィールド名で ObjectRef を書き込む。 データ経由の構築と同じ set 経路を通す
    void SetTargetRef(NS::Object::Component& comp, std::uint32_t id)
    {
        const NS::Object::ReflectionInfo* info = comp.GetReflection();
        ASSERT_NE(info, nullptr);
        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            if (std::string_view{info->fields[i].name} != "追従対象")
                continue;
            const ObjectRef ref{id};
            info->fields[i].set(&comp, &ref);
            return;
        }
        FAIL() << "追従対象フィールドがリフレクションに無い";
    }
} // namespace

TEST(ObjectRefTest, ResolvesByPersistentId)
{
    World world;
    GameObject* object = world.Spawn<GameObject>();
    ObjectIdAccess::SetId(*object, 7u);

    EXPECT_EQ(world.FindObject(ObjectRef{7u}), object);
    // 未設定 0 と該当なしの id は nullptr
    EXPECT_EQ(world.FindObject(ObjectRef{}), nullptr);
    EXPECT_EQ(world.FindObject(ObjectRef{8u}), nullptr);

    // 索引は所有リストそのものなので、 破棄すれば参照は引けなくなる
    world.Clear();
    EXPECT_EQ(world.FindObject(ObjectRef{7u}), nullptr);
}

TEST(ObjectRefTest, UnnumberedObjectIsNeverResolved)
{
    World world;
    // 一時オブジェクトと同じ未採番の状態。 0 を素通しすると先頭のこれが引けてしまう
    world.Spawn<GameObject>();

    EXPECT_EQ(world.FindObject(ObjectRef{0u}), nullptr);
    EXPECT_EQ(world.FindObject(ObjectRef{9u}), nullptr);
}

TEST(ObjectRefTest, FollowResolvesTargetAndMovementOnStart)
{
    Scene scene;

    // 追従先: 移動 component 持ちの実体を id 5 で world へ入れる
    GameObject* target = scene.World().Spawn<GameObject>();
    ObjectIdAccess::SetId(*target, 5u);
    auto* movement = target->AddComponent<NS::Object::CharacterMovementComponent>();

    GameObject rig;
    rig.AttachScene(&scene);
    auto* follow = rig.AddComponent<NS::Object::ThirdPersonFollowComponent>();
    SetTargetRef(*follow, 5u);

    rig.OnStart();

    // OnStart が参照を解決し、追従 Transform と自動ズーム用 Movement の両方が結ばれる
    EXPECT_EQ(follow->Target(), &target->Root());
    EXPECT_EQ(follow->Movement(), movement);
    EXPECT_EQ(follow->TargetRef().id, 5u);
}

TEST(ObjectRefTest, FollowKeepsDirectWiringWhenRefUnset)
{
    Scene scene;

    // 参照未設定なら直結線を触らない。基準軌跡テスト等の直組みが従来どおり動く前提
    GameObject player;
    GameObject rig;
    rig.AttachScene(&scene);
    auto* follow = rig.AddComponent<NS::Object::ThirdPersonFollowComponent>();
    follow->SetTarget(&player.Root());

    rig.OnStart();

    EXPECT_EQ(follow->Target(), &player.Root());
    EXPECT_FALSE(follow->TargetRef().IsSet());
}

TEST(ObjectRefTest, FollowKeepsWiringWhenRefDangling)
{
    Scene scene;

    // 参照はあるが world に該当 id が居ない。解決失敗でも既存の結線を壊さない
    GameObject player;
    GameObject rig;
    rig.AttachScene(&scene);
    auto* follow = rig.AddComponent<NS::Object::ThirdPersonFollowComponent>();
    follow->SetTarget(&player.Root());
    SetTargetRef(*follow, 123u);

    rig.OnStart();

    EXPECT_EQ(follow->Target(), &player.Root());
}
