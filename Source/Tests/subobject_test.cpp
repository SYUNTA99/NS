#include "NSlib/CoreObject/AssetManager.h"
#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/Reflection/Archetype.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/CoreObject/Reflection/ReflectedSubObjs.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/BoxCollision.h"
#include "NSlib/CoreObject/SubObjects/Model.h"
#include "NSlib/CoreObject/SubObjects/SphereCollision.h"
#include "NSlib/Windows/FileSystem.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    class PlainSubObj final : public NS::SubObject
    {};

    class OrderActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_BEGIN(OrderActor, NS::SceneActor)
        NS_REFLECT_SUBOBJ(first, "First")
        NS_REFLECT_SUBOBJ(second, "Second")
        NS_REFLECT_END()

        std::unique_ptr<PlainSubObj> first;
        std::unique_ptr<PlainSubObj> second;
        NS::BoxCollision* box = nullptr;
        NS::SphereCollision* refused = nullptr;

    protected:
        void OnInit() override
        {
            CreateSubObj(first);
            box = CreateSubObj<NS::Collision, NS::BoxCollision>(m_collision, this);
            CreateSubObj(second);
            CreateSubObj(m_model, this);
            refused = CreateSubObj<NS::Collision, NS::SphereCollision>(m_collision, this);
        }
    };

    class NameClashActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_BEGIN(NameClashActor, NS::SceneActor)
        NS_REFLECT_SUBOBJ(clash, "Model")
        NS_REFLECT_END()

        NS::Model* model = nullptr;
        std::unique_ptr<PlainSubObj> clash;

    protected:
        void OnInit() override
        {
            CreateSubObj(clash);
            model = CreateSubObj(m_model, this);
        }
    };

    // 基底の OnInit を呼ばずに、共通の部分を InitShape に出す
    class MiddleActor : public NS::SceneActor
    {
    protected:
        void OnInit() override
        {
            InitShape();
            CreateSubObj<NS::Collision, NS::BoxCollision>(m_collision, this);
        }
        void InitShape() { CreateSubObj(m_model, this); }
    };

    class GrandActor final : public MiddleActor
    {
    protected:
        void OnInit() override
        {
            InitShape();
            CreateSubObj<NS::Collision, NS::SphereCollision>(m_collision, this);
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
            scene.SpawnTransient(std::move(actor));
        });
        static_assert(!requires(TScene& scene, std::unique_ptr<NS::SceneActor> actor) {
            scene.SpawnObject(std::move(actor), std::string{});
        });
    }

} // namespace

class SubObjectInitProbeActor final : public NS::SceneActor
{
public:
    int inits = 0;
    NS_REFLECT_NONE(SubObjectInitProbeActor, NS::SceneActor)

protected:
    void OnInit() override
    {
        ++inits;
        CreateSubObj(m_model, this);
    }
};
NS_CLASS(SubObjectInitProbeActor)

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
        // 一時のアクターは id を持たないので、一覧を端から見る
        inListAtLoad = false;
        const NS::Scene* scene = OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        for (const NS::SceneActor* actor : scene->Objects())
        {
            if (actor == this)
            {
                inListAtLoad = true;
            }
        }
    }
};
NS_CLASS(AssetTimingProbeActor)

TEST(SubObject, CreateWritesNameAndEnumeratesRootThenSlotsThenDerivedInCreationOrder)
{
    const std::unique_ptr<OrderActor> actor = NS::CreateActor<OrderActor>();
    ASSERT_NE(actor->first, nullptr);
    ASSERT_NE(actor->second, nullptr);
    EXPECT_EQ(actor->SubObjKey(*actor->first), "First");
    EXPECT_EQ(actor->first->Owner(), actor.get());
    ASSERT_NE(actor->CollisionSubObj(), nullptr);
    EXPECT_EQ(actor->SubObjKey(*actor->CollisionSubObj()), "Collision");
    std::vector<std::string> names;
    NS::ForEachReflectedSubObj(*actor, [&](const NS::SubObjectDesc& field, const NS::SubObject* subObject) {
        EXPECT_EQ(subObject->Owner(), actor.get());
        names.push_back(field.key);
    });
    EXPECT_EQ(names, (std::vector<std::string>{"Transform", "Model", "Collision", "First", "Second"}));
}

