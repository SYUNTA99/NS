#include "NSlib/Object/Actor.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Reflection/Archetype.h"
#include "NSlib/Object/Reflection/ObjectBuilder.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/SubObjects/BoxCollision.h"
#include "NSlib/Object/SubObjects/Model.h"
#include "NSlib/Object/SubObjects/SphereCollision.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    class PlainSubObj final : public NS::Obj::SubObject
    {};

    class OrderActor final : public NS::Obj::Actor
    {
    public:
        std::unique_ptr<PlainSubObj> first;
        std::unique_ptr<PlainSubObj> second;
        NS::Obj::BoxCollision* box = nullptr;
        NS::Obj::SphereCollision* refused = nullptr;

    protected:
        void VisitExtraSubObjs(const SubObjVisitor& visit) const override { VisitSubObjMembers(visit, first, second); }
        void OnInit() override
        {
            (void)CreateSubObj(first, "First");
            box = CreateSubObj<NS::Obj::Collision, NS::Obj::BoxCollision>(m_collision, "Collision");
            (void)CreateSubObj(second, "Second");
            CreateSubObj(m_model, "Model");
            refused = CreateSubObj<NS::Obj::Collision, NS::Obj::SphereCollision>(m_collision, "Collision");
        }
    };

    class NameClashActor final : public NS::Obj::Actor
    {
    public:
        NS::Obj::Model* model = nullptr;
        std::unique_ptr<PlainSubObj> clash;

    protected:
        void VisitExtraSubObjs(const SubObjVisitor& visit) const override { VisitSubObjMembers(visit, clash); }
        void OnInit() override
        {
            (void)CreateSubObj(clash, "Model");
            model = CreateSubObj(m_model, "Model");
        }
    };

    // 基底の OnInit を呼ばずに、共通の部分を InitShape に出す
    class MiddleActor : public NS::Obj::Actor
    {
    protected:
        void OnInit() override
        {
            InitShape();
            CreateSubObj<NS::Obj::Collision, NS::Obj::BoxCollision>(m_collision, "Collision");
        }
        void InitShape() { CreateSubObj(m_model, "Model"); }
    };

    class GrandActor final : public MiddleActor
    {
    protected:
        void OnInit() override
        {
            InitShape();
            CreateSubObj<NS::Obj::Collision, NS::Obj::SphereCollision>(m_collision, "Collision");
        }
    };

    // Init は作る関数だけが呼ぶ。外から呼べると、1 つの個体に 2 回書ける
    template <class TActor = NS::Obj::Actor> void CheckInitIsClosed()
    {
        static_assert(!requires(TActor& actor) { actor.Init(); });
    }

    // シーンは外で作った個体を受けない。受けると、Init していない個体が一覧に入る
    template <class TScene = NS::Obj::Scene> void CheckSceneBuildsItsOwnActors()
    {
        static_assert(!requires(TScene& scene, std::unique_ptr<NS::Obj::Actor> actor) {
            scene.SpawnTransient(std::move(actor));
        });
        static_assert(!requires(TScene& scene, std::unique_ptr<NS::Obj::Actor> actor) {
            scene.SpawnObject(std::move(actor), std::string{});
        });
    }

} // namespace

class SubObjectInitProbe final : public NS::Obj::Actor
{
public:
    int inits = 0;
    NS_REFLECT_NONE(SubObjectInitProbe, NS::Obj::Actor)

protected:
    void OnInit() override
    {
        ++inits;
        CreateSubObj(m_model, "Model");
    }
};
NS_CLASS(SubObjectInitProbe)

