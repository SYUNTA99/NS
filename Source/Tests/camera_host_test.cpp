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
    ASSERT_NE(second, first);
    EXPECT_EQ(scene.CameraBrain()->ActiveVirtualCamera(), nullptr);

    second->SetActive(true);
    scene.OnUpdate();
    EXPECT_EQ(scene.CameraBrain()->ActiveVirtualCamera(), second);
}
