#include <gtest/gtest.h>

#include <Framework/Core/Logger.h>
#include <Framework/Graphics/Camera.h>
#include <Framework/Graphics/RenderSettings.h>
#include <Framework/Graphics/Renderer.h>
#include <Framework/Platform/Window.h>
#include <Framework/Scene/EnvironmentSubsystem.h>
#include <Framework/Scene/SceneBase.h>

#include <filesystem>

namespace
{
    using NS::Scene::EnvironmentSettings;
    using NS::Scene::EnvironmentSubsystem;
    using NS::Scene::SceneBase;

    constexpr float kEpsilon = 1e-5f;

    /// protected の ResolveSceneSettings を試験用に公開した最小派生
    class ResolveProbeScene : public SceneBase
    {
    public:
        NS::Graphics::RenderSettings CallResolve(const NS::Graphics::RenderSettings& defaults)
        {
            return ResolveSceneSettings(defaults);
        }
    };

    EnvironmentSettings MakeSettings()
    {
        EnvironmentSettings s{};
        s.lightDirection = NS::Math::Vector3{0.0f, -1.0f, 0.5f};
        s.lightColor = NS::Math::Vector3{0.9f, 0.8f, 0.7f};
        s.ambientColor = NS::Math::Vector3{0.1f, 0.2f, 0.3f};
        s.skyboxCubemapPath = "Assets/Skybox/kurt/";
        return s;
    }
} // namespace

class EnvironmentSubsystemTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(EnvironmentSubsystemTest, CreatedOnAnyScene)
{
    SceneBase scene;
    scene.CreateSceneSubsystems();

    // どのシーンにも環境 1 系統。設定の出所が居なくても窓口は立つ
    EXPECT_NE(scene.GetSubsystem<EnvironmentSubsystem>(), nullptr);
}

TEST_F(EnvironmentSubsystemTest, SettingsRoundTrip)
{
    SceneBase scene;
    scene.CreateSceneSubsystems();
    auto* environment = scene.GetSubsystem<EnvironmentSubsystem>();
    ASSERT_NE(environment, nullptr);

    environment->SetSettings(MakeSettings());
    const EnvironmentSettings& s = environment->Settings();
    EXPECT_NEAR(s.lightDirection.y, -1.0f, kEpsilon);
    EXPECT_NEAR(s.lightDirection.z, 0.5f, kEpsilon);
    EXPECT_NEAR(s.lightColor.x, 0.9f, kEpsilon);
    EXPECT_NEAR(s.ambientColor.z, 0.3f, kEpsilon);
    EXPECT_EQ(s.skyboxCubemapPath, std::filesystem::path("Assets/Skybox/kurt/"));
}

TEST_F(EnvironmentSubsystemTest, FreshOverrideIsEmpty)
{
    SceneBase scene;
    scene.CreateSceneSubsystems();
    auto* environment = scene.GetSubsystem<EnvironmentSubsystem>();
    ASSERT_NE(environment, nullptr);

    // 設定が書かれるまで上書きを宣言せず、環境を書かないシーンの見た目を変えない
    const NS::Graphics::RenderSettingsOverride over = environment->BuildOverride();
    EXPECT_FALSE(over.lightDir.has_value());
    EXPECT_FALSE(over.lightColor.has_value());
    EXPECT_FALSE(over.ambientColor.has_value());
    EXPECT_FALSE(over.clearColor.has_value());
}

TEST_F(EnvironmentSubsystemTest, BuildOverrideMapsLighting)
{
    SceneBase scene;
    scene.CreateSceneSubsystems();
    auto* environment = scene.GetSubsystem<EnvironmentSubsystem>();
    ASSERT_NE(environment, nullptr);

    environment->SetSettings(MakeSettings());
    const NS::Graphics::RenderSettingsOverride over = environment->BuildOverride();
    ASSERT_TRUE(over.lightDir.has_value());
    EXPECT_NEAR(over.lightDir->z, 0.5f, kEpsilon);
    ASSERT_TRUE(over.lightColor.has_value());
    EXPECT_NEAR(over.lightColor->x, 0.9f, kEpsilon);
    ASSERT_TRUE(over.ambientColor.has_value());
    EXPECT_NEAR(over.ambientColor->z, 0.3f, kEpsilon);
    // clearColor は環境の語彙に無く、project 既定値のまま残す
    EXPECT_FALSE(over.clearColor.has_value());
}

TEST_F(EnvironmentSubsystemTest, ZeroLightDirectionFallsToDefault)
{
    SceneBase scene;
    scene.CreateSceneSubsystems();
    auto* environment = scene.GetSubsystem<EnvironmentSubsystem>();
    ASSERT_NE(environment, nullptr);

    EnvironmentSettings s = MakeSettings();
    s.lightDirection = NS::Math::Vector3{0.0f, 0.0f, 0.0f};
    environment->SetSettings(s);

    // zero ベクトルは normalize で拡散光が無言で消えるため上書きせず既定 lightDir に落とす
    const NS::Graphics::RenderSettingsOverride over = environment->BuildOverride();
    EXPECT_FALSE(over.lightDir.has_value());
    EXPECT_TRUE(over.lightColor.has_value());
    EXPECT_TRUE(over.ambientColor.has_value());
}

TEST_F(EnvironmentSubsystemTest, ResolveStoresLastResolvedIntoSubsystem)
{
    ResolveProbeScene scene;
    scene.CreateSceneSubsystems();
    auto* environment = scene.GetSubsystem<EnvironmentSubsystem>();
    ASSERT_NE(environment, nullptr);

    environment->SetSettings(MakeSettings());
    const NS::Graphics::RenderSettings defaults{};
    const NS::Graphics::RenderSettings resolved = scene.CallResolve(defaults);

    // 基底の既定 BuildSceneOverride が環境設定を読み、解決値が subsystem の控えに揃う
    EXPECT_NEAR(resolved.lightColor.x, 0.9f, kEpsilon);
    EXPECT_NEAR(environment->LastResolved().lightColor.x, resolved.lightColor.x, kEpsilon);
    EXPECT_NEAR(environment->LastResolved().lightDir.z, resolved.lightDir.z, kEpsilon);
    EXPECT_NEAR(environment->LastResolved().ambientColor.y, resolved.ambientColor.y, kEpsilon);
}

TEST_F(EnvironmentSubsystemTest, DrawSkyWithoutDeviceDoesNotCrash)
{
    // device 不在のまま Initialize された環境は skybox 装置を持たない
    SceneBase scene;
    scene.CreateSceneSubsystems();
    auto* environment = scene.GetSubsystem<EnvironmentSubsystem>();
    ASSERT_NE(environment, nullptr);
    environment->SetSettings(MakeSettings());

    // 呼出用の renderer と camera を後から立てても、装置無しの DrawSky は何もしない
    NS::Platform::WindowDesc wd{};
    wd.title = "ns_env_drawsky";
    wd.size = NS::Math::Size2D{320, 240};
    wd.visible = false;
    NS::Platform::Window window(wd);
    ASSERT_TRUE(window.IsValid());

    NS::Graphics::RendererDesc rd{};
    rd.vsync = false;
    rd.enableDebugLayer = false;
    NS::Graphics::Renderer renderer(rd, window);
    ASSERT_TRUE(renderer.IsValid());

    NS::Graphics::Camera camera{};
    environment->DrawSky(renderer, camera);
    SUCCEED();
}
