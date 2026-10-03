#include "Game/Player.h"

#include "Game/Level/LaunchArc.h"
#include "Game/Player/PlayerJudges.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/States/BodySlamPlayerState.h"
#include "Game/Player/States/BrakePlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/LedgeClimbingPlayerState.h"
#include "Game/Player/States/LedgeHangingPlayerState.h"
#include "Game/Player/States/ReboundPlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/Collider.h"
#include "Runtime/Object/Components/PlayerInput.h"

#include <cmath>

// ---- 突進と反発 ----

void Player::RequestBodySlam(float charge01) noexcept
{
    // そのフレームで出せないと押しが無言で消える。ジャンプと同じ先行入力時間だけ覚える
    m_request.bufferRemaining = m_params->m_jumpBufferTime;
    // 非数は 0..1 への丸めを素通りして溜め量に残るため、入口で 0 へ倒す
    if (!std::isfinite(charge01))
    {
        m_request.charge01 = 0.0f;
    }
    else
    {
        m_request.charge01 = NS::Core::Clamp(charge01, 0.0f, 1.0f);
    }
    // 残すと、先行入力のうちに来たタップが前の溜めた突進の向きへ出る
    m_request.hasDir = false;
    m_request.verticalSpeed = 0.0f;
}

void Player::RequestBodySlam(float charge01, const NS::Core::Vector3& aimDirection, float launchVerticalSpeed) noexcept
{
    RequestBodySlam(charge01);
    NS::Core::Vector3 dir{};
    if (!NS::Core::TryNormalizeHorizontal(aimDirection, dir))
    {
        return;
    }
    // 非数と無限の向きは正規化を通り抜ける
    if (!std::isfinite(dir.x) || !std::isfinite(dir.z))
    {
        return;
    }
    m_request.dir = dir;
    m_request.hasDir = true;
    if (std::isfinite(launchVerticalSpeed))
    {
        m_request.verticalSpeed = launchVerticalSpeed;
    }
}

float Player::BodySlamSpeed() const noexcept
{
    if (m_slam.isTap)
    {
        return m_params->m_tapSlamSpeed;
    }
    return m_params->m_bodySlamSpeed;
}

NS::Core::Vector3 Player::BodySlamVelocity() const noexcept
{
    const NS::Obj::Body& body = *m_body;
    if (!IsBodySlamming())
    {
        return body.Velocity();
    }

    const float speed = BodySlamSpeed();
    return NS::Core::Vector3{m_slam.dir.x * speed, body.VerticalVelocity(), m_slam.dir.z * speed};
}

void Player::CancelBodySlam() noexcept
{
    if (!IsBodySlamming())
    {
        return;
    }
    EndBodySlam();
}

void Player::EndBodySlam() noexcept
{
    NS::Obj::Body& body = *m_body;
    m_slam.travelled = 0.0f;
    m_slam.distanceTarget = 0.0f;

    // 加速は最高速を超えた速さを削らない。切らないと、倒している間は突進の速さのまま走り続ける
    const NS::Core::Vector3 lateral = body.LateralVelocity();
    const float speed = std::sqrt(lateral.x * lateral.x + lateral.z * lateral.z);
    const float cap = MaxSpeed();
    if (speed > cap)
    {
        const float scale = cap / speed;
        body.SetLateralVelocity(NS::Core::Vector3{lateral.x * scale, 0.0f, lateral.z * scale});
    }

    if (body.IsGrounded())
    {
        (void)m_states->Change<NS::Game::Player::WalkPlayerState>();
    }
    else
    {
        (void)m_states->Change<NS::Game::Player::FallPlayerState>();
    }
    m_playerEvents.onBodySlamEnded.Invoke();
}

void Player::AdvanceBodySlamTravel(const NS::Core::Vector3& delta) noexcept
{
    if (!IsBodySlamming())
    {
        return;
    }

    // 進んだ距離は実際に動いた量から測る。突進の速さから積むと壁で止められたフレームも進んだ扱いになる
    const float stepDistance = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    m_slam.travelled += stepDistance;

    // 進めないフレームで打ち切る。壁で止められると進んだ距離が伸びず、突進から出られなくなる
    // 発動したフレームは見ない。ここで打ち切ると発動から打ち切りまでに ImpactResolver が
    // 一度も走らず、突進を見ないまま終わる
    const bool stalled = !m_slam.justStarted && stepDistance < NS::Core::k_Epsilon;
    m_slam.justStarted = false;

    if (m_slam.travelled >= m_slam.distanceTarget || stalled)
    {
        EndBodySlam();
    }
}

