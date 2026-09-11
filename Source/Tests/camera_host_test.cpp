#include <Runtime/Object/Component.h>
#include <Runtime/Object/Components/CameraBrainComponent.h>
#include <Runtime/Object/Components/CameraComponent.h>
#include <Runtime/Object/Components/PlacedVirtualCamera.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Scene/SceneData.h>
#include <Runtime/Object/World.h>
#include <gtest/gtest.h>

#include <cmath>
#include <utility>

namespace
{
    using NS::Object::Scene;

    // 据え置きカメラを 1 体だけ持つレベル。 組み立ては AssetManager 不在でも通る
    NS::Object::SceneData MakePlacedCameraLevel()
    {
        NS::Object::SceneData data;
        NS::Object::ObjectData object{};
        object.components.push_back(NS::Object::MakeComponentEntry("PlacedVirtualCamera"));
        data.objects.push_back(std::move(object));
        NS::Object::EnsureUniqueObjectIds(data);
        return data;
    }

    NS::Object::PlacedVirtualCamera* FindPlaced(Scene& scene)
    {
        NS::Object::PlacedVirtualCamera* found = nullptr;
        scene.World().ForEachComponent<NS::Object::PlacedVirtualCamera>(
            [&found](NS::Object::PlacedVirtualCamera& placed) {
                if (found == nullptr)
                    found = &placed;
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

    // シーンを畳むと host ごと消え、 読み口は nullptr を返す
    scene.OnShutdown();
    EXPECT_EQ(scene.CameraBrain(), nullptr);
    EXPECT_EQ(scene.MainCamera(), nullptr);
}

TEST(CameraHost, BrainRunsInTheLateUpdateBand)
{
    Scene scene;
    ASSERT_NE(scene.CameraBrain(), nullptr);
    // vcam を供給する follow / placed の LateUpdate + 50 より後ろに居る
    EXPECT_EQ(scene.CameraBrain()->Priority(), NS::Object::TickPriority::LateUpdate + 60);

    scene.LoadFromData(MakePlacedCameraLevel());
    NS::Object::PlacedVirtualCamera* placed = FindPlaced(scene);
    ASSERT_NE(placed, nullptr);
    placed->SetActive(true);
    ASSERT_EQ(scene.CameraBrain()->ActiveVirtualCamera(), nullptr);

    // 帯が brain を回すので、 進行から手で呼ぶ 1 行は要らない
    scene.OnUpdate();

    EXPECT_EQ(scene.CameraBrain()->ActiveVirtualCamera(), placed);
}

TEST(CameraHost, SurvivesRebuildAndRebindsVirtualCameras)
{
    Scene scene;
    NS::Object::CameraBrainComponent* brainBefore = scene.CameraBrain();
    ASSERT_NE(brainBefore, nullptr);
    const NS::Object::GameObject* hostBefore = brainBefore->Owner();

    scene.LoadFromData(MakePlacedCameraLevel());
    // データから組み直しても同じ host が残る
    EXPECT_EQ(scene.CameraBrain(), brainBefore);
    EXPECT_EQ(scene.CameraBrain()->Owner(), hostBefore);

    NS::Object::PlacedVirtualCamera* first = FindPlaced(scene);
    ASSERT_NE(first, nullptr);
    first->SetActive(true);
    scene.OnUpdate();
    ASSERT_EQ(scene.CameraBrain()->ActiveVirtualCamera(), first);

    // 2 回目の組み直しで古い登録が外れ、 新しい実体が選ばれる
    scene.LoadFromData(MakePlacedCameraLevel());
    NS::Object::PlacedVirtualCamera* second = FindPlaced(scene);
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
    scene.LoadFromData(MakePlacedCameraLevel());
    NS::Object::PlacedVirtualCamera* placed = FindPlaced(scene);
    ASSERT_NE(placed, nullptr);
    placed->SetActive(true);
    placed->SetView(NS::Core::Vector3{0.0f, 3.0f, -6.0f}, NS::Core::Vector3{0.0f, 1.0f, 0.0f});
    scene.OnUpdate();

    NS::Object::CameraBrainComponent* brain = scene.CameraBrain();
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
    scene.LoadFromData(MakePlacedCameraLevel());
    NS::Object::PlacedVirtualCamera* placed = FindPlaced(scene);
    ASSERT_NE(placed, nullptr);
    placed->SetActive(true);
    placed->SetView(NS::Core::Vector3{0.0f, 3.0f, -6.0f}, NS::Core::Vector3{0.0f, 1.0f, 0.0f});
    scene.OnUpdate();

    NS::Object::CameraBrainComponent* brain = scene.CameraBrain();
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

// 揺れは渡した歩数の中で減衰し切り、明けた後に尾を引かない
TEST(CameraHost, ShakeEndsWithinSteps)
{
    Scene scene;
    scene.LoadFromData(MakePlacedCameraLevel());
    NS::Object::PlacedVirtualCamera* placed = FindPlaced(scene);
    ASSERT_NE(placed, nullptr);
    placed->SetActive(true);
    placed->SetView(NS::Core::Vector3{0.0f, 3.0f, -6.0f}, NS::Core::Vector3{0.0f, 1.0f, 0.0f});
    scene.OnUpdate();

    NS::Object::CameraBrainComponent* brain = scene.CameraBrain();
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

// 壊れた値は受け取らない。負の振れ幅と 0 歩は揺れを始めない
TEST(CameraHost, ShakeRejectsBrokenInput)
{
    Scene scene;
    scene.LoadFromData(MakePlacedCameraLevel());
    NS::Object::PlacedVirtualCamera* placed = FindPlaced(scene);
    ASSERT_NE(placed, nullptr);
    placed->SetActive(true);
    placed->SetView(NS::Core::Vector3{0.0f, 3.0f, -6.0f}, NS::Core::Vector3{0.0f, 1.0f, 0.0f});
    scene.OnUpdate();

    NS::Object::CameraBrainComponent* brain = scene.CameraBrain();
    brain->Evaluate(1.0f);
    const NS::Core::Vector3 before = brain->LastPose().position;

    brain->StartShake(-1.0f, 4);
    brain->Evaluate(1.0f);
    EXPECT_FLOAT_EQ(brain->LastPose().position.y, before.y);

    brain->StartShake(0.1f, 0);
    brain->Evaluate(1.0f);
    EXPECT_FLOAT_EQ(brain->LastPose().position.y, before.y);
}
