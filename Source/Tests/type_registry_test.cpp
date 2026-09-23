#include <Runtime/Object/Component.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Reflection/TypeRegistry.h>
#include <algorithm>
#include <cstddef>
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using NS::Obj::Component;
    using NS::Obj::CreateComponent;
    using NS::Obj::GameObject;
    using NS::Obj::IsRegistered;
    using NS::Obj::ReflectionInfo;
    using NS::Obj::RegisteredNames;

    // 登録型を 1 つ生成し、attach 先 obj の Components() が 1 増えて戻り値がその列に入るのを確かめる
    // 並びは priority が決めるので、末尾に来るとは限らない
    Component* CreateAndExpectAttached(std::string_view typeName, GameObject& obj)
    {
        const std::size_t before = obj.Components().size();
        Component* comp = CreateComponent(typeName, obj);
        EXPECT_NE(comp, nullptr) << typeName;
        if (comp != nullptr)
        {
            const std::vector<Component*>& list = obj.Components();
            EXPECT_EQ(list.size(), before + 1) << typeName;
            EXPECT_NE(std::find(list.begin(), list.end(), comp), list.end()) << typeName;
        }
        return comp;
    }

    [[nodiscard]] bool Contains(const std::vector<std::string>& names, const char* target)
    {
        for (const std::string& name : names)
        {
            if (name == target)
                return true;
        }
        return false;
    }
} // namespace

// 登録の抜けを見張る一覧。自己登録の翻訳単位がリンカに落とされたり登録マクロが消えたりすると、
// その型の生成が失敗して露見する
TEST(TypeRegistryTest, CreatesEachRegisteredType)
{
    const char* k_Registered[] = {
        "BoxCollider",
        "Breakable",
        "SphereCollider",
        "CapsuleCollider",
        "SlopeCollider",
        "MeshCollider",
        "CollisionInput",
        "ImpactResolver",
        "Hazard",
        "Health",
        "ImpactMark",
        "KillZone",
        "LaunchedBody",
        "MeshRenderer",
        "Goal",
        "CameraComponent",
        "PlacedVirtualCamera",
        "CameraBrain",
        "ThirdPersonFollow",
        "PlayerComponent",
        "PlayerInput",
        "PlayerStateManager",
        "PlayerAnimator",
        "Shadow",
        "SkeletalAnimation",
        "DirectionalLight",
    };
    for (const char* name : k_Registered)
    {
        GameObject obj;
        CreateAndExpectAttached(name, obj);
    }
}

TEST(TypeRegistryTest, CreatedTypeNameMatchesReflection)
{
    // 登録済み全型でリフレクション typeName が登録キーと一致する。JSON の type キーと整合する
    // 登録が増えても手直し不要なよう、一覧は TypeRegistry 自身から取る
    for (const std::string& name : RegisteredNames())
    {
        GameObject obj;
        Component* comp = CreateComponent(name, obj);
        ASSERT_NE(comp, nullptr) << name;
        const ReflectionInfo* info = comp->GetReflection();
        ASSERT_NE(info, nullptr) << name;
        EXPECT_STREQ(info->typeName, name.c_str());
    }
}

// TransformComponent は GameObject が必ず 1 つ持つので登録しない
TEST(TypeRegistryTest, ExcludedTypesReturnNull)
{
    // 抽象基底と必ず持つ基盤は登録しないので、信頼できない type 名から生成できない
    GameObject obj;
    const std::size_t before = obj.Components().size();
    EXPECT_EQ(CreateComponent("VirtualCamera", obj), nullptr);
    EXPECT_EQ(CreateComponent("Collider", obj), nullptr);
    EXPECT_EQ(CreateComponent("TransformComponent", obj), nullptr);
    EXPECT_EQ(CreateComponent("EntityComponent", obj), nullptr);
    EXPECT_EQ(CreateComponent("EntityStateManager", obj), nullptr);
    EXPECT_EQ(obj.Components().size(), before);
}

TEST(TypeRegistryTest, UnknownTypeReturnsNull)
{
    GameObject obj;
    const std::size_t before = obj.Components().size();
    EXPECT_EQ(CreateComponent("Bogus", obj), nullptr);
    EXPECT_EQ(CreateComponent("", obj), nullptr);
    EXPECT_EQ(obj.Components().size(), before);
}

TEST(TypeRegistryTest, GameObjectClassIsNotAComponent)
{
    // GameObject 側の登録名から Component は作れない。1 表でも両者の生成経路は交わらない
    GameObject obj;
    EXPECT_EQ(CreateComponent("Player", obj), nullptr);
    EXPECT_FALSE(IsRegistered("Player"));
}