NS::Core::Vector3 Player::AimDirection() const noexcept
{
    const NS::Obj::Body& body = *m_body;
    NS::Core::Vector3 dir{DesiredDirection().x, 0.0f, DesiredDirection().z};
    float length = std::sqrt(dir.x * dir.x + dir.z * dir.z);

    // 反発後の滑りなど残った速度が向きに勝つと狙いと食い違う方へ飛ぶ。入力が無ければ速度よりカメラの前を先に見る
    if (length < NS::Core::k_Epsilon && GetCameraManager() != nullptr)
    {
        const NS::Core::Vector3 forward = NS::Obj::CameraForwardHorizontal(*this);
        dir = NS::Core::Vector3{forward.x, 0.0f, forward.z};
        length = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    }
    if (length < NS::Core::k_Epsilon)
    {
        const NS::Core::Vector3 lateral = body.LateralVelocity();
        dir = NS::Core::Vector3{lateral.x, 0.0f, lateral.z};
        length = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    }
    if (length < NS::Core::k_Epsilon)
    {
        return NS::Core::Vector3{0.0f, 0.0f, 0.0f};
    }

    return NS::Core::Vector3{dir.x / length, 0.0f, dir.z / length};
}

void Player::MarkBodySlamAim() noexcept
{
    m_request.aimDir = AimDirection();
    m_request.aimAge = 0.0f;
}

float Player::BodySlamAimBlend01() const noexcept
{
    const float hold = m_params->m_slamAimHoldTime;
    const float fade = m_params->m_slamAimFadeTime;
    const float age = m_request.aimAge;
    if (age <= hold)
    {
        return 1.0f;
    }
    // 巻き戻し秒を消える秒より後ろにできる。幅が 0 以下なら割らずに切る
    if (!(fade > hold) || age >= fade)
    {
        return 0.0f;
    }
    return (fade - age) / (fade - hold);
}

bool Player::BodySlam() noexcept
{
    NS::Obj::Body& body = *m_body;
    NS::Core::Vector3 dir = AimDirection();

    const float aimLength =
        std::sqrt(m_request.aimDir.x * m_request.aimDir.x + m_request.aimDir.z * m_request.aimDir.z);
    // 添えた向きは放す前に見せていた狙いなので、入力も押したフレームの控えも混ぜない
    if (m_request.hasDir)
    {
        dir = m_request.dir;
    }
    else if (aimLength >= NS::Core::k_Epsilon)
    {
        const float blend = BodySlamAimBlend01();
        if (blend > 0.0f)
        {
            NS::Core::Vector3 mixed{dir.x * (1.0f - blend) + m_request.aimDir.x * blend,
                                    0.0f,
                                    dir.z * (1.0f - blend) + m_request.aimDir.z * blend};
            const float mixedLength = std::sqrt(mixed.x * mixed.x + mixed.z * mixed.z);
            // 正反対の向きを同じくらいの重みで混ぜると長さが 0 近くになる。その時は濃い側をそのまま採る
            if (mixedLength >= NS::Core::k_Epsilon)
            {
                dir = NS::Core::Vector3{mixed.x / mixedLength, 0.0f, mixed.z / mixedLength};
            }
            else if (blend >= 0.5f)
            {
                dir = m_request.aimDir;
            }
        }
    }

    if (std::sqrt(dir.x * dir.x + dir.z * dir.z) < NS::Core::k_Epsilon)
    {
        return false;
    }

    m_slam.charge01 = m_request.charge01;
    m_slam.isTap = !(m_request.charge01 > 0.0f);
    m_slam.travelled = 0.0f;
    m_slam.justStarted = true;
    const float speed = BodySlamSpeed();

    if (m_slam.isTap)
    {
        m_slam.distanceTarget = m_params->m_tapSlamDistance;
        body.SetVelocity(NS::Core::Vector3{dir.x * speed, m_params->m_tapSlamUpSpeed, dir.z * speed});
    }
    else
    {
        m_slam.distanceTarget = m_params->m_bodySlamDistance;
        // 縦はジャンプのどこで放ったかでなく狙いで決める。同じ狙いならいつも矢印と同じ道筋で出て、ジャンプの上向きの
        // 勢いに乗って放つ角度の上限と別の道で高く上がることも無い。届く相手が無ければ 0 で水平に出て重力で落ちる
        float vertical = 0.0f;
        if (m_request.hasDir)
        {
            vertical = m_request.verticalSpeed;
        }
        body.SetVelocity(NS::Core::Vector3{dir.x * speed, vertical, dir.z * speed});
    }

    // 距離が 0 以下だと 1 フレーム目で終わって発動が消えるため、出さずに通常移動のままにする
    if (!(m_slam.distanceTarget > 0.0f))
    {
        return false;
    }

    // 出せた時だけ書く。反動の後のカメラと放した瞬間の絵が、突進の後も最後に出た突進の向きとして読む
    m_slam.dir = dir;
    m_request.hasDir = false;
    m_request.spent = true;
    // 突進はどの経路で出ても玉で走らせる。掴まり中に放した押しは予約に残り、先行入力の秒の内に
    // 縁を離れれば出るが、その時の丸まりは掴まりで解けている
    ChangeCurled(true);

    (void)m_states->Change<NS::Game::Player::BodySlamPlayerState>();
    m_playerEvents.onBodySlamStarted.Invoke();
    return true;
}

