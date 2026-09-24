#include <Runtime/Object/Component.h>
#include <Runtime/Object/Components/BoxCollider.h>
#include <Runtime/Object/Components/SphereCollider.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/ObjectJson.h>
#include <Runtime/Object/ObjectList.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Reflection/ComponentRef.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Reflection/ReflectionJson.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Scene/SceneJson.h>

#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace
{
    using NS::Obj::BoxCollider;
    using NS::Obj::Component;
    using NS::Obj::ComponentRef;
    using NS::Obj::ComponentRefValue;
    using NS::Obj::GameObject;
    using NS::Obj::Scene;
    using NS::Obj::SphereCollider;

    // 別の配置物の箱を名指しする欄を持つだけの Component。欄の型が選べる相手を BoxCollider に絞る
    class GateSwitch : public Component
    {
    public:
        ComponentRef<BoxCollider> m_gate;

        NS_REFLECT_BEGIN(GateSwitch, Component)
        NS_REFLECT_FIELD(m_gate, "開ける相手")
        NS_REFLECT_END()
    };

    // 配置物 1 体を名前付きで置く。id は SpawnObject が配置物と全 component に振る
    GameObject& SpawnNamed(Scene& scene, const char* name, std::unique_ptr<GameObject> object)
    {
        return *scene.SpawnObject(std::move(object), name);
    }

    // 参照の欄をメモリ上の形 {"ref": 持ち主, "component": Component} で書く
    nlohmann::json RefValue(std::uint32_t object, std::uint32_t component)
    {
        nlohmann::json value;
        value["ref"] = object;
        value["component"] = component;
        return value;
    }
} // namespace

// ---- 名前 ----

// 積んだ時点で型名が付き、同じ配置物の中で重なれば番号が付く
TEST(ComponentName, AddedComponentsGetUniqueTypeNames)
{
    GameObject object;
    BoxCollider* first = object.AddComponent<BoxCollider>();
    BoxCollider* second = object.AddComponent<BoxCollider>();
    SphereCollider* sphere = object.AddComponent<SphereCollider>();

    EXPECT_EQ(first->Name(), "BoxCollider");
    EXPECT_EQ(second->Name(), "BoxCollider_1");
    EXPECT_EQ(sphere->Name(), "SphereCollider");
    EXPECT_EQ(object.FindComponentByName("BoxCollider_1"), second);
    EXPECT_EQ(object.FindComponentByName("Missing"), nullptr);
}

// 改名は配置物の中で一意になるよう補正し、空は型名へ戻す
TEST(ComponentName, RenameStaysUniqueWithinOwner)
{
    GameObject object;
    BoxCollider* first = object.AddComponent<BoxCollider>();
    BoxCollider* second = object.AddComponent<BoxCollider>();

    object.RenameComponent(*second, "Gate");
    EXPECT_EQ(second->Name(), "Gate");
    object.RenameComponent(*first, "Gate");
    EXPECT_EQ(first->Name(), "Gate_1");
    // 自分の名前へ付け直しても番号は増えない
    object.RenameComponent(*second, "Gate");
    EXPECT_EQ(second->Name(), "Gate");
    object.RenameComponent(*first, "");
    EXPECT_EQ(first->Name(), "BoxCollider");
}

// 保存された名前は組み立てで実体へ戻り、id は ObjectList が書く
TEST(ComponentName, LoadedComponentsKeepSavedNamesAndIds)
{
    nlohmann::json data = NS::Obj::MakeSceneJson();
    nlohmann::json object = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonName(object, "Gate");
    nlohmann::json door = NS::Obj::MakeComponentEntry("BoxCollider");
    NS::Obj::SetComponentEntryName(door, "Door");
    NS::Obj::ObjectJsonComponents(object).push_back(std::move(door));
    NS::Obj::ObjectJsonComponents(object).push_back(NS::Obj::MakeComponentEntry("BoxCollider"));
    NS::Obj::SceneJsonObjects(data).push_back(std::move(object));
    NS::Obj::EnsureUniqueObjectIds(data);
    const nlohmann::json& placed = NS::Obj::SceneJsonObjects(data)[0];
    const std::uint32_t objectId = NS::Obj::ObjectJsonId(placed);
    const std::uint32_t doorId = NS::Obj::ComponentEntryId(NS::Obj::ObjectJsonComponents(placed)[0]);

    Scene scene;
    scene.LoadJson(std::move(data));

    GameObject* live = scene.Objects().FindObject(NS::Obj::ObjectRef{objectId});
    ASSERT_NE(live, nullptr);
    Component* liveDoor = live->FindComponentByName("Door");
    ASSERT_NE(liveDoor, nullptr);
    EXPECT_EQ(liveDoor->Id(), doorId);
    // 名前の無い件は型名が付く
    EXPECT_NE(live->FindComponentByName("BoxCollider"), nullptr);
}

