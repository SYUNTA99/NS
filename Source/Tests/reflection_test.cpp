#include <Game/Player/PlayerComponent.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Component.h>
#include <Runtime/Object/Components/BoxColliderComponent.h>
#include <Runtime/Object/Components/CameraBrainComponent.h>
#include <Runtime/Object/Components/MeshRendererComponent.h>
#include <Runtime/Object/Components/PlacedVirtualCamera.h>
#include <Runtime/Object/Components/ShadowComponent.h>
#include <Runtime/Object/Components/ThirdPersonFollowComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <gtest/gtest.h>
#include <string>
#include <string_view>

namespace
{
    using NS::Object::Component;
    using NS::Object::FieldDesc;
    using NS::Object::FieldType;
    using NS::Object::FindField;
    using NS::Object::GameObject;
    using NS::Object::PlacedVirtualCamera;
    using NS::Object::ReflectionInfo;

    // float / int / bool / Vector3 を private に持ち、 4 フィールドをリフレクションするテスト用 Component
    class FakeReflectedComponent : public Component
    {
    public:
        FakeReflectedComponent() noexcept : Component(0) {}

        NS_REFLECT_BEGIN(FakeReflectedComponent, Component)
        NS_REFLECT_FIELD(m_speed, "再生速度")
        NS_REFLECT_FIELD(m_count, "Count")
        NS_REFLECT_FIELD(m_enabled, "Enabled")
        NS_REFLECT_FIELD(m_offset, "Offset")
        NS_REFLECT_END()

        [[nodiscard]] float Speed() const noexcept { return m_speed; }
        [[nodiscard]] int Count() const noexcept { return m_count; }
        [[nodiscard]] bool Enabled() const noexcept { return m_enabled; }
        [[nodiscard]] const NS::Core::Vector3& Offset() const noexcept { return m_offset; }

    private:
        float m_speed = 1.5f;
        int m_count = 3;
        bool m_enabled = true;
        NS::Core::Vector3 m_offset{1.0f, 2.0f, 3.0f};
    };

    // std::string をリフレクションするテスト用 Component
    class FakeStringComponent : public Component
    {
    public:
        FakeStringComponent() noexcept : Component(0) {}

        NS_REFLECT_BEGIN(FakeStringComponent, Component)
        NS_REFLECT_FIELD(m_label, "Label")
        NS_REFLECT_END()

        [[nodiscard]] const std::string& Label() const noexcept { return m_label; }

    private:
        std::string m_label{"hello"};
    };

    // リフレクション宣言を持たない素の Component 派生
    class BareComponent : public Component
    {
    public:
        BareComponent() noexcept : Component(0) {}
    };

    // Component を継承しない素の値型。リフレクションがベース非依存で効くことを確かめる
    struct FakeValueType
    {
        NS_REFLECT_BEGIN(FakeValueType, void)
        NS_REFLECT_FIELD(speed, "再生速度")
        NS_REFLECT_FIELD(name, "Name")
        NS_REFLECT_END_VALUE()

        float speed = 2.5f;
        std::string name{"init"};
    };
} // namespace

TEST(ReflectionTest, GetReflectionListsAllFields)
{
    FakeReflectedComponent comp;
    const ReflectionInfo* info = comp.GetReflection();
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->fieldCount, 4u);

    const FieldDesc* speed = FindField(info, "再生速度");
    const FieldDesc* count = FindField(info, "Count");
    const FieldDesc* enabled = FindField(info, "Enabled");
    const FieldDesc* offset = FindField(info, "Offset");
    ASSERT_NE(speed, nullptr);
    ASSERT_NE(count, nullptr);
    ASSERT_NE(enabled, nullptr);
    ASSERT_NE(offset, nullptr);
    EXPECT_EQ(speed->type, FieldType::Float);
    EXPECT_EQ(count->type, FieldType::Int);
    EXPECT_EQ(enabled->type, FieldType::Bool);
    EXPECT_EQ(offset->type, FieldType::Vector3);

    // 不在名と null info は nullptr
    EXPECT_EQ(FindField(info, "Nope"), nullptr);
    EXPECT_EQ(FindField(nullptr, "再生速度"), nullptr);
}