TEST(TypeRegistryTest, IsRegisteredMatchesRegistrationSet)
{
    EXPECT_TRUE(IsRegistered("BoxCollider"));
    EXPECT_TRUE(IsRegistered("PlayerComponent"));
    EXPECT_FALSE(IsRegistered("Bogus"));
}

TEST(TypeRegistryTest, RegisteredNamesListsAllRuntimeTypes)
{
    const std::vector<std::string>& names = RegisteredNames();
    EXPECT_EQ(names.size(), 26u);
    EXPECT_TRUE(Contains(names, "BoxCollider"));
    EXPECT_TRUE(Contains(names, "MeshRenderer"));
    EXPECT_TRUE(Contains(names, "PlayerComponent"));
    // パレット表示が実行ごとに揺れないよう名前順に揃えてある
    EXPECT_TRUE(std::is_sorted(names.begin(), names.end()));
}

TEST(TypeRegistryTest, ReflectedFieldsMatchLedger)
{
    // リフレクションフィールドの期待一覧
    // リフレクションに載ったフィールドだけが Inspector 編集とシリアライズの対象になる
    // 増減が意図か事故かをこの一覧との突き合わせで判定する。抜けは気づけない保存漏れになる
    const std::map<std::string, std::vector<std::string>> k_Ledger = {
        {"BoxCollider", {"半径", "中心オフセット", "回転 (度)", "トリガー"}},
        {"Breakable", {"質量", "耐久"}},
        {"CameraBrain", {"ブレンド秒数"}},
        {"CameraComponent", {}},
        {"CapsuleCollider", {"半径", "半分の高さ", "中心オフセット", "回転 (度)"}},
        {"CollisionInput",
         {"チャージしきい値秒",
          "チャージ満タン秒",
          "チャージ減速率",
          "チャージ倍率カーブ",
          "突進位置係数カーブ",
          "中心近くの当たりのしきい値",
          "構えの縮み",
          "押しの構えの縮み"}},
        {"DirectionalLight", {"方向", "色", "環境光", "地面環境光", "露出"}},
        {"Hazard", {}},
        {"Health", {"体力"}},
        {"ImpactMark", {"跡の直径", "跡の残る秒"}},
        {"ImpactResolver",
         {"反発基準初速",
          "反発の上向き初速",
          "押し飛ばし基準初速",
          "押し飛ばしの質量指数",
          "押し飛ばしの浮き上がり",
          "押し飛ばしの最高速",
          "ヒットストップ基準秒",
          "中心近くの当たりのヒットストップ倍率",
          "ヒットストップの上限秒",
          "食い込み距離",
          "振動の振幅",
          "カメラ揺れの強さ",
          "潰れの厚み",
          "潰れの伸び上がり",
          "弾け伸びの倍率",
          "弾け伸びを戻すフレーム数",
          "中心近くの当たりの白の濃さ",
          "中心近くの当たりの白のフレーム数",
          "破壊を許可",
          "貫通時の減速倍率",
          "貫通の止め秒"}},
        {"KillZone", {}},
        {"LaunchedBody",
         {"跳ね返り",
          "摩擦",
          "回転の強さ",
          "止まってから消える秒",
          "破片の数",
          "破片の速さ",
          "破片の寿命秒",
          "破片の大きさ",
          "破片の色"}},
        {"MeshCollider", {}},
        {"MeshRenderer", {"基本色", "メッシュ", "マテリアル"}},
        {"Goal", {}},
        {"PlacedVirtualCamera", {"注視点", "上方向", "トリガー中心", "トリガー半径", "プレイヤー追視", "優先度"}},
        {"PlayerComponent",
         {"ジャンプ初速",
          "上昇重力",
          "下降重力",
          "頂点滞空 Vy",
          "頂点滞空倍率",
          "ジャンプ離し倍率",
          "コヨーテ時間",
          "先行入力時間",
          "歩き速度",
          "走行速度",
          "加速度",
          "空中の加速度",
          "曲がる時の抵抗",
          "手を放した時の減速度",
          "ブレーキの減速度",
          "ブレーキのしきい値",
          "スティック遊び",
          "登れる段の高さ",
          "掴める縁の下向き距離",
          "縁へ手を伸ばす距離",
          "よじ登りの所要時間",
          "縁の横移動速度",
          "振り向きの速さ",
          "突進速度",
          "突進距離",
          "タップ初速",
          "タップの上向き初速",
          "タップ距離",
          "狙いの巻き戻し秒",
          "狙いの巻き戻しが消える秒"}},
        {"PlayerInput", {}},
        {"PlayerStateManager", {}},
        {"PlayerAnimator",
         {"立ちのクリップ",
          "歩きのクリップ",
          "走りのクリップ",
          "跳ぶクリップ",
          "落ちるクリップ",
          "ぶら下がりのクリップ",
          "走りへ移る速さの比",
          "再生速度の下限"}},
        {"Shadow", {"基本直径", "最大投影距離", "表面オフセット", "基本不透明度"}},
        {"SkeletalAnimation", {"再生速度", "ループ再生", "モデル", "クリップ"}},
        {"SlopeCollider", {"角度 (度)", "半径"}},
        {"SphereCollider", {"半径", "中心オフセット"}},
        {"ThirdPersonFollow",
         {"追従対象",
          "初期ヨー",
          "初期ピッチ",
          "バネ角速度",
          "待機時距離",
          "走行時距離",
          "ジャンプ時距離",
          "走り判定速度",
          "頭の高さ",
          "感度 X",
          "感度 Y",
          "スティック感度 X",
          "スティック感度 Y",
          "反転 X",
          "反転 Y",
          "ピッチ下限",
          "ピッチ上限",
          "ファークリップ",
          "優先度"}},
    };

    const std::vector<std::string>& names = RegisteredNames();
    ASSERT_EQ(names.size(), k_Ledger.size());
    for (const std::string& name : names)
    {
        const auto entry = k_Ledger.find(name);
        ASSERT_NE(entry, k_Ledger.end()) << name << " が台帳に無い";

        GameObject obj;
        Component* comp = CreateComponent(name, obj);
        ASSERT_NE(comp, nullptr) << name;
        const ReflectionInfo* info = comp->GetReflection();
        ASSERT_NE(info, nullptr) << name;

        std::vector<std::string> actual;
        actual.reserve(info->fieldCount);
        for (std::size_t i = 0; i < info->fieldCount; ++i)
            actual.emplace_back(info->fields[i].name);
        EXPECT_EQ(actual, entry->second) << name;
    }
}