// ---- 参照の解決 ----

TEST(ComponentRefTest, ResolvesByIdAndChecksType)
{
    Scene scene;
    std::unique_ptr<GameObject> gate = std::make_unique<GameObject>();
    BoxCollider* box = gate->AddComponent<BoxCollider>();
    GameObject& placed = SpawnNamed(scene, "Gate", std::move(gate));
    ASSERT_NE(box->Id(), 0u);

    ComponentRef<BoxCollider> ref;
    ref.object = placed.Id();
    ref.component = box->Id();
    EXPECT_EQ(scene.Objects().FindComponent(ref), box);

    // 同じ相手でも欄の型が合わなければ見つからない扱い
    ComponentRef<SphereCollider> wrongType;
    wrongType.object = placed.Id();
    wrongType.component = box->Id();
    EXPECT_EQ(scene.Objects().FindComponent(wrongType), nullptr);

    EXPECT_EQ(scene.Objects().FindComponent(ComponentRef<BoxCollider>{}), nullptr);
}

// 実行中は id で持つので、改名しても参照は切れない
TEST(ComponentRefTest, RenameKeepsReference)
{
    Scene scene;
    std::unique_ptr<GameObject> gate = std::make_unique<GameObject>();
    BoxCollider* box = gate->AddComponent<BoxCollider>();
    GameObject& placed = SpawnNamed(scene, "Gate", std::move(gate));

    ComponentRef<BoxCollider> ref;
    ref.object = placed.Id();
    ref.component = box->Id();
    placed.RenameComponent(*box, "Door");

    EXPECT_EQ(scene.Objects().FindComponent(ref), box);
}

// 相手が消えたら引けない。ポインタを控えないので、消えた相手を指し続けない
TEST(ComponentRefTest, DestroyedOwnerIsNotResolved)
{
    Scene scene;
    std::unique_ptr<GameObject> gate = std::make_unique<GameObject>();
    BoxCollider* box = gate->AddComponent<BoxCollider>();
    GameObject& placed = SpawnNamed(scene, "Gate", std::move(gate));

    ComponentRef<BoxCollider> ref;
    ref.object = placed.Id();
    ref.component = box->Id();
    scene.DestroyObject(placed.Id());

    EXPECT_EQ(scene.Objects().FindComponent(ref), nullptr);
}

// ---- リフレクション ----

// 欄の型は ComponentRef で、選べる相手の型を持つ。値は型を問わない ComponentRefValue で受け渡す
TEST(ComponentRefTest, ReflectedFieldCarriesAllowedType)
{
    const NS::Obj::FieldDesc* field = NS::Obj::FindField(GateSwitch::StaticReflection(), "開ける相手");
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->type, NS::Obj::FieldType::ComponentRef);
    ASSERT_NE(field->refType, nullptr);
    EXPECT_EQ(field->refType(), BoxCollider::StaticReflection());

    GateSwitch gateSwitch;
    const ComponentRefValue written{3u, 7u};
    field->set(&gateSwitch, &written);
    EXPECT_EQ(gateSwitch.m_gate.object, 3u);
    EXPECT_EQ(gateSwitch.m_gate.component, 7u);

    ComponentRefValue read{};
    field->get(&gateSwitch, &read);
    EXPECT_EQ(read, written);
}

