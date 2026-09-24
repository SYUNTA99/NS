#include <Runtime/Object/Component.h>
#include <Runtime/Object/Components/CameraBrain.h>
#include <Runtime/Object/Components/CameraComponent.h>
#include <Runtime/Object/Components/ThirdPersonFollow.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/ObjectList.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Scene/SceneJson.h>
#include <gtest/gtest.h>

#include <cmath>
#include <utility>

namespace
{
    using NS::Obj::Scene;

    // 追従カメラを 1 体だけ持つレベル。追う相手が無いので固定の既定視点を返す。組み立ては AssetManager 不在でも通る
    nlohmann::json MakeCameraLevel()
    {
        nlohmann::json data = NS::Obj::MakeSceneJson();
        nlohmann::json object = NS::Obj::MakeObjectJson();
        NS::Obj::ObjectJsonComponents(object).push_back(NS::Obj::MakeComponentEntry("ThirdPersonFollow"));
        NS::Obj::SceneJsonObjects(data).push_back(std::move(object));
        NS::Obj::EnsureUniqueObjectIds(data);
        return data;
    }

    NS::Obj::ThirdPersonFollow* FindCamera(Scene& scene)
    {
        NS::Obj::ThirdPersonFollow* found = nullptr;
        scene.Objects().ForEachComponent<NS::Obj::ThirdPersonFollow>([&found](NS::Obj::ThirdPersonFollow& follow) {
            if (found == nullptr)
                found = &follow;
        });
        return found;
    }
} // namespace

TEST(CameraHost, EverySceneHasCameraAndBrain)
{
    Scene scene;

    // どのシーンにも実カメラ + Brain が 1 組ある
    EXPECT_NE(scene.CameraBrain(), nullptr);
    EXPECT_NE(scene.MainCamera(), nullptr);
}

TEST(CameraHost, MainCameraLivesOnTheBrainObject)
{
    Scene scene;
    ASSERT_NE(scene.CameraBrain(), nullptr);

    // 2 つの読み口が同じ配置物の上の同じカメラを指す
    EXPECT_EQ(scene.MainCamera(), scene.CameraBrain()->Camera());
    EXPECT_EQ(scene.CameraBrain()->Owner(), scene.MainCamera()->Owner());
}

TEST(CameraHost, TeardownDropsTheHost)
{
    Scene scene;
    ASSERT_NE(scene.CameraBrain(), nullptr);

    // シーンを畳むと host ごと消え、読み口は nullptr を返す
    scene.OnShutdown();
    EXPECT_EQ(scene.CameraBrain(), nullptr);
    EXPECT_EQ(scene.MainCamera(), nullptr);
}

TEST(CameraHost, BrainRunsInTheLateUpdateBand)
{
    Scene scene;
    ASSERT_NE(scene.CameraBrain(), nullptr);
    // vcam を供給する追従カメラの LateUpdate + 50 より後ろに居る
    EXPECT_EQ(scene.CameraBrain()->Priority(), NS::Obj::TickPriority::LateUpdate + 60);

    scene.LoadJson(MakeCameraLevel());
    NS::Obj::ThirdPersonFollow* camera = FindCamera(scene);
    ASSERT_NE(camera, nullptr);
    camera->SetActive(true);
    ASSERT_EQ(scene.CameraBrain()->ActiveVirtualCamera(), nullptr);

    // 帯が brain を回すので、進行から手で呼ぶ 1 行は要らない
    scene.OnUpdate();

    EXPECT_EQ(scene.CameraBrain()->ActiveVirtualCamera(), camera);
}

TEST(CameraHost, SurvivesRebuildAndRebindsVirtualCameras)
{
    Scene scene;
    NS::Obj::CameraBrain* brainBefore = scene.CameraBrain();
    ASSERT_NE(brainBefore, nullptr);
    const NS::Obj::GameObject* hostBefore = brainBefore->Owner();

    scene.LoadJson(MakeCameraLevel());
    // データから組み直しても同じ host が残る
    EXPECT_EQ(scene.CameraBrain(), brainBefore);
    EXPECT_EQ(scene.CameraBrain()->Owner(), hostBefore);

    NS::Obj::ThirdPersonFollow* first = FindCamera(scene);
    ASSERT_NE(first, nullptr);
    first->SetActive(true);
    scene.OnUpdate();
    ASSERT_EQ(scene.CameraBrain()->ActiveVirtualCamera(), first);

    // 2 回目の組み直しで古い登録が外れ、新しい実体が選ばれる
    scene.LoadJson(MakeCameraLevel());
    NS::Obj::ThirdPersonFollow* second = FindCamera(scene);
    ASSERT_NE(second, nullptr);
    // アドレス比較はしない。解放直後の再確保が同じ番地を返すと、新しい実体でも偽で赤になる
    // 古い実体が残っていれば active のままここに出る。null は登録が外れて作り直された証拠
    EXPECT_EQ(scene.CameraBrain()->ActiveVirtualCamera(), nullptr);

    second->SetActive(true);
    scene.OnUpdate();
    EXPECT_EQ(scene.CameraBrain()->ActiveVirtualCamera(), second);
}

