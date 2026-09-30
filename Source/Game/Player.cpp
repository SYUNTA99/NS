#include "Game/Player.h"

#include "Game/Level/CollisionInput.h"
#include "Game/Level/CourseDirector.h"
#include "Game/Level/Health.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/SlamArrow.h"
#include "Game/Level/TargetMarker.h"
#include "Game/Player/ChargeEffects.h"
#include "Game/Player/ImpactEffects.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/PlayerInputRelay.h"
#include "Game/Player/PlayerStateManager.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Object/Components/CapsuleCollider.h"
#include "Runtime/Object/Components/HitReaction.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Components/Shadow.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/IUseSceneObj.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <algorithm>
#include <cstring>

NS_CLASS(Player)

Player::Player() noexcept
{
    // 構成はここが全部決める。データは値だけを写す
    // 同居する部品の引き当ては OnStart なので生成順に縛りは無い
    NS::Obj::MeshRenderer* mesh = AddComponent<NS::Obj::MeshRenderer>();
    // mesh の参照は空のまま。立ち姿と玉の mesh は PlayerAppearance が差す
    mesh->SetMaterialRef("player");
    // 差し替えのモデルが来るまでの仮の色。色味を持たない中間の灰色で、床の 0.7 より暗く床の上で輪郭が残る
    mesh->SetBaseColor(NS::Core::Vector3{0.5f, 0.5f, 0.5f});
    AddComponent<NS::Game::Player::PlayerAppearance>();
    // 2 つで 1 組。状態機械が欠けると遷移が 1 つも起きない
    AddComponent<NS::Game::Player::PlayerStateManager>();
    AddComponent<NS::Game::Player::PlayerComponent>();
    AddComponent<NS::Obj::PlayerInput>();
    // 入力は NS::Obj に居て自機の型を名指しできないので、値の受け渡しを挟む
    AddComponent<NS::Game::Player::PlayerInputRelay>();
    // 命は player 自身の持ち物。ルール配置物がこれを削る
    AddComponent<NS::Game::Level::Health>();
    // 接地シャドウ。mesh / material は後から注入される
    AddComponent<NS::Obj::Shadow>();
    // 移動の当たりの形
    AddComponent<NS::Obj::CapsuleCollider>();
    // 体のセンサー。落下死とゴールの範囲に調べられる。寸法は移動の当たりに合わせて移動の部品が書く
    NS::Obj::HitSensor* body = AddComponent<NS::Obj::HitSensor>();
    body->SetType(NS::Obj::HitSensorType::PlayerBody);
    body->SetCapsule(0.4f, 0.5f);

    // 体当たり。入力を読んで発動を求め、ぶつかった結果を決め、狙いの印と突進の線と当たりの演出を出す
    AddComponent<NS::Game::Level::CollisionInput>();
    AddComponent<NS::Game::Level::ImpactResolver>();
    // 当てた時の白の光・カメラの揺れと寄り・パッドの振動。裁定が組んで渡す
    AddComponent<NS::Obj::HitReaction>();
    AddComponent<NS::Game::Level::TargetMarker>();
    AddComponent<NS::Game::Level::SlamArrow>();
    AddComponent<NS::Game::Player::ChargeEffects>();
    AddComponent<NS::Game::Player::ImpactEffects>();
}

NS::Obj::CameraTargetState Player::GetCameraTargetState() const
{
    NS::Obj::CameraTargetState state{};
    if (const NS::Game::Player::PlayerComponent* movement = FindComponent<NS::Game::Player::PlayerComponent>())
    {
        state.grounded = movement->IsGrounded();
        state.velocity = movement->Velocity();
        // 当たりの足元に立ち姿のカプセルを立てた時の中心を見る。玉の間は根が立ち姿の半長ぶん下がっているので、
        // 根を見ると押すたびに画面が 1 フレームで半長ぶん沈み、解けると跳ね上がる
        state.heightOffset = movement->StandingHalfHeight() - movement->CapsuleHalfHeight();
        state.hasRebound = true;
        state.rebound = NS::Obj::FollowReboundDesc{
            .rebounding = movement->IsRebounding(),
            .slamDirection = movement->BodySlamStartDirection(),
        };
    }
    if (const NS::Game::Level::CollisionInput* input = FindComponent<NS::Game::Level::CollisionInput>())
    {
        // 溜め量は放した後も放した時の値を返し続けるので、押していないフレームは 0 を渡す
        const NS::Game::Level::ImpactInputJudge& judge = input->Judge();
        state.hasCharge = true;
        state.charge.held = judge.IsHeld();
        state.charge.charge01 = state.charge.held ? judge.Charge01() : 0.0f;
        NS::Game::Level::SlamLineTarget aim{};
        state.charge.hasAimTarget = input->TryGetAimTarget(aim);
        if (state.charge.hasAimTarget)
        {
            state.charge.aimTargetCenter = NS::Core::Vector3{aim.bounds.Center.x, aim.bounds.Center.y, aim.bounds.Center.z};
            state.charge.aimTargetRadius = std::max({aim.bounds.Extents.x, aim.bounds.Extents.y, aim.bounds.Extents.z});
        }
    }
    return state;
}

void Player::InitAfterPlacement()
{
    // 死とゴールを伝える先。先に作っておくと、最初の知らせの段で流れが進む
    (void)NS::Obj::GetOrCreateSceneObj<NS::Game::Level::CourseDirector>(*this);
}