TEST(ReflectionTest, GetSetRoundTripsFloat)
{
    FakeReflectedComponent comp;
    const FieldDesc* f = FindField(comp.GetReflection(), "再生速度");
    ASSERT_NE(f, nullptr);

    float got = 0.0f;
    f->get(&comp, &got);
    EXPECT_FLOAT_EQ(got, 1.5f);

    float set = 9.25f;
    f->set(&comp, &set);
    EXPECT_FLOAT_EQ(comp.Speed(), 9.25f);
}

TEST(ReflectionTest, GetSetRoundTripsInt)
{
    FakeReflectedComponent comp;
    const FieldDesc* f = FindField(comp.GetReflection(), "Count");
    ASSERT_NE(f, nullptr);

    int got = 0;
    f->get(&comp, &got);
    EXPECT_EQ(got, 3);

    int set = 42;
    f->set(&comp, &set);
    EXPECT_EQ(comp.Count(), 42);
}

TEST(ReflectionTest, GetSetRoundTripsBool)
{
    FakeReflectedComponent comp;
    const FieldDesc* f = FindField(comp.GetReflection(), "Enabled");
    ASSERT_NE(f, nullptr);

    bool got = false;
    f->get(&comp, &got);
    EXPECT_TRUE(got);

    bool set = false;
    f->set(&comp, &set);
    EXPECT_FALSE(comp.Enabled());
}

TEST(ReflectionTest, GetSetRoundTripsVector3)
{
    FakeReflectedComponent comp;
    const FieldDesc* f = FindField(comp.GetReflection(), "Offset");
    ASSERT_NE(f, nullptr);

    NS::Core::Vector3 got{};
    f->get(&comp, &got);
    EXPECT_FLOAT_EQ(got.x, 1.0f);
    EXPECT_FLOAT_EQ(got.y, 2.0f);
    EXPECT_FLOAT_EQ(got.z, 3.0f);

    NS::Core::Vector3 set{4.0f, 5.0f, 6.0f};
    f->set(&comp, &set);
    EXPECT_FLOAT_EQ(comp.Offset().x, 4.0f);
    EXPECT_FLOAT_EQ(comp.Offset().y, 5.0f);
    EXPECT_FLOAT_EQ(comp.Offset().z, 6.0f);
}

TEST(ReflectionTest, BareComponentHasNullReflection)
{
    BareComponent comp;
    EXPECT_EQ(comp.GetReflection(), nullptr);
}

TEST(ReflectionTest, PlacedVirtualCameraReflectsSixFields)
{
    GameObject host;
    auto* cam = host.AddComponent<PlacedVirtualCamera>();
    cam->SetView({1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f});
    cam->SetTrigger({7.0f, 8.0f, 9.0f}, {10.0f, 11.0f, 12.0f});
    cam->SetLookAtPlayer(true);
    cam->SetVcamPriority(15);

    const ReflectionInfo* info = cam->GetReflection();
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->fieldCount, 6u);

    EXPECT_NE(FindField(info, "注視点"), nullptr);
    EXPECT_NE(FindField(info, "上方向"), nullptr);
    EXPECT_NE(FindField(info, "トリガー中心"), nullptr);
    EXPECT_NE(FindField(info, "トリガー半径"), nullptr);
    EXPECT_NE(FindField(info, "プレイヤー追視"), nullptr);
    EXPECT_NE(FindField(info, "優先度"), nullptr);
    // 視点位置は owner Transform 所有なのでリフレクションしない。transform 編集の経路と二重にしない
    EXPECT_EQ(FindField(info, "Camera Pos"), nullptr);

    // Priority は基底 accessor 経由で書き戻る
    const FieldDesc* priority = FindField(info, "優先度");
    ASSERT_NE(priority, nullptr);
    int newPriority = 7;
    priority->set(cam, &newPriority);
    EXPECT_EQ(cam->VcamPriority(), 7);

    // Trigger Center は直メンバ経由で書き戻る
    const FieldDesc* center = FindField(info, "トリガー中心");
    ASSERT_NE(center, nullptr);
    NS::Core::Vector3 newCenter{20.0f, 21.0f, 22.0f};
    center->set(cam, &newCenter);
    EXPECT_FLOAT_EQ(cam->TriggerCenter().x, 20.0f);
    EXPECT_FLOAT_EQ(cam->TriggerCenter().y, 21.0f);
    EXPECT_FLOAT_EQ(cam->TriggerCenter().z, 22.0f);
}

