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

#include <string>
#include <vector>

namespace
{
    class PlainSubObj final : public NS::Obj::SubObject
    {};

    class OrderActor final : public NS::Obj::Actor
    {
    public:
        PlainSubObj* first = nullptr;
        PlainSubObj* second = nullptr;
        NS::Obj::BoxCollision* box = nullptr;
        NS::Obj::SphereCollision* refused = nullptr;

    protected:
        void OnInit() override
        {
            first = CreateSubObj<PlainSubObj>("First");
            box = CreateSubObj<NS::Obj::BoxCollision>(CollisionSlot());
            second = CreateSubObj<PlainSubObj>("Second");
            CreateSubObj<NS::Obj::Model>(ModelSlot());
            refused = CreateSubObj<NS::Obj::SphereCollision>(CollisionSlot());
        }
    };

    class NameClashActor final : public NS::Obj::Actor
    {
    public:
        NS::Obj::Model* model = nullptr;

    protected:
        void OnInit() override
        {
            CreateSubObj<PlainSubObj>("Model");
            model = CreateSubObj<NS::Obj::Model>(ModelSlot());
        }
    };

    // 基底の OnInit を呼ばずに、共通の部分を InitShape に出す
    class MiddleActor : public NS::Obj::Actor
    {
    protected:
        void OnInit() override
        {
            InitShape();
            CreateSubObj<NS::Obj::BoxCollision>(CollisionSlot());
        }
        void InitShape() { CreateSubObj<NS::Obj::Model>(ModelSlot()); }
    };

    class GrandActor final : public MiddleActor
    {
    protected:
        void OnInit() override
        {
            InitShape();
            CreateSubObj<NS::Obj::SphereCollision>(CollisionSlot());
        }
    };

    class SlotProbe final : public NS::Obj::Actor
    {
    public:
        // 枠の型の派生だけを作れる。requires は templated の中でないと誤りを偽にできない
        template <class TSelf = SlotProbe> static void CheckSlotTypes()
        {
            static_assert(
                requires(TSelf& self) { self.template CreateSubObj<NS::Obj::SphereCollision>(self.CollisionSlot()); });
            static_assert(!requires(TSelf& self) { self.template CreateSubObj<NS::Obj::Model>(self.CollisionSlot()); });
            static_assert(
                !requires(TSelf& self) { self.template CreateSubObj<NS::Obj::BoxCollision>(self.ModelSlot()); });
        }
    };

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
        CreateSubObj<NS::Obj::Model>(ModelSlot());
    }
};
NS_CLASS(SubObjectInitProbe)

TEST(SubObject, CreateWritesNameAndEnumeratesRootThenSlotsThenDerivedInCreationOrder)
{
    OrderActor actor;
    actor.Init();
    ASSERT_NE(actor.first, nullptr);
    ASSERT_NE(actor.second, nullptr);
    EXPECT_EQ(actor.first->Name(), "First");
    EXPECT_EQ(actor.first->Owner(), &actor);
    EXPECT_EQ(actor.FindSubObj("Second"), actor.second);
    ASSERT_NE(actor.CollisionSubObj(), nullptr);
    EXPECT_EQ(actor.CollisionSubObj()->Name(), "Collision");
    std::vector<std::string> names;
    for (const NS::Obj::SubObject* subObject : actor.SubObjs())
    {
        EXPECT_EQ(subObject->Owner(), &actor);
        names.push_back(subObject->Name());
    }
    EXPECT_EQ(names, (std::vector<std::string>{"Transform", "Model", "Collision", "First", "Second"}));
}

TEST(SubObject, SecondCreateInTheSameSlotIsRefused)
{
    OrderActor actor;
    actor.Init();
    EXPECT_EQ(actor.refused, nullptr);
    EXPECT_EQ(actor.CollisionSubObj(), actor.box);
}

TEST(SubObject, SlotIsRefusedWhenItsNameIsTaken)
{
    NameClashActor actor;
    actor.Init();
    EXPECT_EQ(actor.model, nullptr);
    EXPECT_EQ(actor.ModelSubObj(), nullptr);
}

TEST(SubObject, GrandchildReplacesTheCollisionTypeChosenByTheMiddleBase)
{
    MiddleActor middle;
    middle.Init();
    EXPECT_NE(NS::Obj::Cast<NS::Obj::BoxCollision>(middle.CollisionSubObj()), nullptr);

    GrandActor grand;
    grand.Init();
    EXPECT_NE(NS::Obj::Cast<NS::Obj::SphereCollision>(grand.CollisionSubObj()), nullptr);
    EXPECT_NE(grand.ModelSubObj(), nullptr);
}

TEST(SubObject, SlotAcceptsOnlyDerivedTypesOfItsBase)
{
    SlotProbe::CheckSlotTypes();
}

TEST(SubObject, ActorInSceneAndBaselineAreInitializedExactlyOnce)
{
    NS::Obj::Scene scene;
    SubObjectInitProbe* placed = scene.SpawnTransient<SubObjectInitProbe>();
    EXPECT_EQ(placed->inits, 1);
    EXPECT_NE(placed->ModelSubObj(), nullptr);
    placed->Init();
    EXPECT_EQ(placed->inits, 1);

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
