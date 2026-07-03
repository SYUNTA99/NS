#include <gtest/gtest.h>

#include <Framework/Scene/CameraSubsystem.h>
#include <Framework/Scene/Components/CameraBrainComponent.h>
#include <Framework/Scene/Components/CameraComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/SceneBase.h>

namespace
{
    using NS::Scene::CameraBrainComponent;
    using NS::Scene::CameraComponent;
    using NS::Scene::CameraSubsystem;
    using NS::Scene::GameObject;
    using NS::Scene::SceneBase;
} // namespace

TEST(CameraSubsystemTest, CreatedOnAnyScene)
{
    SceneBase scene;
    scene.CreateSceneSubsystems();

    // どのシーンにもカメラ 1 系統。生成と同時に実カメラ + Brain の host が立ち上がる
    CameraSubsystem* subsystem = scene.GetSubsystem<CameraSubsystem>();
    ASSERT_NE(subsystem, nullptr);
    EXPECT_NE(subsystem->Brain(), nullptr);
    EXPECT_NE(subsystem->MainCamera(), nullptr);
}

TEST(CameraSubsystemTest, MainCameraResolvesThroughBrain)
{
    SceneBase scene;
    scene.CreateSceneSubsystems();
    auto* subsystem = scene.GetSubsystem<CameraSubsystem>();
    ASSERT_NE(subsystem, nullptr);

    // 実カメラは Brain と同じ host に載り、窓口の 2 つが同じ実体系を指す
    ASSERT_NE(subsystem->Brain(), nullptr);
    EXPECT_EQ(subsystem->MainCamera(), subsystem->Brain()->Camera());
    EXPECT_EQ(subsystem->Brain()->Owner(), subsystem->MainCamera()->Owner());
}

TEST(CameraSubsystemTest, DeinitializeReleasesHost)
{
    SceneBase scene;
    scene.CreateSceneSubsystems();
    auto* subsystem = scene.GetSubsystem<CameraSubsystem>();
    ASSERT_NE(subsystem, nullptr);
    ASSERT_NE(subsystem->Brain(), nullptr);

    // シーン破棄経路で host ごと畳まれ、窓口は無カメラを返す
    subsystem->Deinitialize();
    EXPECT_EQ(subsystem->Brain(), nullptr);
    EXPECT_EQ(subsystem->MainCamera(), nullptr);
}