// メモリ上の JSON は {"ref": 持ち主, "component": Component}。読み戻すと同じ値になる
TEST(ComponentRefTest, JsonRoundTrip)
{
    GateSwitch source;
    source.m_gate.object = 5u;
    source.m_gate.component = 9u;

    const nlohmann::json serialized = NS::Obj::SerializeComponent(source);
    const nlohmann::json& value = serialized.at("fields").at("開ける相手");
    EXPECT_EQ(value.at("ref").get<std::uint32_t>(), 5u);
    EXPECT_EQ(value.at("component").get<std::uint32_t>(), 9u);

    GateSwitch restored;
    NS::Obj::ApplyJsonFields(restored, serialized.at("fields"));
    EXPECT_EQ(restored.m_gate.object, 5u);
    EXPECT_EQ(restored.m_gate.component, 9u);
}

// ---- 保存形式 ----

namespace
{
    // [0] は箱を名指しする配置物、[1] は名前 Gate の配置物で、名前 Door の箱を持つ
    nlohmann::json MakeSwitchAndGate(std::uint32_t& outGateId, std::uint32_t& outDoorId)
    {
        nlohmann::json data = NS::Obj::MakeSceneJson();
        nlohmann::json& objects = NS::Obj::SceneJsonObjects(data);
        objects.push_back(NS::Obj::MakeObjectJson());
        objects.push_back(NS::Obj::MakeObjectJson());
        NS::Obj::SetObjectJsonName(objects[1], "Gate");
        nlohmann::json door = NS::Obj::MakeComponentEntry("BoxCollider");
        NS::Obj::SetComponentEntryName(door, "Door");
        NS::Obj::ObjectJsonComponents(objects[1]).push_back(std::move(door));
        NS::Obj::ObjectJsonComponents(objects[0]).push_back(NS::Obj::MakeComponentEntry("GateSwitch"));
        NS::Obj::EnsureUniqueObjectIds(data);

        outGateId = NS::Obj::ObjectJsonId(objects[1]);
        outDoorId = NS::Obj::ComponentEntryId(NS::Obj::ObjectJsonComponents(objects[1])[0]);
        NS::Obj::ObjectJsonComponents(objects[0])[0]["fields"]["開ける相手"] = RefValue(outGateId, outDoorId);
        return data;
    }

    // 先頭の配置物 (箱を名指しする側) の先頭 component の参照の欄
    const nlohmann::json& SwitchRefValue(const nlohmann::json& data)
    {
        return NS::Obj::ObjectJsonComponents(NS::Obj::SceneJsonObjects(data)[0])[0].at("fields").at("開ける相手");
    }

    const nlohmann::json* FindSwitchField(const nlohmann::json& objects)
    {
        for (const nlohmann::json& object : objects)
        {
            for (const nlohmann::json& entry : object.at("components"))
            {
                if (NS::Obj::ComponentEntryType(entry) == "GateSwitch")
                    return &entry.at("fields").at("開ける相手");
            }
        }
        return nullptr;
    }
} // namespace

// ファイルには持ち主と Component の名前で書き、読むと id へ戻る
TEST(ComponentRefFile, WrittenByNamesAndReadBackAsIds)
{
    std::uint32_t gateId = 0;
    std::uint32_t doorId = 0;
    const nlohmann::json source = MakeSwitchAndGate(gateId, doorId);

    const std::string text = NS::Obj::SerializeSceneToJson(source);
    const nlohmann::json root = nlohmann::json::parse(text);
    const nlohmann::json* written = FindSwitchField(root.at("objects"));
    ASSERT_NE(written, nullptr);
    EXPECT_EQ(written->at("ref").get<std::string>(), "Gate");
    EXPECT_EQ(written->at("component").get<std::string>(), "Door");

    nlohmann::json loaded = NS::Obj::MakeSceneJson();
    ASSERT_TRUE(NS::Obj::DeserializeSceneFromJson(loaded, text));
    const nlohmann::json& read = SwitchRefValue(loaded);
    EXPECT_EQ(read.at("ref").get<std::uint32_t>(), gateId);
    EXPECT_EQ(read.at("component").get<std::uint32_t>(), doorId);
}

