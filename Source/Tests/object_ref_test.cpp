#include <Runtime/Object/Components/ThirdPersonFollow.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Object.h>
#include <Runtime/Object/ObjectList.h>
#include <Runtime/Object/Reflection/ObjectRef.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Scene/Scene.h>
#include <gtest/gtest.h>

#include "tuning_field_access.h"

namespace
{
    using NS::Obj::GameObject;
    using NS::Obj::ObjectIdAccess;
    using NS::Obj::ObjectRef;
    using NS::Obj::Scene;
    using NS::Obj::ObjectList;
} // namespace

TEST(ObjectRefTest, ResolvesByPersistentId)
{
    ObjectList objects;
    GameObject* object = objects.Spawn<GameObject>();
    ObjectIdAccess::SetId(*object, 7u);

    EXPECT_EQ(objects.FindObject(ObjectRef{7u}), object);
    // 未設定 0 と該当なしの id は nullptr
    EXPECT_EQ(objects.FindObject(ObjectRef{}), nullptr);
    EXPECT_EQ(objects.FindObject(ObjectRef{8u}), nullptr);

    // 索引は所有リストそのものなので、破棄すれば参照は引けなくなる
    objects.Clear();
    EXPECT_EQ(objects.FindObject(ObjectRef{7u}), nullptr);
}

TEST(ObjectRefTest, UnnumberedObjectIsNeverResolved)
{
    ObjectList objects;
    // 一時オブジェクトと同じ未採番の状態。0 を素通しすると先頭のこれが引けてしまう
    objects.Spawn<GameObject>();

    EXPECT_EQ(objects.FindObject(ObjectRef{0u}), nullptr);
    EXPECT_EQ(objects.FindObject(ObjectRef{9u}), nullptr);
}

TEST(ObjectRefTest, FollowResolvesTargetByRef)
{
    Scene scene;
    GameObject* target = scene.Objects().Spawn<GameObject>();
    ObjectIdAccess::SetId(*target, 5u);

    GameObject rig;
    rig.AttachScene(&scene);
    auto* follow = rig.AddComponent<NS::Obj::ThirdPersonFollow>();
    NsTest::WriteObjectRefField(*follow, "追従対象", 5u);

    EXPECT_EQ(follow->Target(), &target->Root());
    EXPECT_EQ(follow->TargetRef().id, 5u);
}

TEST(ObjectRefTest, FollowHasNoTargetWhenRefUnset)
{
    Scene scene;
    GameObject rig;
    rig.AttachScene(&scene);
    auto* follow = rig.AddComponent<NS::Obj::ThirdPersonFollow>();

    EXPECT_EQ(follow->Target(), nullptr);
}

TEST(ObjectRefTest, FollowHasNoTargetWhenRefDangling)
{
    Scene scene;
    GameObject rig;
    rig.AttachScene(&scene);
    auto* follow = rig.AddComponent<NS::Obj::ThirdPersonFollow>();
    NsTest::WriteObjectRefField(*follow, "追従対象", 123u);

    EXPECT_EQ(follow->Target(), nullptr);
}

// 追う相手をポインタで控えないので、相手だけが先に消えても消えた相手を指し続けない
TEST(ObjectRefTest, FollowLosesTargetDestroyedLater)
{
    Scene scene;
    GameObject* target = scene.Objects().Spawn<GameObject>();
    ObjectIdAccess::SetId(*target, 5u);

    GameObject rig;
    rig.AttachScene(&scene);
    auto* follow = rig.AddComponent<NS::Obj::ThirdPersonFollow>();
    NsTest::WriteObjectRefField(*follow, "追従対象", 5u);
    ASSERT_EQ(follow->Target(), &target->Root());

    scene.DestroyObject(5u);

    EXPECT_EQ(follow->Target(), nullptr);
}
