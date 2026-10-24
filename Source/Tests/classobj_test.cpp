#include "Editor/EditorActors.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/BoxCollision.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/SphereCollision.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Windows/FileSystem.h"
#include "TestActorFile.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    class PlainClassObj final : public NS::Object
    {};

    class OrderActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_BEGIN(OrderActor, NS::SceneActor)
        NS_CLASSOBJ(first, "First")
        NS_CLASSOBJ(second, "Second")
        NS_REFLECT_END()

        std::unique_ptr<PlainClassObj> first;
        std::unique_ptr<PlainClassObj> second;
        NS::BoxCollision* box = nullptr;
        NS::SphereCollision* refused = nullptr;

    protected:
        void OnInit() override
        {
            CreateClassObj(first);
            box = CreateClassObj<NS::Collision, NS::BoxCollision>(m_collision);
            CreateClassObj(second);
            CreateClassObj(m_model);
            refused = CreateClassObj<NS::Collision, NS::SphereCollision>(m_collision);
        }
    };

    class NameClashActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_BEGIN(NameClashActor, NS::SceneActor)
        NS_CLASSOBJ(clash, "Model")
        NS_REFLECT_END()

        NS::Model* model = nullptr;
        std::unique_ptr<PlainClassObj> clash;

    protected:
        void OnInit() override
        {
            CreateClassObj(clash);
            model = CreateClassObj(m_model);
        }
    };

    // 基底の OnInit を呼ばずに、共通の部分を InitShape に出す
    class MiddleActor : public NS::SceneActor
    {
    protected:
        void OnInit() override
        {
            InitShape();
            CreateClassObj<NS::Collision, NS::BoxCollision>(m_collision);
        }
        void InitShape() { CreateClassObj(m_model); }
    };

    class GrandActor final : public MiddleActor
    {
    protected:
        void OnInit() override
        {
            InitShape();
            CreateClassObj<NS::Collision, NS::SphereCollision>(m_collision);
        }
    };

    // Init は作る関数だけが呼ぶ。外から呼べると、1 つの個体に 2 回書ける
    template <class TActor = NS::SceneActor> void CheckInitIsClosed()
    {
        static_assert(!requires(TActor& actor) { actor.Init(); });
    }

    // シーンは外で作った個体を受けない。受けると、Init していない個体が一覧に入る
    template <class TScene = NS::Scene> void CheckSceneBuildsItsOwnActors()
    {
        static_assert(!requires(TScene& scene, std::unique_ptr<NS::SceneActor> actor) {
            scene.SpawnActor(std::move(actor), std::string{});
        });
    }

} // namespace

class ClassObjInitProbeActor final : public NS::SceneActor
{
public:
    int inits = 0;
    NS_REFLECT_NONE(ClassObjInitProbeActor, NS::SceneActor)

protected:
    void OnInit() override
    {
        ++inits;
        CreateClassObj(m_model);
    }
};
NS_CLASS(ClassObjInitProbeActor)

// 素材を読んだ時に、シーンの一覧に居たか・始まっていたかを控える
class AssetTimingProbeActor final : public NS::SceneActor
{
public:
    int loads = 0;
    bool inListAtLoad = false;
    bool startedAtLoad = false;
    bool started = false;

    NS_REFLECT_NONE(AssetTimingProbeActor, NS::SceneActor)

    void OnStart() override
    {
        started = true;
        NS::SceneActor::OnStart();
    }

protected:
    void OnAssetsLoaded() override
    {
        NS::SceneActor::OnAssetsLoaded();
        ++loads;
        startedAtLoad = started;
        // 型から湧かす道は一覧へ入る時に id を振るので、id で引かずに一覧を端から見る
        inListAtLoad = false;
        const NS::Scene* scene = OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        for (const NS::SceneActor* actor : scene->Actors())
        {
            if (actor == this)
            {
                inListAtLoad = true;
            }
        }
    }
};
NS_CLASS(AssetTimingProbeActor)

TEST(ClassObj, CreateEnumeratesRootThenSlotsThenDerivedInCreationOrder)
{
    const TestLogCapture log;
    const std::unique_ptr<OrderActor> actor = NS::CreateActor<OrderActor>();
    EXPECT_EQ(log.ErrorCount(), 1);
    ASSERT_NE(actor->first, nullptr);
    ASSERT_NE(actor->second, nullptr);
    ASSERT_NE(actor->CollisionClassObj(), nullptr);
    std::vector<std::string> names;
    NS::ForEachReflectedClassObj(actor.get(), [&](const NS::ClassObjInfo& field, const NS::Object* part) {
        if (std::string_view{field.key} == "First")
        {
            EXPECT_EQ(part, actor->first.get());
        }
        if (std::string_view{field.key} == "Collision")
        {
            EXPECT_EQ(part, actor->CollisionClassObj());
        }
        names.push_back(field.key);
    });
    EXPECT_EQ(names, (std::vector<std::string>{"Transform", "Model", "Collision", "First", "Second"}));
}