TEST(ReflectionTest, PlayerComponentReflectsFeelFloats)
{
    NS::Game::Player::PlayerComponent move;
    const ReflectionInfo* info = move.GetReflection();
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->fieldCount, 20u);

    // 操作感の代表値が float として往復する
    const FieldDesc* jump = FindField(info, "ジャンプ初速");
    ASSERT_NE(jump, nullptr);
    EXPECT_EQ(jump->type, FieldType::Float);

    float got = 0.0f;
    jump->get(&move, &got);
    EXPECT_FLOAT_EQ(got, 12.0f);

    float set = 20.0f;
    jump->set(&move, &set);
    jump->get(&move, &got);
    EXPECT_FLOAT_EQ(got, 20.0f);
}

TEST(ReflectionTest, MeshRendererReflectsBaseColorMeshAndMaterialRef)
{
    NS::Object::MeshRendererComponent renderer;
    const ReflectionInfo* info = renderer.GetReflection();
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->fieldCount, 3u);

    const FieldDesc* color = FindField(info, "基本色");
    ASSERT_NE(color, nullptr);
    EXPECT_EQ(color->type, FieldType::Vector3);

    NS::Core::Vector3 set{0.2f, 0.3f, 0.4f};
    color->set(&renderer, &set);
    NS::Core::Vector3 got{};
    color->get(&renderer, &got);
    EXPECT_FLOAT_EQ(got.x, 0.2f);
    EXPECT_FLOAT_EQ(got.y, 0.3f);
    EXPECT_FLOAT_EQ(got.z, 0.4f);

    const FieldDesc* mesh = FindField(info, "メッシュ");
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->type, FieldType::String);

    std::string meshSet{"cube"};
    mesh->set(&renderer, &meshSet);
    std::string meshGot;
    mesh->get(&renderer, &meshGot);
    EXPECT_EQ(meshGot, "cube");

    const FieldDesc* material = FindField(info, "マテリアル");
    ASSERT_NE(material, nullptr);
    EXPECT_EQ(material->type, FieldType::String);

    std::string materialSet{"block"};
    material->set(&renderer, &materialSet);
    std::string materialGot;
    material->get(&renderer, &materialGot);
    EXPECT_EQ(materialGot, "block");
}

TEST(ReflectionTest, BoxColliderHalfExtentsAccessorClampsNegative)
{
    NS::Object::BoxColliderComponent collider;
    const ReflectionInfo* info = collider.GetReflection();
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->fieldCount, 4u);

    const FieldDesc* he = FindField(info, "半径");
    ASSERT_NE(he, nullptr);

    NS::Core::Vector3 set{2.0f, 3.0f, 4.0f};
    he->set(&collider, &set);
    EXPECT_FLOAT_EQ(collider.HalfExtents().x, 2.0f);
    EXPECT_FLOAT_EQ(collider.HalfExtents().y, 3.0f);
    EXPECT_FLOAT_EQ(collider.HalfExtents().z, 4.0f);

    // ACCESSOR は setter 経由なので負は 0 にクランプされる。直 FIELD では起きない
    NS::Core::Vector3 negative{-1.0f, 5.0f, -2.0f};
    he->set(&collider, &negative);
    EXPECT_FLOAT_EQ(collider.HalfExtents().x, 0.0f);
    EXPECT_FLOAT_EQ(collider.HalfExtents().y, 5.0f);
    EXPECT_FLOAT_EQ(collider.HalfExtents().z, 0.0f);
}

