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
        "BoxCollider",        "Breakable",      "SphereCollider",    "CapsuleCollider",  "SlopeCollider",
        "MeshCollider",       "CollisionInput", "ImpactResolver",    "FollowCameraFeed", "Health",
        "ImpactMark",         "KillZone",       "LaunchedBody",      "MeshRenderer",     "Goal",
        "CameraComponent",    "CameraBrain",    "ThirdPersonFollow", "PlayerComponent",  "PlayerInput",
        "PlayerStateManager", "PlayerAnimator", "PlayerAppearance",  "Shadow",           "SkeletalAnimation",
        "DirectionalLight",   "RigidBody",      "PhysicsSettings",   "TargetMarker",     "ChargeEffects",
        "ImpactEffects",      "HitZones",
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
    EXPECT_EQ(names.size(), 33u);
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
        {"BoxCollider", {"半径", "中心オフセット", "回転 (度)", "トリガー", "物理に入れない"}},
        {"Breakable", {"耐久"}},
        {"CameraBrain", {"ブレンド秒数"}},
        {"CameraComponent", {}},
        {"CapsuleCollider", {"半径", "半分の高さ", "中心オフセット", "回転 (度)", "トリガー", "物理に入れない"}},
        {"ChargeEffects", {"タップの弾けの大きさ", "溜めきりで足す弾けの大きさ"}},
        {"CollisionInput",
         {"チャージしきい値秒",
          "チャージ満タン秒",
          "チャージ減速率",
          "チャージ倍率カーブ",
          "突進位置係数カーブ",
          "中心近くの境目",
          "惜しいの境目",
          "寄せる相手を探す角度",
          "寄せる相手を探す距離",
          "構えの縮み",
          "押しの構えの縮み"}},
        {"DirectionalLight", {"方向", "色", "環境光", "地面環境光", "露出"}},
        {"FollowCameraFeed", {}},
        {"Health", {"体力"}},
        {"HitZones", {"段の数", "真ん中の範囲", "惜しいの範囲"}},
        {"ImpactEffects",
         {"核の直径の基準",
          "核の直径の威力あたり",
          "核の直径の上限",
          "核の出始めの大きさ",
          "光条の長さの基準",
          "光条の長さの威力あたり",
          "光条の細りきった太さ",
          "輪の半径の基準",
          "輪の半径の威力あたり",
          "輪の出始めの半径",
          "惜しいの輪が届く割合",
          "輪をカメラへ起こす割合",
          "火花の数の下限",
          "火花の数の上限",
          "火花の速さの基準",
          "火花の速さの飛ばしの比あたり",
          "惜しいの火花の数の割合",
          "大きな外れの火花の数",
          "大きな外れの火花の速さ",
          "火の粉の数の火花あたり",
          "照りの直径の基準",
          "照りの直径の威力あたり",
          "弾かれ線の本数",
          "大きな外れの弾かれ線の本数",
          "弾かれ線の長さの基準",
          "弾かれ線の長さの反動の比あたり",
          "当たりの粉の数の基準",
          "当たりの粉の数を増やす質量の上限",
          "当たりの粉の大きさの基準",
          "当たりの粉の大きさの質量の平方根あたり",
          "当たりの粉の大きさの威力あたりの伸び",
          "飛び出しの尾が残るフレーム数の基準",
          "飛び出しの尾が残るフレーム数の飛ばしの比あたり",
          "着地の粉の半径の基準",
          "着地の粉の半径の落ちる速さあたり",
          "飛ばした物の着地の粉の大きさの基準",
          "飛ばした物の着地の粉の大きさの質量の平方根あたり",
          "飛ばした物の着地の粉の大きさの威力あたりの伸び"}},
        {"ImpactMark", {"跡の直径", "跡の残る秒"}},
        {"ImpactResolver",
         {"反動の高さ",
          "反動の距離",
          "中心近くの当たりの反動の距離の倍率",
          "押し飛ばしの距離",
          "押し飛ばしの質量指数",
          "押し飛ばしの高さ",
          "下りの速さの倍率",
          "頂点の帯の縦速度",
          "頂点の帯の重力倍率",
          "ヒットストップ基準秒",
          "中心近くの当たりのヒットストップ倍率",
          "ヒットストップの上限秒",
          "食い込み距離",
          "振動の振幅",
          "カメラ揺れの強さ",
          "中心近くの当たりの揺れの倍率",
          "大きな外れの揺れのフレーム数",
          "大きな外れの揺れの縦と横の比",
          "大きな外れの揺れの入れ替わりの最長フレーム数",
          "中心近くの当たりの寄りの倍率",
          "中心近くの当たりの傾き",
          "寄りと傾きを戻すフレーム数",
          "惜しい当たりの返りの割合",
          "惜しい当たりの返りを引き始める割合",
          "中心近くの当たりのパッドの振動の強さ",
          "大きな外れのパッドの振動の強さ",
          "潰れの厚み",
          "潰れの伸び上がり",
          "弾け伸びの倍率",
          "弾け伸びの行き過ぎ",
          "弾け伸びを戻すフレーム数",
          "中心近くの当たりの白の濃さ",
          "中心近くの当たりの白のフレーム数",
          "破壊を許可",
          "貫通時の減速倍率",
          "貫通の止め秒",
          "跡の床探しの距離"}},
        {"KillZone", {}},
        {"PhysicsSettings", {"重力"}},
        {"RigidBody",
         {"キネマティック",
          "質量",
          "重力を使う",
          "重力の倍率",
          "摩擦",
          "跳ね返り",
          "移動の減衰",
          "回転の減衰",
          "連続衝突判定",
          "X 移動を固定",
          "Y 移動を固定",
          "Z 移動を固定",
          "X 回転を固定",
          "Y 回転を固定",
          "Z 回転を固定"}},
        {"LaunchedBody",
         {"回転の強さ", "止まってから消える秒", "破片の数", "破片の速さ", "破片の寿命秒", "破片の大きさ", "破片の色"}},
        {"MeshCollider", {"トリガー", "物理に入れない"}},
        {"MeshRenderer", {"基本色", "メッシュ", "マテリアル"}},
        {"Goal", {"半径"}},
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
          "狙いの巻き戻しが消える秒",
          "寄せる角度の上限",
          "1 フレームの向きの変化の上限",
          "反動の上りの重力倍率",
          "反動中の空中の加速度"}},
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
        {"PlayerAppearance",
         {"立ち姿のメッシュ",
          "玉のメッシュ",
          "溜め 0 の回る速さ",
          "溜めきりの回る速さ",
          "突進中の回る速さ",
          "着地の潰れ",
          "着地の潰れを戻すフレーム数"}},
        {"Shadow", {"基本直径", "最大投影距離", "表面オフセット", "基本不透明度"}},
        {"SkeletalAnimation", {"再生速度", "ループ再生", "モデル", "クリップ"}},
        {"SlamArrow",
         {"矢印が伸びるフレーム数",
          "矢印を浮かせる高さ",
          "矢じりの幅",
          "矢じりの奥行きの割合",
          "矢じりの奥行きの下限",
          "矢じりの奥行きの上限",
          "帯の始まりのぼかし",
          "色の境目のぼかし",
          "後半の色へ変わる溜め量",
          "溜めの前半の色",
          "溜めの後半の色",
          "溜めきりの色",
          "色の付いていない部分の色",
          "矢印の暗い縁の色",
          "矢印の暗い縁の不透明度",
          "帯の明るい縁の不透明度",
          "帯の塗りの不透明度",
          "矢じりの明るい縁の不透明度",
          "矢じりの塗りの不透明度",
          "色の無い帯の明るい縁の不透明度",
          "色の無い帯の塗りの不透明度",
          "色の無い矢じりの明るい縁の不透明度",
          "色の無い矢じりの塗りの不透明度"}},
        {"SlopeCollider", {"角度 (度)", "半径", "トリガー", "物理に入れない"}},
        {"SphereCollider", {"半径", "中心オフセット", "トリガー", "物理に入れない"}},
        {"TargetMarker",
         {"印の色",
          "印の太さ",
          "印の腕の割合",
          "枠と輪郭の間",
          "枠の一辺の下限",
          "枠の不透明度",
          "枠が出る時の倍率",
          "枠が出る時の一辺の上限",
          "枠が縮むフレーム数",
          "枠が縮む進みの曲線",
          "枠が出る時の色",
          "枠が出る時の不透明度",
          "外れた時の枠の倍率",
          "外れた時の枠のフレーム数",
          "枠の縁の色",
          "枠の縁の不透明度"}},
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
          "溜めで締める視野角",
          "締めを戻すフレーム数",
          "溜めの揺れの強さ",
          "溜めの構図の枠",
          "溜めの構図のバネ角速度",
          "反動の間の横と前後のバネ角速度",
          "反動の間の横と前後の遅れの上限",
          "反動の間の上下の帯",
          "反動の後に戻すフレーム数",
          "反動の向きへ回すフレーム数",
          "反動の間に下げる距離",
          "ファークリップ",
          "優先度"}},
    };

    const std::vector<std::string>& names = RegisteredNames();
    ASSERT_EQ(names.size(), k_Ledger.size());
    for (const std::string& name : names)
    {
        const std::map<std::string, std::vector<std::string>>::const_iterator entry = k_Ledger.find(name);
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
        {"ChargeEffects", {}},
        {"CollisionInput", {}},
        {"DirectionalLight", {}},
        {"FollowCameraFeed", {}},
        {"Health", {}},
        {"HitZones", {}},
        {"ImpactEffects", {}},
        {"ImpactMark", {}},
        {"ImpactResolver", {"OverlayRenderer"}},
        {"KillZone", {}},
        {"LaunchedBody", {}},
        {"PhysicsSettings", {}},
        {"RigidBody", {}},
        {"MeshCollider", {"Collider"}},
        {"MeshRenderer", {}},
        {"Goal", {}},
        {"PlayerComponent", {"EntityComponent"}},
        {"PlayerInput", {}},
        {"PlayerStateManager", {"EntityStateManager"}},
        {"PlayerAnimator", {}},
        {"PlayerAppearance", {}},
        {"Shadow", {}},
        {"SkeletalAnimation", {}},
        {"SlamArrow", {}},
        {"SlopeCollider", {"Collider"}},
        {"SphereCollider", {"Collider"}},
        {"TargetMarker", {"OverlayRenderer"}},
        {"ThirdPersonFollow", {"VirtualCamera"}},
    };

    const std::vector<std::string>& names = RegisteredNames();
    ASSERT_EQ(names.size(), k_BaseLedger.size());
    for (const std::string& name : names)
    {
        const std::map<std::string, std::vector<std::string>>::const_iterator entry = k_BaseLedger.find(name);
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