TEST(TypeRegistryTest, BaseChainMatchesLedger)
{
    // 基底鎖の期待一覧。空は Component 直下で鎖が終端することを表す
    // 誤った基底を書いた宣言は typeName 一致では捕まらないため、期待基底を明示して突き合わせる
    const std::map<std::string, std::vector<std::string>> k_BaseLedger = {
        {"BoxCollider", {"Collider"}},
        {"Breakable", {}},
        {"CameraBrain", {}},
        {"CameraComponent", {}},
        {"CapsuleCollider", {"Collider"}},
        {"CollisionInput", {}},
        {"DirectionalLight", {}},
        {"Hazard", {}},
        {"Health", {}},
        {"ImpactMark", {}},
        {"ImpactResolver", {"OverlayRenderer"}},
        {"KillZone", {}},
        {"LaunchedBody", {}},
        {"MeshCollider", {"Collider"}},
        {"MeshRenderer", {}},
        {"Goal", {}},
        {"PlacedVirtualCamera", {"VirtualCamera"}},
        {"PlayerComponent", {"EntityComponent"}},
        {"PlayerInput", {}},
        {"PlayerStateManager", {"EntityStateManager"}},
        {"PlayerAnimator", {}},
        {"Shadow", {}},
        {"SkeletalAnimation", {}},
        {"SlopeCollider", {"Collider"}},
        {"SphereCollider", {"Collider"}},
        {"ThirdPersonFollow", {"VirtualCamera"}},
    };

    const std::vector<std::string>& names = RegisteredNames();
    ASSERT_EQ(names.size(), k_BaseLedger.size());
    for (const std::string& name : names)
    {
        const auto entry = k_BaseLedger.find(name);
        ASSERT_NE(entry, k_BaseLedger.end()) << name << " が台帳に無い";

        GameObject obj;
        Component* comp = CreateComponent(name, obj);
        ASSERT_NE(comp, nullptr) << name;
        const ReflectionInfo* info = comp->GetReflection();
        ASSERT_NE(info, nullptr) << name;

        std::vector<std::string> actual;
        for (const ReflectionInfo* base = info->base; base != nullptr; base = base->base)
            actual.emplace_back(base->typeName);
        EXPECT_EQ(actual, entry->second) << name;
    }
}