TEST(SubObject, CreateWritesNameAndEnumeratesRootThenSlotsThenDerivedInCreationOrder)
{
    const std::unique_ptr<OrderActor> actor = NS::Obj::CreateActor<OrderActor>();
    ASSERT_NE(actor->first, nullptr);
    ASSERT_NE(actor->second, nullptr);
    EXPECT_EQ(actor->first->Name(), "First");
    EXPECT_EQ(actor->first->Owner(), actor.get());
    EXPECT_EQ(actor->FindSubObj("Second"), actor->second.get());
    ASSERT_NE(actor->CollisionSubObj(), nullptr);
    EXPECT_EQ(actor->CollisionSubObj()->Name(), "Collision");
    std::vector<std::string> names;
    actor->ForEachSubObj([&](const NS::Obj::SubObject* subObject) {
        EXPECT_EQ(subObject->Owner(), actor.get());
        names.push_back(subObject->Name());
    });
    EXPECT_EQ(names, (std::vector<std::string>{"Transform", "Model", "Collision", "First", "Second"}));
}

TEST(SubObject, SecondCreateInTheSameSlotIsRefused)
{
    const std::unique_ptr<OrderActor> actor = NS::Obj::CreateActor<OrderActor>();
    EXPECT_EQ(actor->refused, nullptr);
    EXPECT_EQ(actor->CollisionSubObj(), actor->box);
}

TEST(SubObject, SlotIsRefusedWhenItsNameIsTaken)
{
    const std::unique_ptr<NameClashActor> actor = NS::Obj::CreateActor<NameClashActor>();
    EXPECT_EQ(actor->model, nullptr);
    EXPECT_EQ(actor->ModelSubObj(), nullptr);
}

TEST(SubObject, GrandchildReplacesTheCollisionTypeChosenByTheMiddleBase)
{
    const std::unique_ptr<MiddleActor> middle = NS::Obj::CreateActor<MiddleActor>();
    EXPECT_NE(NS::Obj::Cast<NS::Obj::BoxCollision>(middle->CollisionSubObj()), nullptr);

    const std::unique_ptr<GrandActor> grand = NS::Obj::CreateActor<GrandActor>();
    EXPECT_NE(NS::Obj::Cast<NS::Obj::SphereCollision>(grand->CollisionSubObj()), nullptr);
    EXPECT_NE(grand->ModelSubObj(), nullptr);
}

TEST(SubObject, OnlyTheBuildersCanInitAndEnterAnActor)
{
    CheckInitIsClosed();
    CheckSceneBuildsItsOwnActors();
}

TEST(SubObject, EveryBuildPathRunsOnInitExactlyOnce)
{
    NS::Obj::Scene scene;
    const SubObjectInitProbe* transient = scene.SpawnTransient<SubObjectInitProbe>();
    EXPECT_EQ(transient->inits, 1);
    EXPECT_NE(transient->ModelSubObj(), nullptr);

    const SubObjectInitProbe* placed = scene.SpawnObject<SubObjectInitProbe>("Placed");
    EXPECT_EQ(placed->inits, 1);

    const std::unique_ptr<SubObjectInitProbe> loose = NS::Obj::CreateActor<SubObjectInitProbe>();
    EXPECT_EQ(loose->inits, 1);

    const SubObjectInitProbe* fromJson =
        NS::Obj::Cast<SubObjectInitProbe>(scene.SpawnFromJson(NS::Obj::MakePrototypeJson<SubObjectInitProbe>()));
    ASSERT_NE(fromJson, nullptr);
    EXPECT_EQ(fromJson->inits, 1);

    const NS::Obj::Actor& baseline = NS::Obj::ArchetypeLibrary::Get().Baseline("SubObjectInitProbe");
    const SubObjectInitProbe* probe = NS::Obj::Cast<SubObjectInitProbe>(&baseline);
    ASSERT_NE(probe, nullptr);
    EXPECT_EQ(probe->inits, 1);
    EXPECT_NE(probe->ModelSubObj(), nullptr);
}

TEST(SubObject, PrototypeJsonListsTheSubObjectsInit)
{
    const nlohmann::json prototype = NS::Obj::MakePrototypeJson<SubObjectInitProbe>();
    EXPECT_NE(NS::Obj::SubObjFields(prototype, "Model"), nullptr);
}