TEST(ClassObj, SecondCreateInTheSameSlotIsRefused)
{
    const TestLogCapture log;
    const std::unique_ptr<OrderActor> actor = NS::CreateActor<OrderActor>();
    EXPECT_EQ(log.ErrorCount(), 1);
    EXPECT_EQ(actor->refused, nullptr);
    EXPECT_EQ(actor->CollisionClassObj(), actor->box);
}

TEST(ClassObj, SlotIsRefusedWhenItsNameIsTaken)
{
    const TestLogCapture log;
    const std::unique_ptr<NameClashActor> actor = NS::CreateActor<NameClashActor>();
    EXPECT_EQ(log.ErrorCount(), 1);
    EXPECT_EQ(actor->model, nullptr);
    EXPECT_EQ(actor->ModelClassObj(), nullptr);
}

TEST(ClassObj, GrandchildReplacesTheCollisionTypeChosenByTheMiddleBase)
{
    const std::unique_ptr<MiddleActor> middle = NS::CreateActor<MiddleActor>();
    EXPECT_NE(NS::Cast<NS::BoxCollision>(middle->CollisionClassObj()), nullptr);

    const std::unique_ptr<GrandActor> grand = NS::CreateActor<GrandActor>();
    EXPECT_NE(NS::Cast<NS::SphereCollision>(grand->CollisionClassObj()), nullptr);
    EXPECT_NE(grand->ModelClassObj(), nullptr);
}

TEST(ClassObj, OnlyTheBuildersCanInitAndEnterAnActor)
{
    CheckInitIsClosed();
    CheckSceneBuildsItsOwnActors();
}

TEST(ClassObj, EveryBuildPathRunsOnInitExactlyOnce)
{
    const ScopedActorFile probeFile{"ClassObjInitProbeActor", CodeValuesActorFile("ClassObjInitProbeActor")};
    NS::Scene scene;
    const ClassObjInitProbeActor* placed = scene.SpawnActor<ClassObjInitProbeActor>("Placed");
    EXPECT_EQ(placed->inits, 1);
    EXPECT_NE(placed->ModelClassObj(), nullptr);

    const std::unique_ptr<ClassObjInitProbeActor> loose = NS::CreateActor<ClassObjInitProbeActor>();
    EXPECT_EQ(loose->inits, 1);

    const ClassObjInitProbeActor* fromJson =
        NS::Cast<ClassObjInitProbeActor>(scene.SpawnFromJson(NS::Editor::MakePrototypeJson<ClassObjInitProbeActor>()));
    ASSERT_NE(fromJson, nullptr);
    EXPECT_EQ(fromJson->inits, 1);

    const std::unique_ptr<NS::SceneActor> baselineOwned = ActorFromFile("ClassObjInitProbeActor");
    ASSERT_NE(baselineOwned, nullptr);
    const NS::SceneActor& baseline = *baselineOwned;
    const ClassObjInitProbeActor* probe = NS::Cast<ClassObjInitProbeActor>(&baseline);
    ASSERT_NE(probe, nullptr);
    EXPECT_EQ(probe->inits, 1);
    EXPECT_NE(probe->ModelClassObj(), nullptr);
}

// どの道で作っても、素材は欄を書いた後・一覧に入る前・OnStart の前に 1 回だけ読む
TEST(ClassObj, EveryBuildPathLoadsAssetsOnceBeforeEnteringTheScene)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::AssetManagerScope useAssets{assets};
    const ScopedActorFile probeFile{"AssetTimingProbeActor", CodeValuesActorFile("AssetTimingProbeActor")};
    NS::Scene scene;
    const auto check = [](const NS::SceneActor* built, const char* path) {
        SCOPED_TRACE(path);
        const AssetTimingProbeActor* actor = NS::Cast<AssetTimingProbeActor>(built);
        ASSERT_NE(actor, nullptr);
        EXPECT_EQ(actor->loads, 1);
        EXPECT_FALSE(actor->inListAtLoad);
        EXPECT_FALSE(actor->startedAtLoad);
        EXPECT_TRUE(actor->started);
    };

    nlohmann::json placed = NS::Editor::MakePrototypeJson<AssetTimingProbeActor>();
    NS::SetActorJsonId(placed, 1);
    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonActors(doc).push_back(placed);
    scene.ResetFromJson(doc);
    check(scene.Actors().FindByActorId(1), "組み直し");

    check(scene.SpawnFromJson(NS::Editor::MakePrototypeJson<AssetTimingProbeActor>()), "JSON から湧かす");
    check(scene.SpawnActor<AssetTimingProbeActor>("Placed"), "型から湧かす");
}

// AssetManager を差していない間も素材を読む道を通る。試しも本番と同じ順を通る
TEST(ClassObj, ActorLoadsAssetsWithoutAssetManagerScope)
{
    NS::Scene scene;
    const AssetTimingProbeActor* actor = scene.SpawnActor<AssetTimingProbeActor>("AssetTimingProbeActor");
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(actor->loads, 1);
}

TEST(ClassObj, PrototypeJsonListsTheClassObjsInit)
{
    const nlohmann::json prototype = NS::Editor::MakePrototypeJson<ClassObjInitProbeActor>();
    EXPECT_NE(NS::ClassObjFields(prototype, "Model"), nullptr);
}
