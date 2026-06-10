#include <gtest/gtest.h>

#include <Framework/Graphics/RenderSettings.h>
#include <Framework/Scene/RenderContext.h>
#include <Framework/Scene/SceneBase.h>

namespace
{
    constexpr float kEpsilon = 1e-5f;

    /// scene 段解決の hook だけを差し替えた最小 SceneBase 派生
    /// BuildSceneOverride が返す override を ResolveSceneSettings に流して結果を観測する
    class FakeSceneWithOverride : public NS::Scene::SceneBase
    {
    public:
        NS::Graphics::RenderSettingsOverride m_over{};

        NS::Graphics::RenderSettings CallResolve(const NS::Graphics::RenderSettings& defaults)
        {
            return ResolveSceneSettings(defaults);
        }

    protected:
        NS::Graphics::RenderSettingsOverride BuildSceneOverride() override { return m_over; }
    };
} // namespace

TEST(RenderSettingsAuto, EmptySceneOverrideKeepsDefaults)
{
    FakeSceneWithOverride scene;
    const NS::Graphics::RenderSettings projDefaults{};
    const NS::Graphics::RenderSettings r = scene.CallResolve(projDefaults);
    EXPECT_NEAR(r.lightDir.x, projDefaults.lightDir.x, kEpsilon);
    EXPECT_NEAR(r.lightDir.y, projDefaults.lightDir.y, kEpsilon);
    EXPECT_NEAR(r.lightDir.z, projDefaults.lightDir.z, kEpsilon);
    EXPECT_NEAR(r.lightColor.x, projDefaults.lightColor.x, kEpsilon);
    EXPECT_NEAR(r.ambientColor.x, projDefaults.ambientColor.x, kEpsilon);
    EXPECT_NEAR(r.clearColor.B(), projDefaults.clearColor.B(), kEpsilon);
}

TEST(RenderSettingsAuto, SceneOverrideAppliesToContext)
{
    FakeSceneWithOverride scene;
    scene.m_over.lightDir = NS::Math::Vector3{1.0f, 0.0f, 0.0f};
    const NS::Graphics::RenderSettings projDefaults{};
    const NS::Graphics::RenderSettings r = scene.CallResolve(projDefaults);
    EXPECT_NEAR(r.lightDir.x, 1.0f, kEpsilon);
    EXPECT_NEAR(r.lightDir.y, 0.0f, kEpsilon);
    EXPECT_NEAR(r.lightDir.z, 0.0f, kEpsilon);
    // 未指定フィールドは project 既定値のまま残る
    EXPECT_NEAR(r.lightColor.x, projDefaults.lightColor.x, kEpsilon);
    EXPECT_NEAR(r.ambientColor.x, projDefaults.ambientColor.x, kEpsilon);
    EXPECT_NEAR(r.clearColor.B(), projDefaults.clearColor.B(), kEpsilon);
}

TEST(RenderSettingsAuto, ZeroLightDirOverrideFallsBackToDefault)
{
    // lightDir を nullopt のまま (zero ベクトル検証は呼出側責務) → Resolve は既定値を残す
    FakeSceneWithOverride scene;
    const NS::Graphics::RenderSettings projDefaults{};
    const NS::Graphics::RenderSettings r = scene.CallResolve(projDefaults);
    EXPECT_NEAR(r.lightDir.x, -0.3f, kEpsilon);
    EXPECT_NEAR(r.lightDir.y, -1.0f, kEpsilon);
    EXPECT_NEAR(r.lightDir.z, -0.2f, kEpsilon);
}
