#include <gtest/gtest.h>

#include <Framework/Graphics/RenderSettings.h>
#include <Framework/Scene/RenderContext.h>
#include <Framework/Scene/SceneBase.h>

#include <cmath>

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

TEST(RenderSettingsAuto, ObjectOverrideChainsOnScene)
{
    // object 段は scene 解決値をベースに Resolve する 2 段連鎖。scene が決めた lightColor の上に
    // object が lightDir だけ載せ、両者が正しく重なることを縛るゴールデン値
    NS::Graphics::RenderSettings proj{};
    NS::Graphics::RenderSettingsOverride sceneOver{};
    sceneOver.lightColor = NS::Math::Vector3{0.5f, 0.5f, 0.5f};
    const NS::Graphics::RenderSettings sceneResolved = NS::Graphics::Resolve(proj, sceneOver);

    NS::Graphics::RenderSettingsOverride objOver{};
    objOver.lightDir = NS::Math::Vector3{0.0f, -1.0f, 0.0f};
    const NS::Graphics::RenderSettings finalSettings = NS::Graphics::Resolve(sceneResolved, objOver);

    EXPECT_NEAR(finalSettings.lightColor.x, 0.5f, kEpsilon);
    EXPECT_NEAR(finalSettings.lightColor.y, 0.5f, kEpsilon);
    EXPECT_NEAR(finalSettings.lightColor.z, 0.5f, kEpsilon);
    EXPECT_NEAR(finalSettings.lightDir.y, -1.0f, kEpsilon);
}

TEST(RenderSettingsAuto, NoObjectOverrideUsesSceneResolved)
{
    // object override 空なら scene 解決値が完全にそのまま流れる
    NS::Graphics::RenderSettings proj{};
    NS::Graphics::RenderSettingsOverride sceneOver{};
    sceneOver.lightDir = NS::Math::Vector3{1.0f, 0.0f, 0.0f};
    sceneOver.ambientColor = NS::Math::Vector3{0.4f, 0.4f, 0.4f};
    const NS::Graphics::RenderSettings sceneResolved = NS::Graphics::Resolve(proj, sceneOver);

    const NS::Graphics::RenderSettingsOverride emptyObj{};
    const NS::Graphics::RenderSettings finalSettings = NS::Graphics::Resolve(sceneResolved, emptyObj);

    EXPECT_NEAR(finalSettings.lightDir.x, sceneResolved.lightDir.x, kEpsilon);
    EXPECT_NEAR(finalSettings.lightDir.y, sceneResolved.lightDir.y, kEpsilon);
    EXPECT_NEAR(finalSettings.lightColor.x, sceneResolved.lightColor.x, kEpsilon);
    EXPECT_NEAR(finalSettings.ambientColor.x, sceneResolved.ambientColor.x, kEpsilon);
    EXPECT_NEAR(finalSettings.clearColor.B(), sceneResolved.clearColor.B(), kEpsilon);
}

TEST(RenderSettingsAuto, ObjectStageBaseIsSceneNotProjectDefault)
{
    // object 段のベースが project 既定値ではなく scene 解決値であることを縛る
    // scene が lightColor を変えた後、object は lightDir だけ載せる → 最終 lightColor は
    // project 既定 (白) ではなく scene override 値であること
    NS::Graphics::RenderSettings proj{};
    NS::Graphics::RenderSettingsOverride sceneOver{};
    sceneOver.lightColor = NS::Math::Vector3{0.25f, 0.25f, 0.25f};
    const NS::Graphics::RenderSettings sceneResolved = NS::Graphics::Resolve(proj, sceneOver);

    NS::Graphics::RenderSettingsOverride objOver{};
    objOver.lightDir = NS::Math::Vector3{0.0f, 0.0f, -1.0f};
    const NS::Graphics::RenderSettings finalSettings = NS::Graphics::Resolve(sceneResolved, objOver);

    // project 既定の lightColor は {1,1,1}。それに引きずられていないこと
    EXPECT_NEAR(finalSettings.lightColor.x, 0.25f, kEpsilon);
    EXPECT_FALSE(std::abs(finalSettings.lightColor.x - proj.lightColor.x) < kEpsilon);
    EXPECT_NEAR(finalSettings.lightDir.z, -1.0f, kEpsilon);
}
