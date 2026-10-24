#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/UIActor.h"

#include <type_traits>

#include <gtest/gtest.h>

template <class T> constexpr bool k_HasPersistentId = requires(const T& object) { object.Id(); };
template <class T> constexpr bool k_HasObjectName = requires(const T& object) { object.Name(); };
template <class T>
constexpr bool k_CanCreateTypedClassObjFromOutside =
    requires(T& actor) { actor.template CreateClassObj<NS::Model>("Model"); };
template <class T>
constexpr bool k_HasPartTypeSearch = requires(T& actor) { actor.template FindClassObj<NS::Model>(); };
static_assert(!k_HasPersistentId<NS::Object>);
static_assert(!k_HasObjectName<NS::Object>);
static_assert(k_HasPersistentId<NS::SceneActor>);
static_assert(k_HasObjectName<NS::SceneActor>);
// 世界に置く物と画面は Object から別々に派生し、画面はシーンへの問いの口を継がない
static_assert(std::is_base_of_v<NS::Object, NS::SceneActor>);
static_assert(std::is_base_of_v<NS::Object, NS::UIActor>);
static_assert(!std::is_base_of_v<NS::SceneActor, NS::UIActor>);
static_assert(!std::is_base_of_v<NS::UIActor, NS::SceneActor>);
static_assert(!std::is_base_of_v<NS::IUseCamera, NS::UIActor>);
static_assert(!std::is_base_of_v<NS::IUseSceneSystem, NS::UIActor>);
// エフェクトの窓口は保持役の枠を持つ個体だけが継ぐ。シーンは出す先の EffectScene を持つだけで継がない
static_assert(std::is_base_of_v<NS::IUseEffect, NS::SceneActor>);
static_assert(!std::is_base_of_v<NS::IUseEffect, NS::Scene>);
static_assert(!std::is_base_of_v<NS::IUseEffect, NS::UIActor>);
static_assert(!k_HasObjectName<NS::UIActor>);
static_assert(!k_CanCreateTypedClassObjFromOutside<NS::SceneActor>);
static_assert(!k_HasPartTypeSearch<NS::SceneActor>);

namespace
{
    class ReflectedActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_NONE(ReflectedActor, NS::SceneActor)
    };
} // namespace

TEST(ObjectType, ActorAndPartsShareTheReflectionCastWithoutRtti)
{
    ReflectedActor actor;
    NS::Object* object = &actor;
    EXPECT_EQ(NS::Cast<ReflectedActor>(object), &actor);
    EXPECT_EQ(NS::Cast<NS::SceneActor>(object), &actor);
    EXPECT_EQ(NS::Cast<NS::Model>(object), nullptr);
    EXPECT_STREQ(object->ClassName(), "ReflectedActor");
    const NS::Object* constant = object;
    EXPECT_EQ(NS::Cast<ReflectedActor>(constant), &actor);
    EXPECT_EQ(NS::Cast<ReflectedActor>(static_cast<NS::Object*>(nullptr)), nullptr);
    NS::Model mesh;
    EXPECT_EQ(NS::Cast<NS::Model>(&mesh), &mesh);
    EXPECT_EQ(NS::Cast<NS::SceneActor>(&mesh), nullptr);
}
