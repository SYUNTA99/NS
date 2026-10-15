#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/Reflection/Archetype.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/CoreObject/Reflection/ReflectedSubObjs.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/BoxCollision.h"
#include "NSlib/CoreObject/SubObjects/Model.h"
#include "NSlib/CoreObject/SubObjects/SphereCollision.h"

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
            box = CreateSubObj<NS::Collision, NS::BoxCollision>(m_collision);
            CreateSubObj(second);
            CreateSubObj(m_model);
            refused = CreateSubObj<NS::Collision, NS::SphereCollision>(m_collision);
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
            model = CreateSubObj(m_model);
        }
    };

    // 基底の OnInit を呼ばずに、共通の部分を InitShape に出す
    class MiddleActor : public NS::SceneActor
    {
    protected:
        void OnInit() override
        {
            InitShape();
            CreateSubObj<NS::Collision, NS::BoxCollision>(m_collision);
        }
        void InitShape() { CreateSubObj(m_model); }
    };

    class GrandActor final : public MiddleActor
    {
    protected:
        void OnInit() override
        {
            InitShape();
            CreateSubObj<NS::Collision, NS::SphereCollision>(m_collision);
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
        CreateSubObj(m_model);
    }
};
NS_CLASS(SubObjectInitProbeActor)

TEST(SubObject, CreateWritesNameAndEnumeratesRootThenSlotsThenDerivedInCreationOrder)
{
    const std::unique_ptr<OrderActor> actor = NS::CreateActor<OrderActor>();
    ASSERT_NE(actor->first, nullptr);
    ASSERT_NE(actor->second, nullptr);
    EXPECT_EQ(actor->SubObjKey(*actor->first), "First");
    EXPECT_EQ(actor->first->Owner(), actor.get());
    EXPECT_EQ(actor->FindSubObj("Second"), actor->second.get());
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

TEST(SubObject, PrototypeJsonListsTheSubObjectsInit)
{
    const nlohmann::json prototype = NS::MakePrototypeJson<SubObjectInitProbeActor>();
    EXPECT_NE(NS::SubObjFields(prototype, "Model"), nullptr);
}