TEST(SubObject, SecondCreateInTheSameSlotIsRefused)
{
    const std::unique_ptr<OrderActor> actor = NS::CreateActor<OrderActor>();
    EXPECT_EQ(actor->refused, nullptr);
    EXPECT_EQ(actor->CollisionSubObj(), actor->box);
}

TEST(SubObject, SlotIsRefusedWhenItsNameIsTaken)
{
    const std::unique_ptr<NameClashActor> actor = NS::CreateActor<NameClashActor>();
    EXPECT_EQ(actor->model, nullptr);
    EXPECT_EQ(actor->ModelSubObj(), nullptr);
}

TEST(SubObject, GrandchildReplacesTheCollisionTypeChosenByTheMiddleBase)
{
    const std::unique_ptr<MiddleActor> middle = NS::CreateActor<MiddleActor>();
    EXPECT_NE(NS::Cast<NS::BoxCollision>(middle->CollisionSubObj()), nullptr);

    const std::unique_ptr<GrandActor> grand = NS::CreateActor<GrandActor>();
    EXPECT_NE(NS::Cast<NS::SphereCollision>(grand->CollisionSubObj()), nullptr);
    EXPECT_NE(grand->ModelSubObj(), nullptr);
}

TEST(SubObject, OnlyTheBuildersCanInitAndEnterAnActor)
{
    CheckInitIsClosed();
    CheckSceneBuildsItsOwnActors();
}

TEST(SubObject, EveryBuildPathRunsOnInitExactlyOnce)
{
    NS::Scene scene;
    const SubObjectInitProbeActor* transient = scene.SpawnTransient<SubObjectInitProbeActor>();
    EXPECT_EQ(transient->inits, 1);
    EXPECT_NE(transient->ModelSubObj(), nullptr);

    const SubObjectInitProbeActor* placed = scene.SpawnObject<SubObjectInitProbeActor>("Placed");
    EXPECT_EQ(placed->inits, 1);

    const std::unique_ptr<SubObjectInitProbeActor> loose = NS::CreateActor<SubObjectInitProbeActor>();
    EXPECT_EQ(loose->inits, 1);

    const SubObjectInitProbeActor* fromJson =
        NS::Cast<SubObjectInitProbeActor>(scene.SpawnFromJson(NS::MakePrototypeJson<SubObjectInitProbeActor>()));
    ASSERT_NE(fromJson, nullptr);
    EXPECT_EQ(fromJson->inits, 1);

    const NS::SceneActor& baseline = NS::ArchetypeLibrary::Get().Baseline("SubObjectInitProbeActor");
    const SubObjectInitProbeActor* probe = NS::Cast<SubObjectInitProbeActor>(&baseline);
    ASSERT_NE(probe, nullptr);
    EXPECT_EQ(probe->inits, 1);
    EXPECT_NE(probe->ModelSubObj(), nullptr);
}

// どの道で作っても、素材は欄を書いた後・一覧に入る前・OnStart の前に 1 回だけ読む
TEST(SubObject, EveryBuildPathLoadsAssetsOnceBeforeEnteringTheScene)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::AssetManagerScope useAssets{assets};
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

    nlohmann::json placed = NS::MakePrototypeJson<AssetTimingProbeActor>();
    NS::SetObjectJsonId(placed, 1);
    nlohmann::json doc = NS::MakeSceneJson();
    NS::SceneJsonObjects(doc).push_back(placed);
    scene.ResetFromJson(doc);
    check(scene.Objects().FindByObjectId(1), "組み直し");

    check(scene.ReplaceFromJson(placed), "作り直す");
    check(scene.SpawnFromJson(NS::MakePrototypeJson<AssetTimingProbeActor>()), "JSON から湧かす");
    check(scene.SpawnTransient<AssetTimingProbeActor>(), "型から湧かす (一時)");
    check(scene.SpawnObject<AssetTimingProbeActor>("Placed"), "型から湧かす (配置)");
}

// AssetManager を差していない間も素材を読む道を通る。試しも本番と同じ順を通る
TEST(SubObject, ActorLoadsAssetsWithoutAssetManagerScope)
{
    NS::Scene scene;
    const AssetTimingProbeActor* actor = scene.SpawnTransient<AssetTimingProbeActor>();
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(actor->loads, 1);
}

TEST(SubObject, PrototypeJsonListsTheSubObjectsInit)
{
    const nlohmann::json prototype = NS::MakePrototypeJson<SubObjectInitProbeActor>();
    EXPECT_NE(NS::SubObjFields(prototype, "Model"), nullptr);
}
