#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/RenderSettings.h>
#include <Runtime/Object/Components/DirectionalLight.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Scene/Scene.h>
#include <gtest/gtest.h>

namespace
{
    using NS::Obj::DirectionalLight;
    using NS::Obj::GameObject;
    using NS::Obj::Scene;

    constexpr float k_Epsilon = 1e-5f;

    //! protected の ResolveSceneSettings をテスト用に公開する最小派生
    class ResolveProbeScene : public Scene
    {
    public:
        NS::Gfx::RenderSettings CallResolve(const NS::Gfx::RenderSettings& defaults)
        {
            return ResolveSceneSettings(defaults);
        }
    };

    //! リフレクションフィールド越しに平行光の値を書く。照明はデータ駆動で、公開の設定関数を持たない
    void SetLightField(DirectionalLight& light, const char* name, const NS::Core::Vector3& value)
    {
        const NS::Obj::FieldDesc* field = NS::Obj::FindField(DirectionalLight::StaticReflection(), name);
        ASSERT_NE(field, nullptr) << name;
        field->set(&light, &value);
    }

    //! シーンに平行光を 1 本置いて返す。登録はまだしていない
    DirectionalLight* SpawnLight(Scene& scene)
    {
        // 後から OnStart を呼ぶ時に OwningScene が要る。ObjectList::Spawn は scene を紐付けない
        GameObject* obj = scene.SpawnTransient<GameObject>();
        if (obj == nullptr)
        {
            return nullptr;
        }
        return obj->AddComponent<DirectionalLight>();
    }