// 衝突の揺れ。vcam の pose は全部 Brain を通って実カメラへ書かれるので、どの vcam が選ばれていても一様に掛かる
TEST(CameraHost, ShakeOffsetsFinalPose)
{
    Scene scene;
    scene.LoadJson(MakeCameraLevel());
    NS::Obj::ThirdPersonFollow* camera = FindCamera(scene);
    ASSERT_NE(camera, nullptr);
    camera->SetActive(true);
    scene.OnUpdate();

    NS::Obj::CameraBrain* brain = scene.CameraBrain();
    ASSERT_NE(brain, nullptr);
    brain->Evaluate(1.0f);
    const NS::Core::Vector3 before = brain->LastPose().position;

    brain->StartShake(0.1f, 4);
    brain->Evaluate(1.0f);
    const NS::Core::Vector3 during = brain->LastPose().position;

    EXPECT_GT(std::abs(during.y - before.y), 1.0e-4f);
}

// 揺れは position と target を同じだけ平行移動する。視線が回らないので camera 相対入力に波及しない
TEST(CameraHost, ShakeTranslatesViewWithoutTurning)
{
    Scene scene;
    scene.LoadJson(MakeCameraLevel());
    NS::Obj::ThirdPersonFollow* camera = FindCamera(scene);
    ASSERT_NE(camera, nullptr);
    camera->SetActive(true);
    scene.OnUpdate();

    NS::Obj::CameraBrain* brain = scene.CameraBrain();
    brain->Evaluate(1.0f);
    const NS::Core::Vector3 lookBefore = brain->LastPose().target - brain->LastPose().position;
    const NS::Core::Vector3 forwardBefore = brain->ForwardHorizontal();

    brain->StartShake(0.1f, 4);
    brain->Evaluate(1.0f);
    const NS::Core::Vector3 lookDuring = brain->LastPose().target - brain->LastPose().position;
    const NS::Core::Vector3 forwardDuring = brain->ForwardHorizontal();

    EXPECT_FLOAT_EQ(lookDuring.x, lookBefore.x);
    EXPECT_FLOAT_EQ(lookDuring.y, lookBefore.y);
    EXPECT_FLOAT_EQ(lookDuring.z, lookBefore.z);
    EXPECT_FLOAT_EQ(forwardDuring.x, forwardBefore.x);
    EXPECT_FLOAT_EQ(forwardDuring.z, forwardBefore.z);
}

// 揺れは渡したフレーム数の中で減衰し切り、明けた後に尾を引かない
TEST(CameraHost, ShakeEndsWithinSteps)
{
    Scene scene;
    scene.LoadJson(MakeCameraLevel());
    NS::Obj::ThirdPersonFollow* camera = FindCamera(scene);
    ASSERT_NE(camera, nullptr);
    camera->SetActive(true);
    scene.OnUpdate();

    NS::Obj::CameraBrain* brain = scene.CameraBrain();
    brain->Evaluate(1.0f);
    const NS::Core::Vector3 before = brain->LastPose().position;

    brain->StartShake(0.1f, 3);
    brain->OnUpdate();
    brain->Evaluate(1.0f);
    EXPECT_GT(std::abs(brain->LastPose().position.y - before.y), 1.0e-4f);

    brain->OnUpdate();
    brain->OnUpdate();
    brain->Evaluate(1.0f);
    EXPECT_FLOAT_EQ(brain->LastPose().position.y, before.y);
}

// 壊れた値は受け取らない。負の振れ幅と 0 フレームは揺れを始めない
TEST(CameraHost, ShakeRejectsBrokenInput)
{
    Scene scene;
    scene.LoadJson(MakeCameraLevel());
    NS::Obj::ThirdPersonFollow* camera = FindCamera(scene);
    ASSERT_NE(camera, nullptr);
    camera->SetActive(true);
    scene.OnUpdate();

    NS::Obj::CameraBrain* brain = scene.CameraBrain();
    brain->Evaluate(1.0f);
    const NS::Core::Vector3 before = brain->LastPose().position;

    brain->StartShake(-1.0f, 4);
    brain->Evaluate(1.0f);
    EXPECT_FLOAT_EQ(brain->LastPose().position.y, before.y);

    brain->StartShake(0.1f, 0);
    brain->Evaluate(1.0f);
    EXPECT_FLOAT_EQ(brain->LastPose().position.y, before.y);
}