// 居ない Component の名前は未設定へ戻す。持ち主だけ残すと、何も指さない参照が生きて見える
TEST(ComponentRefFile, UnknownComponentNameBecomesUnset)
{
    std::uint32_t gateId = 0;
    std::uint32_t doorId = 0;
    const nlohmann::json source = MakeSwitchAndGate(gateId, doorId);

    nlohmann::json root = nlohmann::json::parse(NS::Obj::SerializeSceneToJson(source));
    for (nlohmann::json& object : root.at("objects"))
    {
        for (nlohmann::json& entry : object.at("components"))
        {
            if (NS::Obj::ComponentEntryType(entry) == "GateSwitch")
                entry["fields"]["開ける相手"]["component"] = "Missing";
        }
    }

    nlohmann::json loaded = NS::Obj::MakeSceneJson();
    ASSERT_TRUE(NS::Obj::DeserializeSceneFromJson(loaded, root.dump()));
    const nlohmann::json& read = SwitchRefValue(loaded);
    EXPECT_EQ(read.at("ref").get<std::uint32_t>(), 0u);
    EXPECT_EQ(read.at("component").get<std::uint32_t>(), 0u);
}

// ---- 宙に浮いた参照と複製 ----

// 持ち主の中に居ない Component を指す参照は、持ち主ごと未設定へ戻す
TEST(ComponentRefData, PruneResetsReferenceToMissingComponent)
{
    std::uint32_t gateId = 0;
    std::uint32_t doorId = 0;
    nlohmann::json data = MakeSwitchAndGate(gateId, doorId);
    NS::Obj::ObjectJsonComponents(NS::Obj::SceneJsonObjects(data)[0])[0]["fields"]["開ける相手"] = RefValue(gateId, doorId + 1000u);

    EXPECT_EQ(NS::Obj::PruneDanglingObjectRefs(data), 1u);
    const nlohmann::json& value = SwitchRefValue(data);
    EXPECT_EQ(value.at("ref").get<std::uint32_t>(), 0u);
    EXPECT_EQ(value.at("component").get<std::uint32_t>(), 0u);
}

TEST(ComponentRefData, PruneKeepsValidAndUnsetReferences)
{
    std::uint32_t gateId = 0;
    std::uint32_t doorId = 0;
    nlohmann::json data = MakeSwitchAndGate(gateId, doorId);
    EXPECT_EQ(NS::Obj::PruneDanglingObjectRefs(data), 0u);

    NS::Obj::ObjectJsonComponents(NS::Obj::SceneJsonObjects(data)[0])[0]["fields"]["開ける相手"] = RefValue(0u, 0u);
    EXPECT_EQ(NS::Obj::PruneDanglingObjectRefs(data), 0u);
}

// 複製の付け替えは表に載った id だけを変え、範囲の外を指す参照は元の相手のまま残す
TEST(ComponentRefData, RemapChangesOnlyMappedIds)
{
    nlohmann::json object = NS::Obj::MakeObjectJson();
    nlohmann::json entry = NS::Obj::MakeComponentEntry("GateSwitch");
    entry["fields"]["開ける相手"] = RefValue(10u, 11u);
    entry["fields"]["外の相手"] = RefValue(20u, 21u);
    NS::Obj::ObjectJsonComponents(object).push_back(std::move(entry));

    const std::unordered_map<std::uint32_t, std::uint32_t> idMap{{10u, 110u}, {11u, 111u}};
    NS::Obj::RemapObjectRefs(object, idMap);

    const nlohmann::json& fields = NS::Obj::ObjectJsonComponents(object)[0].at("fields");
    EXPECT_EQ(fields.at("開ける相手").at("ref").get<std::uint32_t>(), 110u);
    EXPECT_EQ(fields.at("開ける相手").at("component").get<std::uint32_t>(), 111u);
    EXPECT_EQ(fields.at("外の相手").at("ref").get<std::uint32_t>(), 20u);
    EXPECT_EQ(fields.at("外の相手").at("component").get<std::uint32_t>(), 21u);
}