    //! OnStart を呼ばない経路を LightWithoutStartDoesNotResolve が見るので、呼ぶ側と分けてある
    DirectionalLight* SpawnStartedLight(Scene& scene)
    {
        DirectionalLight* light = SpawnLight(scene);
        if (light != nullptr)
        {
            light->OnStart();
        }
        return light;
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
    NS::Gfx::RenderSettings defaults{};
    defaults.lightColor = NS::Core::Vector3{0.5f, 0.6f, 0.7f};
    const NS::Gfx::RenderSettings resolved = scene.CallResolve(defaults);

    EXPECT_NEAR(resolved.lightColor.x, 0.5f, k_Epsilon);
    EXPECT_NEAR(resolved.lightDir.x, defaults.lightDir.x, k_Epsilon);
    EXPECT_NEAR(resolved.ambientColor.x, defaults.ambientColor.x, k_Epsilon);
    EXPECT_NEAR(resolved.clearColor.B(), defaults.clearColor.B(), k_Epsilon);
}

TEST_F(SceneLightResolveTest, PlacedLightOverridesResolve)
{
    ResolveProbeScene scene;

    DirectionalLight* light = SpawnStartedLight(scene);
    ASSERT_NE(light, nullptr);
    SetLightField(*light, "方向", NS::Core::Vector3{0.0f, -1.0f, 0.5f});
    SetLightField(*light, "色", NS::Core::Vector3{0.9f, 0.8f, 0.7f});
    SetLightField(*light, "環境光", NS::Core::Vector3{0.1f, 0.2f, 0.3f});

    const NS::Gfx::RenderSettings resolved = scene.CallResolve(NS::Gfx::RenderSettings{});

    // 配置された平行光の値が解決値に映る
    EXPECT_NEAR(resolved.lightDir.z, 0.5f, k_Epsilon);
    EXPECT_NEAR(resolved.lightColor.x, 0.9f, k_Epsilon);
    EXPECT_NEAR(resolved.ambientColor.z, 0.3f, k_Epsilon);
    // clearColor は照明の語彙に無く、project 既定値のまま残す
    EXPECT_NEAR(resolved.clearColor.B(), NS::Gfx::RenderSettings{}.clearColor.B(), k_Epsilon);
}

TEST_F(SceneLightResolveTest, ZeroLightDirectionFallsToDefault)
{
    ResolveProbeScene scene;

    DirectionalLight* light = SpawnStartedLight(scene);
    ASSERT_NE(light, nullptr);
    SetLightField(*light, "方向", NS::Core::Vector3{0.0f, 0.0f, 0.0f});
    SetLightField(*light, "色", NS::Core::Vector3{0.9f, 0.8f, 0.7f});

    NS::Gfx::RenderSettings defaults{};
    const NS::Gfx::RenderSettings resolved = scene.CallResolve(defaults);

    // zero ベクトルは normalize で拡散光が無言で消えるため上書きせず既定 lightDir に落とす
    EXPECT_NEAR(resolved.lightDir.x, defaults.lightDir.x, k_Epsilon);
    // 色は有効なので上書きされる
    EXPECT_NEAR(resolved.lightColor.x, 0.9f, k_Epsilon);
}

TEST_F(SceneLightResolveTest, LightWithoutStartDoesNotResolve)
{
    ResolveProbeScene scene;

    DirectionalLight* light = SpawnLight(scene);
    ASSERT_NE(light, nullptr);
    SetLightField(*light, "色", NS::Core::Vector3{0.9f, 0.8f, 0.7f});

    NS::Gfx::RenderSettings defaults{};
    defaults.lightColor = NS::Core::Vector3{0.11f, 0.12f, 0.13f};
    const NS::Gfx::RenderSettings resolved = scene.CallResolve(defaults);

    EXPECT_NEAR(resolved.lightColor.x, 0.11f, k_Epsilon);
}

TEST_F(SceneLightResolveTest, LightUnregistersOnEndPlay)
{
    ResolveProbeScene scene;

    DirectionalLight* light = SpawnStartedLight(scene);
    ASSERT_NE(light, nullptr);
    SetLightField(*light, "色", NS::Core::Vector3{0.9f, 0.8f, 0.7f});
    light->OnEndPlay();

    NS::Gfx::RenderSettings defaults{};
    defaults.lightColor = NS::Core::Vector3{0.11f, 0.12f, 0.13f};
    const NS::Gfx::RenderSettings resolved = scene.CallResolve(defaults);

    EXPECT_NEAR(resolved.lightColor.x, 0.11f, k_Epsilon);
}

TEST_F(SceneLightResolveTest, DoubleStartStillUnregisters)
{
    ResolveProbeScene scene;

    DirectionalLight* light = SpawnStartedLight(scene);
    ASSERT_NE(light, nullptr);
    light->OnStart();
    SetLightField(*light, "色", NS::Core::Vector3{0.9f, 0.8f, 0.7f});
    light->OnEndPlay();

    NS::Gfx::RenderSettings defaults{};
    defaults.lightColor = NS::Core::Vector3{0.11f, 0.12f, 0.13f};
    const NS::Gfx::RenderSettings resolved = scene.CallResolve(defaults);

    EXPECT_NEAR(resolved.lightColor.x, 0.11f, k_Epsilon);
}

TEST_F(SceneLightResolveTest, LastRegisteredLightWins)
{
    ResolveProbeScene scene;

    DirectionalLight* first = SpawnStartedLight(scene);
    ASSERT_NE(first, nullptr);
    SetLightField(*first, "色", NS::Core::Vector3{0.9f, 0.0f, 0.0f});

    DirectionalLight* second = SpawnStartedLight(scene);
    ASSERT_NE(second, nullptr);
    SetLightField(*second, "色", NS::Core::Vector3{0.0f, 0.5f, 0.0f});

    const NS::Gfx::RenderSettings resolved = scene.CallResolve(NS::Gfx::RenderSettings{});

    // 多灯合成は持たないので、2 本置いたら後から登録した方の値が解決値に残る
    EXPECT_NEAR(resolved.lightColor.x, 0.0f, k_Epsilon);
    EXPECT_NEAR(resolved.lightColor.y, 0.5f, k_Epsilon);
}