bool Player::ReceiveMsg(const NS::Obj::Message& msg, NS::Obj::HitSensor* sender, NS::Obj::HitSensor* receiver)
{
    (void)sender;
    (void)receiver;
    if (NS::Game::Level::IsMsgKill(msg))
    {
        Kill();
        if (NS::Game::Level::CourseDirector* director =
                NS::Obj::GetOrCreateSceneObj<NS::Game::Level::CourseDirector>(*this))
        {
            director->NotifyPlayerDead();
        }
        return true;
    }
    if (NS::Game::Level::IsMsgGoal(msg))
    {
        if (NS::Game::Level::CourseDirector* director =
                NS::Obj::GetOrCreateSceneObj<NS::Game::Level::CourseDirector>(*this))
        {
            director->NotifyGoal();
        }
        return true;
    }
    if (const NS::Game::Level::MsgCourseRestart* restart = NS::Obj::MsgCast<NS::Game::Level::MsgCourseRestart>(msg))
    {
        RestartFrom(restart->Baseline());
        return true;
    }
    if (const NS::Game::Level::MsgInputLock* lock = NS::Obj::MsgCast<NS::Game::Level::MsgInputLock>(msg))
    {
        if (NS::Obj::PlayerInput* input = FindComponent<NS::Obj::PlayerInput>())
            input->SetActive(!lock->Locked());
        return true;
    }
    return false;
}

void Player::RestartFrom(const nlohmann::json& baseline) noexcept
{
    // 出現位置はエディタで配置したプレイヤーの capsule 中心の world 位置そのもの
    // 凍結に既にある値なので写しは持たず、その都度読む。居なければ新規レベルで置く位置へ戻す
    NS::Core::Vector3 spawn{0.0f, 1.41f, 0.0f};
    const std::size_t index = NS::Obj::FindObjectIndexById(baseline, Id());
    if (index != NS::Obj::k_NoObjectIndex)
        spawn = NS::Obj::ObjectPosition(NS::Obj::SceneJsonObjects(baseline)[index]);

    Root().SetPosition(spawn);
    if (NS::Game::Player::PlayerComponent* movement = FindComponent<NS::Game::Player::PlayerComponent>())
        movement->ResetState();
    ResetHealth();
}

void Player::ApplyDamage(int amount) noexcept
{
    if (NS::Game::Level::Health* health = FindComponent<NS::Game::Level::Health>())
        health->ApplyDamage(amount);
}

void Player::Kill() noexcept
{
    if (NS::Game::Level::Health* health = FindComponent<NS::Game::Level::Health>())
        health->Kill();
}

void Player::ResetHealth() noexcept
{
    if (NS::Game::Level::Health* health = FindComponent<NS::Game::Level::Health>())
        health->Reset();
}

bool Player::IsDead() const noexcept
{
    if (const NS::Game::Level::Health* health = FindComponent<NS::Game::Level::Health>())
        return health->IsDead();
    return false;
}

int Player::Health() const noexcept
{
    if (const NS::Game::Level::Health* health = FindComponent<NS::Game::Level::Health>())
        return health->Current();
    return 0;
}

Player* FindPlayer(NS::Obj::ObjectList& objects) noexcept
{
    for (NS::Obj::Actor* obj : objects)
    {
        if (std::strcmp(obj->ClassName(), "Player") == 0)
            return static_cast<Player*>(obj);
    }
    return nullptr;
}

bool IsPlayerObject(const nlohmann::json& object) noexcept
{
    return NS::Obj::ObjectJsonClass(object) == "Player";
}

std::size_t FindPlayerObjectIndex(const nlohmann::json& scene) noexcept
{
    const nlohmann::json& objects = NS::Obj::SceneJsonObjects(scene);
    for (std::size_t i = 0; i < objects.size(); ++i)
    {
        if (IsPlayerObject(objects[i]))
            return i;
    }
    return NS::Obj::k_NoObjectIndex;
}

nlohmann::json MakePlayerObject(const NS::Core::Vector3& position, const NS::Core::Quaternion& rotation)
{
    // 構成は Player のコンストラクタが決める。ひな形は型名だけ持ち、値はコード既定を使う
    nlohmann::json object = NS::Obj::MakePrototypeJson<Player>();
    NS::Obj::SetObjectPosition(object, position);
    NS::Obj::SetObjectRotation(object, rotation);
    // 根のスケールは既定の 1 のまま。1 でないと玉が楕円に伸び、差し替えたモデルも同じ比で伸びる
    return object;
}

std::uint32_t PlayerObjectId(const nlohmann::json& scene) noexcept
{
    const std::size_t index = FindPlayerObjectIndex(scene);
    if (index == NS::Obj::k_NoObjectIndex)
        return NS::Obj::k_NoObjectId;
    return NS::Obj::ObjectJsonId(NS::Obj::SceneJsonObjects(scene)[index]);
}

bool EnsurePlayerObject(nlohmann::json& scene)
{
    bool created = false;
    if (FindPlayerObjectIndex(scene) == NS::Obj::k_NoObjectIndex)
    {
        // capsule 中心の高さは、床 block 上面 0.5 + capsule 半高 0.9 + 1cm
        NS::Obj::SceneJsonObjects(scene).push_back(
            MakePlayerObject(NS::Core::Vector3{0.0f, 1.41f, 0.0f}, NS::Core::Quaternion{}));
        created = true;
    }

    std::size_t count = 0;
    for (const nlohmann::json& object : NS::Obj::SceneJsonObjects(scene))
    {
        if (IsPlayerObject(object))
            ++count;
    }
    if (count > 1)
        NS_LOG_WARN(Game, "プレイヤーが {} 体ある。先頭の 1 体を正とし、残りは無効として扱う", count);

    // 追従カメラが Target へ書き込む id が要るので、ここで採番まで済ませる
    NS::Obj::EnsureUniqueObjectIds(scene);
    return created;
}
