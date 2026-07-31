#include <gtest/gtest.h>
#include <Runtime/Object/CameraSubsystem.h>
#include <Runtime/Object/Components/CameraBrainComponent.h>
#include <Runtime/Object/Components/CameraComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Scene/Scene.h>

namespace
{
    using NS::Object::CameraBrainComponent;
    using NS::Object::CameraComponent;
    using NS::Object::CameraSubsystem;
    using NS::Object::GameObject;
    using NS::Object::Scene;
} // namespace

TEST(CameraSubsystemTest, CreatedOnAnyScene)
{
    Scene scene;
    scene.CreateSceneSubsystems();

    // どのシーンでも生成と同時に実カメラ + Brain の host が立ち上がる
    CameraSubsystem* subsystem = scene.GetSubsystem<CameraSubsystem>();
    ASSERT_NE(subsystem, nullptr);
    EXPECT_NE(subsystem->Brain(), nullptr);
    EXPECT_NE(subsystem->MainCamera(), nullptr);
}

TEST(CameraSubsystemTest, MainCameraResolvesThroughBrain)
{
    Scene scene;
    scene.CreateSceneSubsystems();
    auto* subsystem = scene.GetSubsystem<CameraSubsystem>();
    ASSERT_NE(subsystem, nullptr);

    // MainCamera と Brain の 2 窓口が同じ host 上のカメラを指す
    ASSERT_NE(subsystem->Brain(), nullptr);
    EXPECT_EQ(subsystem->MainCamera(), subsystem->Brain()->Camera());
    EXPECT_EQ(subsystem->Brain()->Owner(), subsystem->MainCamera()->Owner());
}

TEST(CameraSubsystemTest, DeinitializeReleasesHost)
{
    Scene scene;
    scene.CreateSceneSubsystems();
    auto* subsystem = scene.GetSubsystem<CameraSubsystem>();
    ASSERT_NE(subsystem, nullptr);
    ASSERT_NE(subsystem->Brain(), nullptr);

    // シーン破棄で host ごと畳まれ、窓口は nullptr を返す
    subsystem->Deinitialize();
    EXPECT_EQ(subsystem->Brain(), nullptr);
    EXPECT_EQ(subsystem->MainCamera(), nullptr);
}
