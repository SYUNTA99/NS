#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/RenderSettings.h>
#include <Runtime/Object/Components/DirectionalLightComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Scene/Scene.h>
#include <gtest/gtest.h>

namespace
{
    using NS::Object::DirectionalLightComponent;
    using NS::Object::GameObject;
    using NS::Object::Scene;

    constexpr float k_Epsilon = 1e-5f;

    /// protected の ResolveSceneSettings をテスト用に公開する最小派生
    class ResolveProbeScene : public Scene
    {
    public:
        NS::Graphics::RenderSettings CallResolve(const NS::Graphics::RenderSettings& defaults)
        {
            return ResolveSceneSettings(defaults);
        }
    };

    /// リフレクションフィールド越しに平行光の値を書く。照明はデータ駆動で、公開の設定関数を持たない
    void SetLightField(DirectionalLightComponent& light, const char* name, const NS::Core::Vector3& value)
    {
        const NS::Object::FieldDesc* field = NS::Object::FindField(DirectionalLightComponent::StaticReflection(), name);
        ASSERT_NE(field, nullptr) << name;
        field->set(&light, &value);
    }

    /// world に平行光を 1 本置いて返す
    DirectionalLightComponent* SpawnLight(Scene& scene)
    {
        GameObject* obj = scene.World().Spawn<GameObject>();
        return obj->AddComponent<DirectionalLightComponent>();
    }
} // namespace

class SceneLightResolveTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(SceneLightResolveTest, NoLightKeepsProjectDefaults)
{
    ResolveProbeScene scene;

    // 平行光が無ければ project 既定値がそのまま残る
    NS::Graphics::RenderSettings defaults{};
    defaults.lightColor = NS::Core::Vector3{0.5f, 0.6f, 0.7f};
    const NS::Graphics::RenderSettings resolved = scene.CallResolve(defaults);

    EXPECT_NEAR(resolved.lightColor.x, 0.5f, k_Epsilon);
    EXPECT_NEAR(resolved.lightDir.x, defaults.lightDir.x, k_Epsilon);
    EXPECT_NEAR(resolved.ambientColor.x, defaults.ambientColor.x, k_Epsilon);
    EXPECT_NEAR(resolved.clearColor.B(), defaults.clearColor.B(), k_Epsilon);
}

TEST_F(SceneLightResolveTest, PlacedLightOverridesResolve)
{
    ResolveProbeScene scene;

    DirectionalLightComponent* light = SpawnLight(scene);
    ASSERT_NE(light, nullptr);
    SetLightField(*light, "方向", NS::Core::Vector3{0.0f, -1.0f, 0.5f});
    SetLightField(*light, "色", NS::Core::Vector3{0.9f, 0.8f, 0.7f});
    SetLightField(*light, "環境光", NS::Core::Vector3{0.1f, 0.2f, 0.3f});

    const NS::Graphics::RenderSettings resolved = scene.CallResolve(NS::Graphics::RenderSettings{});

    // 配置された平行光の値が解決値に映る
    EXPECT_NEAR(resolved.lightDir.z, 0.5f, k_Epsilon);
    EXPECT_NEAR(resolved.lightColor.x, 0.9f, k_Epsilon);
    EXPECT_NEAR(resolved.ambientColor.z, 0.3f, k_Epsilon);
    // clearColor は照明の語彙に無く、project 既定値のまま残す
    EXPECT_NEAR(resolved.clearColor.B(), NS::Graphics::RenderSettings{}.clearColor.B(), k_Epsilon);
}

TEST_F(SceneLightResolveTest, ZeroLightDirectionFallsToDefault)
{
    ResolveProbeScene scene;

    DirectionalLightComponent* light = SpawnLight(scene);
    ASSERT_NE(light, nullptr);
    SetLightField(*light, "方向", NS::Core::Vector3{0.0f, 0.0f, 0.0f});
    SetLightField(*light, "色", NS::Core::Vector3{0.9f, 0.8f, 0.7f});

    NS::Graphics::RenderSettings defaults{};
    const NS::Graphics::RenderSettings resolved = scene.CallResolve(defaults);

    // zero ベクトルは normalize で拡散光が無言で消えるため上書きせず既定 lightDir に落とす
    EXPECT_NEAR(resolved.lightDir.x, defaults.lightDir.x, k_Epsilon);
    // 色は有効なので上書きされる
    EXPECT_NEAR(resolved.lightColor.x, 0.9f, k_Epsilon);
}
