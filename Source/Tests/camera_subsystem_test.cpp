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

    CameraSubsystem* subsystem = scene.GetSubsystem<CameraSubsystem>();
    ASSERT_NE(subsystem, nullptr);
    // 未登録の間は両窓口とも nullptr で、消費者が無カメラを判定できる
    EXPECT_EQ(subsystem->Brain(), nullptr);
    EXPECT_EQ(subsystem->MainCamera(), nullptr);
}

TEST(CameraSubsystemTest, BrainRegistrationRoundTrip)
{
    SceneBase scene;
    scene.CreateSceneSubsystems();
    auto* subsystem = scene.GetSubsystem<CameraSubsystem>();
    ASSERT_NE(subsystem, nullptr);

    GameObject host;
    auto* brain = host.AddComponent<CameraBrainComponent>();

    subsystem->SetBrain(brain);
    EXPECT_EQ(subsystem->Brain(), brain);

    subsystem->SetBrain(nullptr);
    EXPECT_EQ(subsystem->Brain(), nullptr);
}

TEST(CameraSubsystemTest, MainCameraResolvesThroughBrain)
{
    SceneBase scene;
    scene.CreateSceneSubsystems();
    auto* subsystem = scene.GetSubsystem<CameraSubsystem>();
    ASSERT_NE(subsystem, nullptr);

    GameObject host;
    auto* cam = host.AddComponent<CameraComponent>();
    auto* brain = host.AddComponent<CameraBrainComponent>();
    host.OnStart();

    subsystem->SetBrain(brain);
    EXPECT_EQ(subsystem->MainCamera(), cam);
}

TEST(CameraSubsystemTest, DeinitializeReleasesBrainReference)
{
    SceneBase scene;
    scene.CreateSceneSubsystems();
    auto* subsystem = scene.GetSubsystem<CameraSubsystem>();
    ASSERT_NE(subsystem, nullptr);

    GameObject host;
    subsystem->SetBrain(host.AddComponent<CameraBrainComponent>());

    // シーン破棄経路で参照が残らないこと。brain 実体の寿命は所有者側の管轄
    subsystem->Deinitialize();
    EXPECT_EQ(subsystem->Brain(), nullptr);
}
