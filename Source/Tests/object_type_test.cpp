#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Components/Model.h"

#include <gtest/gtest.h>

template <class T> constexpr bool k_HasPersistentId = requires(const T& object) { object.Id(); };
template <class T> constexpr bool k_HasObjectName = requires(const T& object) { object.Name(); };
template <class T> constexpr bool k_HasPartArray = requires(T& actor) { actor.Components(); };
template <class T>
constexpr bool k_HasDynamicPartAddition = requires(T& actor) { actor.template AddComponent<NS::Obj::Model>(); };
template <class T>
constexpr bool k_HasPartTypeSearch = requires(T& actor) { actor.template FindComponent<NS::Obj::Model>(); };
static_assert(!k_HasPersistentId<NS::Obj::Object>);
static_assert(!k_HasObjectName<NS::Obj::Object>);
static_assert(k_HasPersistentId<NS::Obj::Actor>);
static_assert(k_HasObjectName<NS::Obj::ActorBase>);
static_assert(!k_HasPersistentId<NS::Obj::Component>);
static_assert(!k_HasObjectName<NS::Obj::Component>);
static_assert(!k_HasPartArray<NS::Obj::Actor>);
static_assert(!k_HasDynamicPartAddition<NS::Obj::Actor>);
static_assert(!k_HasPartTypeSearch<NS::Obj::Actor>);

namespace
{
    class ReflectedActor final : public NS::Obj::Actor
    {
    public:
        NS_REFLECT_NONE(ReflectedActor, NS::Obj::Actor)
    };
} // namespace

TEST(ObjectType, ActorAndPartsShareTheReflectionCastWithoutRtti)
{
    ReflectedActor actor;
    NS::Obj::Object* object = &actor;
    EXPECT_EQ(NS::Obj::Cast<ReflectedActor>(object), &actor);
    EXPECT_EQ(NS::Obj::Cast<NS::Obj::Actor>(object), &actor);
    EXPECT_EQ(NS::Obj::Cast<NS::Obj::ActorBase>(object), &actor);
    EXPECT_EQ(NS::Obj::Cast<NS::Obj::Model>(object), nullptr);
    EXPECT_STREQ(object->ClassName(), "ReflectedActor");
    const NS::Obj::Object* constant = object;
    EXPECT_EQ(NS::Obj::Cast<ReflectedActor>(constant), &actor);
    EXPECT_EQ(NS::Obj::Cast<ReflectedActor>(static_cast<NS::Obj::Object*>(nullptr)), nullptr);
    NS::Obj::Model mesh;
    EXPECT_EQ(NS::Obj::Cast<NS::Obj::Component>(&mesh), &mesh);
    EXPECT_EQ(NS::Obj::Cast<NS::Obj::Actor>(&mesh), nullptr);
}
