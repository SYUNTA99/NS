#include <gtest/gtest.h>

#include <Framework/Scene/Components/CharacterMovementComponent.h>
#include <Framework/Scene/Components/ThirdPersonFollowComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/ObjectRef.h>
#include <Framework/Scene/ObjectRefSubsystem.h>
#include <Framework/Scene/Reflection.h>
#include <Framework/Scene/SceneBase.h>

namespace
{
    using NS::Scene::GameObject;
    using NS::Scene::ObjectRef;
    using NS::Scene::ObjectRefSubsystem;
    using NS::Scene::SceneBase;

    /// 反射フィールド名で ObjectRef を書き込む。 データ経由の構築と同じ set 経路を通す
    void SetTargetRef(NS::Scene::Component& comp, std::uint32_t id)
    {
        const NS::Scene::ReflectionInfo* info = comp.GetReflection();
        ASSERT_NE(info, nullptr);
        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            if (std::string_view{info->fields[i].name} != "Target")
                continue;
            const ObjectRef ref{id};
            info->fields[i].set(&comp, &ref);
            return;
        }
        FAIL() << "Target フィールドが反射に無い";
    }
} // namespace

TEST(ObjectRefSubsystemTest, RegisterAndResolve)
{
    SceneBase scene;
    scene.CreateSceneSubsystems();
    auto* refs = scene.GetSubsystem<ObjectRefSubsystem>();
    ASSERT_NE(refs, nullptr);

    GameObject object;
    refs->Register(7u, &object);

    EXPECT_EQ(refs->Resolve(ObjectRef{7u}), &object);
    // 未設定 0 と未登録 id は nullptr
    EXPECT_EQ(refs->Resolve(ObjectRef{}), nullptr);
    EXPECT_EQ(refs->Resolve(ObjectRef{8u}), nullptr);

    refs->Clear();
    EXPECT_EQ(refs->Resolve(ObjectRef{7u}), nullptr);
}

TEST(ObjectRefSubsystemTest, IgnoresInvalidRegistration)
{
    SceneBase scene;
    scene.CreateSceneSubsystems();
    auto* refs = scene.GetSubsystem<ObjectRefSubsystem>();
    ASSERT_NE(refs, nullptr);

    GameObject object;
    // id 0 は未設定の印なので登録できない。null 実体も無視する
    refs->Register(0u, &object);
    refs->Register(9u, nullptr);
    EXPECT_EQ(refs->Resolve(ObjectRef{0u}), nullptr);
    EXPECT_EQ(refs->Resolve(ObjectRef{9u}), nullptr);
}

TEST(ObjectRefSubsystemTest, FollowResolvesTargetAndMovementOnStart)
{
    SceneBase scene;
    scene.CreateSceneSubsystems();
    auto* refs = scene.GetSubsystem<ObjectRefSubsystem>();
    ASSERT_NE(refs, nullptr);

    // 追従先: 移動 component 持ちの実体を id 5 で照合窓口へ登録する
    GameObject target;
    auto* movement = target.AddComponent<NS::Scene::CharacterMovementComponent>();
    refs->Register(5u, &target);

    GameObject rig;
    rig.AttachScene(&scene);
    auto* follow = rig.AddComponent<NS::Scene::ThirdPersonFollowComponent>(nullptr);
    SetTargetRef(*follow, 5u);

    rig.OnStart();

    // OnStart が参照を解決し、追従 Transform と自動ズーム用 Movement の両方が結ばれる
    EXPECT_EQ(follow->Target(), &target.Root());
    EXPECT_EQ(follow->Movement(), movement);
    EXPECT_EQ(follow->TargetRef().id, 5u);
}

TEST(ObjectRefSubsystemTest, FollowKeepsDirectWiringWhenRefUnset)
{
    SceneBase scene;
    scene.CreateSceneSubsystems();

    // 参照未設定なら直結線を触らない。基準軌跡テスト等の直組みが従来どおり動く前提
    GameObject player;
    GameObject rig;
    rig.AttachScene(&scene);
    auto* follow = rig.AddComponent<NS::Scene::ThirdPersonFollowComponent>(&player.Root());

    rig.OnStart();

    EXPECT_EQ(follow->Target(), &player.Root());
    EXPECT_FALSE(follow->TargetRef().IsSet());
}

TEST(ObjectRefSubsystemTest, FollowKeepsWiringWhenRefDangling)
{
    SceneBase scene;
    scene.CreateSceneSubsystems();

    // 参照はあるが照合窓口に実体が無い。解決失敗でも既存の結線を壊さない
    GameObject player;
    GameObject rig;
    rig.AttachScene(&scene);
    auto* follow = rig.AddComponent<NS::Scene::ThirdPersonFollowComponent>(&player.Root());
    SetTargetRef(*follow, 123u);

    rig.OnStart();

    EXPECT_EQ(follow->Target(), &player.Root());
}
