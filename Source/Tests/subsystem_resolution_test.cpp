#include <gtest/gtest.h>

#include <Framework/Scene/SceneBase.h>
#include <Framework/Scene/SceneSubsystem.h>
#include <Framework/Scene/SubsystemRegistry.h>

#include <typeindex>

namespace
{
    // scene tier で生成される service。GetSubsystem が scene map から解決するのを確かめる
    class SceneHitSubsystem : public NS::Scene::SceneSubsystem
    {};

    // shouldCreate が false を返すので、どの scene でも生成されない
    class OptOutSubsystem : public NS::Scene::SceneSubsystem
    {};

    // 登録しない型。scene map に無いので app tier provider へ委譲される
    class AppProbeSubsystem : public NS::Scene::SceneSubsystem
    {};

    // 指定 1 型だけを返すテスト用 provider
    class FakeProvider : public NS::Scene::ISubsystemProvider
    {
    public:
        FakeProvider(NS::Scene::SceneSubsystem* target, std::type_index targetType) noexcept
            : m_target(target), m_targetType(targetType)
        {}

        [[nodiscard]] NS::Scene::SceneSubsystem* FindAppSubsystem(std::type_index type) const noexcept override
        {
            if (type == m_targetType)
                return m_target;
            return nullptr;
        }

    private:
        NS::Scene::SceneSubsystem* m_target;
        std::type_index m_targetType;
    };
} // namespace

NS_REGISTER_SUBSYSTEM(SceneHitSubsystem, NS::Scene::SubsystemTier::Scene, [](const NS::Scene::SceneBase&) {
    return true;
})
NS_REGISTER_SUBSYSTEM(OptOutSubsystem, NS::Scene::SubsystemTier::Scene, [](const NS::Scene::SceneBase&) {
    return false;
})

TEST(SubsystemResolutionTest, SceneTierResolves)
{
    NS::Scene::SceneBase scene;
    scene.CreateSceneSubsystems();

    SceneHitSubsystem* hit = scene.GetSubsystem<SceneHitSubsystem>();
    ASSERT_NE(hit, nullptr);
    // 同一実体を返し続ける
    EXPECT_EQ(scene.GetSubsystem<SceneHitSubsystem>(), hit);
}

TEST(SubsystemResolutionTest, MissDelegatesToProvider)
{
    NS::Scene::SceneBase scene;
    AppProbeSubsystem probe;
    FakeProvider provider(&probe, std::type_index(typeid(AppProbeSubsystem)));
    scene.SetSubsystemProvider(&provider);

    // scene map には無いので provider が解決する
    EXPECT_EQ(scene.GetSubsystem<AppProbeSubsystem>(), &probe);
}

TEST(SubsystemResolutionTest, WrongTypeReturnsNull)
{
    NS::Scene::SceneBase scene;
    scene.CreateSceneSubsystems();

    // 未登録かつ provider 未設定なら nullptr
    EXPECT_EQ(scene.GetSubsystem<AppProbeSubsystem>(), nullptr);
}

TEST(SubsystemResolutionTest, ShouldCreateOptOut)
{
    NS::Scene::SceneBase scene;
    scene.CreateSceneSubsystems();

    // shouldCreate が false なので生成されず、provider も無いので nullptr
    EXPECT_EQ(scene.GetSubsystem<OptOutSubsystem>(), nullptr);
}
