#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/SubObjects/Model.h"
#include "NSlib/Object/UIActor.h"

#include <type_traits>

#include <gtest/gtest.h>

template <class T> constexpr bool k_HasPersistentId = requires(const T& object) { object.Id(); };
template <class T> constexpr bool k_HasObjectName = requires(const T& object) { object.Name(); };
template <class T>
constexpr bool k_CanCreateTypedSubObjFromOutside =
    requires(T& actor) { actor.template CreateSubObj<NS::Obj::Model>("Model"); };
template <class T>
constexpr bool k_HasPartTypeSearch = requires(T& actor) { actor.template FindSubObject<NS::Obj::Model>(); };
static_assert(!k_HasPersistentId<NS::Obj::Object>);
static_assert(!k_HasObjectName<NS::Obj::Object>);
static_assert(k_HasPersistentId<NS::Obj::Actor>);
static_assert(k_HasObjectName<NS::Obj::Actor>);
// 世界に置く物と画面は Object から別々に派生し、画面はシーンへの問いの口を継がない
static_assert(std::is_base_of_v<NS::Obj::Object, NS::Obj::Actor>);
static_assert(std::is_base_of_v<NS::Obj::Object, NS::Obj::UIActor>);
static_assert(!std::is_base_of_v<NS::Obj::Actor, NS::Obj::UIActor>);
static_assert(!std::is_base_of_v<NS::Obj::UIActor, NS::Obj::Actor>);
static_assert(!std::is_base_of_v<NS::Obj::IUseCamera, NS::Obj::UIActor>);
static_assert(!std::is_base_of_v<NS::Obj::IUseSceneSystem, NS::Obj::UIActor>);
// エフェクトの窓口は保持役の枠を持つ個体だけが継ぐ。シーンは出す先の EffectScene を持つだけで継がない
static_assert(std::is_base_of_v<NS::Obj::IUseEffect, NS::Obj::Actor>);
static_assert(!std::is_base_of_v<NS::Obj::IUseEffect, NS::Obj::Scene>);
static_assert(!std::is_base_of_v<NS::Obj::IUseEffect, NS::Obj::UIActor>);
static_assert(!k_HasObjectName<NS::Obj::UIActor>);
static_assert(!k_HasPersistentId<NS::Obj::SubObject>);
static_assert(k_HasObjectName<NS::Obj::SubObject>);
static_assert(!k_CanCreateTypedSubObjFromOutside<NS::Obj::Actor>);
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
    EXPECT_EQ(NS::Obj::Cast<NS::Obj::Model>(object), nullptr);
    EXPECT_STREQ(object->ClassName(), "ReflectedActor");
    const NS::Obj::Object* constant = object;
    EXPECT_EQ(NS::Obj::Cast<ReflectedActor>(constant), &actor);
    EXPECT_EQ(NS::Obj::Cast<ReflectedActor>(static_cast<NS::Obj::Object*>(nullptr)), nullptr);
    NS::Obj::Model mesh;
    EXPECT_EQ(NS::Obj::Cast<NS::Obj::SubObject>(&mesh), &mesh);
    EXPECT_EQ(NS::Obj::Cast<NS::Obj::Actor>(&mesh), nullptr);
}