TEST(ReflectionTest, BoxColliderExposesCenterOffsetAndRotation)
{
    NS::Object::BoxColliderComponent collider;
    const ReflectionInfo* info = collider.GetReflection();
    ASSERT_NE(info, nullptr);

    const FieldDesc* offset = FindField(info, "中心オフセット");
    ASSERT_NE(offset, nullptr);
    NS::Core::Vector3 setOffset{1.0f, -2.0f, 3.0f};
    offset->set(&collider, &setOffset);
    EXPECT_FLOAT_EQ(collider.CenterOffset().x, 1.0f);
    EXPECT_FLOAT_EQ(collider.CenterOffset().y, -2.0f);
    EXPECT_FLOAT_EQ(collider.CenterOffset().z, 3.0f);

    // 回転は Euler(度) アクセサで読み書きし、 往復で一致する
    const FieldDesc* rot = FindField(info, "回転 (度)");
    ASSERT_NE(rot, nullptr);
    NS::Core::Vector3 setRot{0.0f, 90.0f, 0.0f};
    rot->set(&collider, &setRot);
    NS::Core::Vector3 readRot{};
    rot->get(&collider, &readRot);
    EXPECT_NEAR(readRot.y, 90.0f, 1e-3f);
}

TEST(ReflectionTest, ThirdPersonFollowReflectsFeelFields)
{
    NS::Object::ThirdPersonFollowComponent follow;
    const ReflectionInfo* info = follow.GetReflection();
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->fieldCount, 19u);

    const FieldDesc* jump = FindField(info, "ジャンプ時距離");
    ASSERT_NE(jump, nullptr);
    EXPECT_EQ(jump->type, FieldType::Float);
    float got = 0.0f;
    jump->get(&follow, &got);
    EXPECT_FLOAT_EQ(got, 7.0f);
    float set = 9.0f;
    jump->set(&follow, &set);
    jump->get(&follow, &got);
    EXPECT_FLOAT_EQ(got, 9.0f);

    const FieldDesc* invertX = FindField(info, "反転 X");
    ASSERT_NE(invertX, nullptr);
    EXPECT_EQ(invertX->type, FieldType::Bool);

    // 追従先はオブジェクト間参照としてデータ化される
    const FieldDesc* target = FindField(info, "追従対象");
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->type, FieldType::ObjectRef);

    // プレイの遠景を抑える投影値もリフレクションでデータ化される
    const FieldDesc* farPlane = FindField(info, "ファークリップ");
    ASSERT_NE(farPlane, nullptr);
    EXPECT_EQ(farPlane->type, FieldType::Float);

    // プレイ開始時の向きは editor のギズモ / Inspector が data 保存し、 OnStart で現在 yaw へ写る
    const FieldDesc* initialYaw = FindField(info, "初期ヨー");
    ASSERT_NE(initialYaw, nullptr);
    EXPECT_EQ(initialYaw->type, FieldType::Float);
}

TEST(ReflectionTest, CameraBrainBlendDurationAccessorClampsNegative)
{
    NS::Object::CameraBrainComponent brain;
    const ReflectionInfo* info = brain.GetReflection();
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->fieldCount, 1u);

    const FieldDesc* blend = FindField(info, "ブレンド秒数");
    ASSERT_NE(blend, nullptr);
    float set = -1.0f;
    blend->set(&brain, &set);
    EXPECT_FLOAT_EQ(brain.BlendDuration(), 0.0f); // ACCESSOR は setter 経由でクランプ
}

TEST(ReflectionTest, ShadowReflectsAppearanceFields)
{
    NS::Object::ShadowComponent shadow;
    const ReflectionInfo* info = shadow.GetReflection();
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->fieldCount, 4u);
    EXPECT_NE(FindField(info, "基本不透明度"), nullptr);
}

TEST(ReflectionTest, FieldTypeOfStringIsString)
{
    EXPECT_EQ(NS::Object::FieldTypeOf<std::string>(), FieldType::String);
}

TEST(ReflectionTest, StringFieldGetReturnsInitial)
{
    FakeStringComponent comp;
    const FieldDesc* f = FindField(comp.GetReflection(), "Label");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->type, FieldType::String);

    std::string out;
    f->get(&comp, &out);
    EXPECT_EQ(out, "hello");
}

TEST(ReflectionTest, StringFieldSetRoundTrips)
{
    FakeStringComponent comp;
    const FieldDesc* f = FindField(comp.GetReflection(), "Label");
    ASSERT_NE(f, nullptr);

    std::string in = "world";
    f->set(&comp, &in);
    EXPECT_EQ(comp.Label(), "world");
}