bool Player::BeginRebound(const NS::Game::Player::ReboundArc& arc) noexcept
{
    // 曲線にならない反動で移すと、弾かれないまま速度が 0 に消える。移さずに偽を返し、速度は呼び手に任せる
    const NS::Core::Vector3 velocity = ReboundVelocityFor(arc);
    if (!(velocity.y > 0.0f))
    {
        return false;
    }
    NS::Core::Vector3 direction{};
    if (!NS::Core::TryNormalizeHorizontal(arc.direction, direction))
    {
        return false;
    }

    m_rebound.direction = direction;
    m_body->SetVelocity(velocity);
    (void)m_states->Change<NS::Game::Player::ReboundPlayerState>();
    return true;
}

NS::Core::Vector3 Player::ReboundVelocityFor(const NS::Game::Player::ReboundArc& arc) const noexcept
{
    // 下りは普段の落ち方のままにする。組は実際に当てる重力 (ReboundGravity) と同じ PlayerParams::ReboundGravity
    // 曲線は上りの重力を正の大きさで、下りの重力を上りに対する倍率で持つので、符号を反転して下降重力を上りの重力で割る
    const NS::Game::Player::PlayerGravity gravity = m_params->ReboundGravity();
    const float riseGravity = -gravity.rise;
    const NS::Game::Level::LaunchArc launchArc{.direction = arc.direction,
                                               .distance = arc.distance,
                                               .apexHeight = arc.apexHeight,
                                               .riseGravity = riseGravity,
                                               .fallGravityScale = -gravity.fall / riseGravity,
                                               .apexBandSpeed = gravity.apexSpeed,
                                               .apexBandGravityScale = gravity.apexScale};
    return NS::Game::Level::LaunchArcInitialVelocity(launchArc);
}

void Player::SetCurled(bool curled) noexcept
{
    // 掴まりからは突進が出ない。玉のままぶら下がると、押しても突進が出ないのに玉の見た目だけが残る
    // 掴まっている間に玉にすると縁を測り直す手の高さが下がり、押したフレームに縁を放して 0.5 m 落ちた
    if (curled && (m_states->IsCurrent<NS::Game::Player::LedgeHangingPlayerState>() ||
                   m_states->IsCurrent<NS::Game::Player::LedgeClimbingPlayerState>()))
    {
        return;
    }
    ChangeCurled(curled);
}

void Player::ChangeCurled(bool curled) noexcept
{
    NS::Obj::Collider& collider = *m_collider;
    if (curled == m_curled)
    {
        return;
    }
    m_curled = curled;
    collider.SetSphereShape(curled);
    // 立ち姿の下端は 中心 − 半長 − 半径、玉の下端は 中心 − 半径。中心を立ち姿の半長ぶん上げ下げすると下端が揃う
    // 下げずに玉にすると、当たりの下端が半長ぶん上がる
    float rise = collider.StandingHalfHeight();
    if (curled)
    {
        rise = -rise;
    }
    // TODO: 低い天井の下で立ち姿へ戻す時の検査は無い。コースに低い天井が無いうちは、
    // 作り直したキャラクターの食い込みは次の Step の接触の解決に任せる
    // 形の持ち替えは動きではないので、前フレームの位置も一緒にずらす。今の位置だけを動かすと、持ち替えたフレームの
    // 描画の補間で玉が床から浮き (立ち姿は床へ沈み)、立ち姿の中心を見る追従カメラの注視点も半長ぶん揺れる
    Root().ShiftPosition(NS::Core::Vector3{0.0f, rise, 0.0f});
}

NS::Core::Sphere Player::SlamBallAt(const NS::Core::Vector3& rootPosition) const noexcept
{
    // 立ち姿の下の球が、丸まった後の玉の中心。ChangeCurled が下端を揃えて根を下げるので、この式が成り立つ
    const NS::Phys::Capsule capsule = m_collider->CapsuleAt(rootPosition);
    return NS::Core::Sphere{capsule.center - capsule.axis * capsule.halfHeight, capsule.radius};
}

void Player::SetBodySlamHeld(bool held) noexcept
{
    m_bodySlamHeld = held;
}

void Player::UncurlWhenSettled() noexcept
{
    NS::Obj::Body& body = *m_body;
    if (!m_curled)
    {
        return;
    }
    if (m_bodySlamHeld || IsBodySlamming() || !body.IsGrounded())
    {
        return;
    }
    // 反動が明けたフレームは接地の印が残ったまま上向きの速度が入る。速度を見ないと宙へ出る前に解ける
    if (body.VerticalVelocity() > 0.0f)
    {
        return;
    }
    // 当てたフレームは ImpactResolver が自分より先に突進を終える。今の状態だけを見ると、当てた瞬間に解ける
    if (m_slam.wasSlamming)
    {
        return;
    }
    // 放したフレームに出せなかった突進は予約に残る。解くと、予約から出るまでの間だけ立ち姿に戻る
    if (m_request.bufferRemaining > 0.0f)
    {
        return;
    }
    ChangeCurled(false);
}
