#include "NSlib/Object/SceneActor.h"
#include "NSlib/Object/Reflection/ReflectedSubObjs.h"
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

    class OrderActor final : public NS::Obj::SceneActor
    {
    public:
        NS_REFLECT_BEGIN(OrderActor, NS::Obj::SceneActor)
        NS_REFLECT_SUBOBJ(first, "First")
        NS_REFLECT_SUBOBJ(second, "Second")
        NS_REFLECT_END()

        std::unique_ptr<PlainSubObj> first;
        std::unique_ptr<PlainSubObj> second;
        NS::Obj::BoxCollision* box = nullptr;
        NS::Obj::SphereCollision* refused = nullptr;

    protected:
        void OnInit() override
        {
            CreateSubObj(first);
            box = CreateSubObj<NS::Obj::Collision, NS::Obj::BoxCollision>(m_collision);
            CreateSubObj(second);
            CreateSubObj(m_model);
            refused = CreateSubObj<NS::Obj::Collision, NS::Obj::SphereCollision>(m_collision);
        }
    };

    class NameClashActor final : public NS::Obj::SceneActor
    {
    public:
        NS_REFLECT_BEGIN(NameClashActor, NS::Obj::SceneActor)
        NS_REFLECT_SUBOBJ(clash, "Model")
        NS_REFLECT_END()

        NS::Obj::Model* model = nullptr;
        std::unique_ptr<PlainSubObj> clash;

    protected:
        void OnInit() override
        {
            CreateSubObj(clash);
            model = CreateSubObj(m_model);
        }
    };

    // 基底の OnInit を呼ばずに、共通の部分を InitShape に出す
    class MiddleActor : public NS::Obj::SceneActor
    {
    protected:
        void OnInit() override
        {
            InitShape();
            CreateSubObj<NS::Obj::Collision, NS::Obj::BoxCollision>(m_collision);
        }
        void InitShape() { CreateSubObj(m_model); }
    };

    class GrandActor final : public MiddleActor
    {
    protected:
        void OnInit() override
        {
            InitShape();
            CreateSubObj<NS::Obj::Collision, NS::Obj::SphereCollision>(m_collision);
        }
    };

    // Init は作る関数だけが呼ぶ。外から呼べると、1 つの個体に 2 回書ける
    template <class TActor = NS::Obj::SceneActor> void CheckInitIsClosed()
    {
        static_assert(!requires(TActor& actor) { actor.Init(); });
    }

    // シーンは外で作った個体を受けない。受けると、Init していない個体が一覧に入る
    template <class TScene = NS::Obj::Scene> void CheckSceneBuildsItsOwnActors()
    {
        static_assert(!requires(TScene& scene, std::unique_ptr<NS::Obj::SceneActor> actor) {
            scene.SpawnTransient(std::move(actor));
        });
        static_assert(!requires(TScene& scene, std::unique_ptr<NS::Obj::SceneActor> actor) {
            scene.SpawnObject(std::move(actor), std::string{});
        });
    }

} // namespace

class SubObjectInitProbeActor final : public NS::Obj::SceneActor
{
public:
    int inits = 0;
    NS_REFLECT_NONE(SubObjectInitProbeActor, NS::Obj::SceneActor)

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
    const std::unique_ptr<OrderActor> actor = NS::Obj::CreateActor<OrderActor>();
    ASSERT_NE(actor->first, nullptr);
    ASSERT_NE(actor->second, nullptr);
    EXPECT_EQ(actor->SubObjKey(*actor->first), "First");
    EXPECT_EQ(actor->first->Owner(), actor.get());
    EXPECT_EQ(actor->FindSubObj("Second"), actor->second.get());
    ASSERT_NE(actor->CollisionSubObj(), nullptr);
    EXPECT_EQ(actor->SubObjKey(*actor->CollisionSubObj()), "Collision");
    std::vector<std::string> names;
    NS::Obj::ForEachReflectedSubObj(*actor,
                                    [&](const NS::Obj::SubObjectDesc& field, const NS::Obj::SubObject* subObject) {
        EXPECT_EQ(subObject->Owner(), actor.get());
        names.push_back(field.key);
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
    const SubObjectInitProbeActor* transient = scene.SpawnTransient<SubObjectInitProbeActor>();
    EXPECT_EQ(transient->inits, 1);
    EXPECT_NE(transient->ModelSubObj(), nullptr);

    const SubObjectInitProbeActor* placed = scene.SpawnObject<SubObjectInitProbeActor>("Placed");
    EXPECT_EQ(placed->inits, 1);

    const std::unique_ptr<SubObjectInitProbeActor> loose = NS::Obj::CreateActor<SubObjectInitProbeActor>();
    EXPECT_EQ(loose->inits, 1);

    const SubObjectInitProbeActor* fromJson =
        NS::Obj::Cast<SubObjectInitProbeActor>(scene.SpawnFromJson(NS::Obj::MakePrototypeJson<SubObjectInitProbeActor>()));
    ASSERT_NE(fromJson, nullptr);
    EXPECT_EQ(fromJson->inits, 1);

    const NS::Obj::SceneActor& baseline = NS::Obj::ArchetypeLibrary::Get().Baseline("SubObjectInitProbeActor");
    const SubObjectInitProbeActor* probe = NS::Obj::Cast<SubObjectInitProbeActor>(&baseline);
    ASSERT_NE(probe, nullptr);
    EXPECT_EQ(probe->inits, 1);
    EXPECT_NE(probe->ModelSubObj(), nullptr);
}

TEST(SubObject, PrototypeJsonListsTheSubObjectsInit)
{
    const nlohmann::json prototype = NS::Obj::MakePrototypeJson<SubObjectInitProbeActor>();
    EXPECT_NE(NS::Obj::SubObjFields(prototype, "Model"), nullptr);
}