TEST(ReflectionTest, FieldTypeOfObjectRefIsObjectRef)
{
    EXPECT_EQ(NS::Object::FieldTypeOf<NS::Object::ObjectRef>(), FieldType::ObjectRef);
}

TEST(ReflectionValueTypeTest, ReflectsNonComponentStruct)
{
    FakeValueType v;
    const ReflectionInfo* info = FakeValueType::StaticReflection();
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->fieldCount, 2u);
    EXPECT_EQ(info->base, nullptr); // void 基底は鎖の終端

    const FieldDesc* speed = FindField(info, "再生速度");
    ASSERT_NE(speed, nullptr);
    EXPECT_EQ(speed->type, FieldType::Float);
    float got = 0.0f;
    speed->get(&v, &got);
    EXPECT_FLOAT_EQ(got, 2.5f);
    float set = 8.0f;
    speed->set(&v, &set);
    EXPECT_FLOAT_EQ(v.speed, 8.0f);

    const FieldDesc* name = FindField(info, "Name");
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->type, FieldType::String);
    std::string nameGot;
    name->get(&v, &nameGot);
    EXPECT_EQ(nameGot, "init");
}

TEST(ReflectionIsATest, MatchesSelfAndBaseChain)
{
    NS::Object::ThirdPersonFollowComponent follow;
    EXPECT_TRUE(follow.IsA(NS::Object::ThirdPersonFollowComponent::StaticReflection()));
    EXPECT_TRUE(follow.IsA(NS::Object::VirtualCameraComponent::StaticReflection()));
}

TEST(ReflectionIsATest, RejectsUnrelatedTypeAndNull)
{
    NS::Object::ThirdPersonFollowComponent follow;
    EXPECT_FALSE(follow.IsA(NS::Object::CameraBrainComponent::StaticReflection()));
    EXPECT_FALSE(follow.IsA(nullptr));

    // リフレクションを持たない素の派生はどの検索にも一致しない
    BareComponent bare;
    EXPECT_FALSE(bare.IsA(NS::Object::CameraBrainComponent::StaticReflection()));
}

TEST(ReflectionIsATest, StaticAndVirtualShareOneInfo)
{
    // 静的関数と仮想関数が同じ実体を返す。二重定義があると is-a のアドレス比較が壊れる
    NS::Object::ThirdPersonFollowComponent follow;
    EXPECT_EQ(follow.GetReflection(), NS::Object::ThirdPersonFollowComponent::StaticReflection());

    PlacedVirtualCamera cam;
    EXPECT_EQ(cam.GetReflection(), PlacedVirtualCamera::StaticReflection());
}

TEST(ReflectionComponentCastTest, CastsSelfAndBaseRejectsOthers)
{
    NS::Object::ThirdPersonFollowComponent follow;
    Component* comp = &follow;
    EXPECT_EQ(NS::Object::ComponentCast<NS::Object::ThirdPersonFollowComponent>(comp), &follow);
    EXPECT_EQ(NS::Object::ComponentCast<NS::Object::VirtualCameraComponent>(comp),
              static_cast<NS::Object::VirtualCameraComponent*>(&follow));
    EXPECT_EQ(NS::Object::ComponentCast<NS::Object::CameraBrainComponent>(comp), nullptr);
    EXPECT_EQ(NS::Object::ComponentCast<NS::Object::ThirdPersonFollowComponent>(static_cast<Component*>(nullptr)),
              nullptr);
}

TEST(ReflectionComponentCastTest, ConstOverloadMatchesNonConst)
{
    NS::Object::ThirdPersonFollowComponent follow;
    const Component* comp = &follow;
    EXPECT_EQ(NS::Object::ComponentCast<NS::Object::ThirdPersonFollowComponent>(comp), &follow);
    EXPECT_EQ(NS::Object::ComponentCast<NS::Object::CameraBrainComponent>(comp), nullptr);
}

TEST(ReflectionComponentCastTest, CastsTypeWithRenderableSide)
{
    // Component + IRenderable の多重継承でも Component* からの下向き static_cast が成立する
    NS::Object::ShadowComponent shadow;
    Component* comp = &shadow;
    EXPECT_EQ(NS::Object::ComponentCast<NS::Object::ShadowComponent>(comp), &shadow);
}
