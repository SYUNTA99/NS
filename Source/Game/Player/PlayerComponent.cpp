#include "Game/Player/PlayerComponent.h"

#include "Game/Entity/EntityStateManager.h"
#include "Game/Level/LaunchedBody.h"
#include "Game/Player/PlayerStateManager.h"
#include "Game/Player/States/BodySlamPlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/LedgeClimbingPlayerState.h"
#include "Game/Player/States/LedgeHangingPlayerState.h"
#include "Game/Player/States/ReboundPlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Object/Components/CameraBrain.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    // 掴まりの走査で見る AABB 群。physics 未設定なら空を返し、掴めないだけにする
    [[nodiscard]] std::vector<NS::Core::AABB> BoxesTouchingBand(const NS::Phys::PhysicsScene* physics,
                                                                const NS::Core::Vector3& probe,
                                                                float below,
                                                                float above)
    {
        if (physics == nullptr)
            return {};

        NS::Core::AABB region;
        region.Center = NS::Core::Vector3{probe.x, probe.y + 0.5f * (above - below), probe.z};
        region.Extents = NS::Core::Vector3{0.0f, 0.5f * (above + below), 0.0f};
        return physics->OverlapBox(region);
    }

    [[nodiscard]] std::vector<NS::Core::AABB> BoxesAtPoint(const NS::Phys::PhysicsScene* physics,
                                                           const NS::Core::Vector3& point)
    {
        if (physics == nullptr)
            return {};

        NS::Core::AABB region;
        region.Center = point;
        region.Extents = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        return physics->OverlapBox(region);
    }

    [[nodiscard]] bool AABBContainsPoint(const NS::Core::AABB& box, const NS::Core::Vector3& p) noexcept
    {
        return p.x >= box.Center.x - box.Extents.x && p.x <= box.Center.x + box.Extents.x &&
               p.y >= box.Center.y - box.Extents.y && p.y <= box.Center.y + box.Extents.y &&
               p.z >= box.Center.z - box.Extents.z && p.z <= box.Center.z + box.Extents.z;
    }

    // 水平の向き from を Y 軸まわりに radians だけ回す。正の角度は +X を -Z の側へ回す
    [[nodiscard]] NS::Core::Vector3 RotateHorizontal(const NS::Core::Vector3& from, float radians) noexcept
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return NS::Core::Vector3{from.x * c + from.z * s, 0.0f, -from.x * s + from.z * c};
    }

    // from を RotateHorizontal で回して to へ重ねる角度。-π..π
    [[nodiscard]] float HorizontalAngleBetween(const NS::Core::Vector3& from, const NS::Core::Vector3& to) noexcept
    {
        return std::atan2(from.z * to.x - from.x * to.z, from.x * to.x + from.z * to.z);
    }

    // from と to は正規化した水平の向き。Y 軸まわりに最大 maxRadians だけ to へ寄せる
    [[nodiscard]] NS::Core::Vector3 TurnHorizontalToward(const NS::Core::Vector3& from,
                                                         const NS::Core::Vector3& to,
                                                         float maxRadians) noexcept
    {
        const float angle = HorizontalAngleBetween(from, to);
        if (std::abs(angle) <= maxRadians)
        {
            return to;
        }
        float step = maxRadians;
        if (angle < 0.0f)
        {
            step = -maxRadians;
        }
        return RotateHorizontal(from, step);
    }
} // namespace

namespace NS::Game::Player
{
    void PlayerComponent::SetDesiredMove(const NS::Core::Vector3& worldDir, float speedScale01) noexcept
    {
        m_desiredDir = worldDir;
        m_desiredSpeedScale = NS::Core::Clamp(speedScale01, 0.0f, 1.0f);
    }

    void PlayerComponent::SetClimbMove(float localRight, float localForward) noexcept
    {
        m_climbRight = NS::Core::Clamp(localRight, -1.0f, 1.0f);
        m_climbForward = NS::Core::Clamp(localForward, -1.0f, 1.0f);
    }

    void PlayerComponent::SetJumpPressed() noexcept
    {
        m_jumpPressedThisFrame = true;
    }

    void PlayerComponent::SetReleaseLedgePressed() noexcept
    {
        m_releaseLedgePressedThisFrame = true;
    }

    void PlayerComponent::SetJumpHeld(bool held) noexcept
    {
        m_jumpHeld = held;
    }

    void PlayerComponent::SetMaxSpeedScale(float scale) noexcept
    {
        // 非数を入れると MaxSpeed() との比較が偽になり、突進明けに水平の速さが切られない
        if (!std::isfinite(scale))
        {
            return;
        }
        m_maxSpeedScale = scale;
    }

    void PlayerComponent::RequestBodySlam(float charge01) noexcept
    {
        // そのフレームで出せないと押しが無言で消える。ジャンプと同じ先行入力時間だけ覚える
        m_bodySlamBufferRemaining = m_jumpBufferTime;
        // 非数は 0..1 への丸めを素通りして溜め量に残るため、入口で 0 へ倒す
        if (!std::isfinite(charge01))
            m_bodySlamRequestCharge01 = 0.0f;
        else
            m_bodySlamRequestCharge01 = NS::Core::Clamp(charge01, 0.0f, 1.0f);
    }

    bool PlayerComponent::IsBodySlamming() const noexcept
    {
        return m_stateManager != nullptr && m_stateManager->IsCurrent<BodySlamPlayerState>();
    }

    float PlayerComponent::BodySlamProgress01() const noexcept
    {
        if (!IsBodySlamming() || !(m_bodySlamDistanceTarget > 0.0f))
            return 0.0f;
        return NS::Core::Clamp(m_bodySlamTravelled / m_bodySlamDistanceTarget, 0.0f, 1.0f);
    }

    NS::Core::Vector3 PlayerComponent::BodySlamVelocity() const noexcept
    {
        if (!IsBodySlamming())
            return Velocity();

        float speed = m_bodySlamSpeed;
        if (m_bodySlamIsTap)
            speed = m_tapSlamSpeed;
        return NS::Core::Vector3{m_bodySlamDir.x * speed, VerticalVelocity(), m_bodySlamDir.z * speed};
    }

    void PlayerComponent::CancelBodySlam() noexcept
    {
        if (!IsBodySlamming())
            return;
        EndBodySlam();
    }

    void PlayerComponent::EndBodySlam() noexcept
    {
        m_bodySlamTravelled = 0.0f;
        m_bodySlamDistanceTarget = 0.0f;
        // 残すと、次の溜めが前の突進で寄せた分を累計に引き継ぎ、放す時に溜めていない分まで回る
        ForgetHoming();

        // 加速は最高速を超えた速さを削らない。切らないと、倒している間は突進の速さのまま走り続ける
        const NS::Core::Vector3 lateral = LateralVelocity();
        const float speed = std::sqrt(lateral.x * lateral.x + lateral.z * lateral.z);
        const float cap = MaxSpeed();
        if (speed > cap)
        {
            const float scale = cap / speed;
            SetLateralVelocity(NS::Core::Vector3{lateral.x * scale, 0.0f, lateral.z * scale});
        }

        if (m_stateManager != nullptr)
        {
            if (IsGrounded())
            {
                m_stateManager->Change<WalkPlayerState>();
            }
            else
            {
                m_stateManager->Change<FallPlayerState>();
            }
        }
        m_playerEvents.onBodySlamEnded.Invoke();
    }

    NS::Core::Vector3 PlayerComponent::AimDirection() const noexcept
    {
        NS::Core::Vector3 dir{m_desiredDir.x, 0.0f, m_desiredDir.z};
        float length = std::sqrt(dir.x * dir.x + dir.z * dir.z);

        // 反発後の滑りなど残った速度が向きに勝つと狙いと食い違う方へ飛ぶ。入力が無ければ速度よりカメラの前を先に見る
        if (length < NS::Core::k_Epsilon && Owner() != nullptr && Owner()->OwningScene() != nullptr)
        {
            if (NS::Obj::CameraBrain* brain = Owner()->OwningScene()->CameraBrain())
            {
                const NS::Core::Vector3 forward = brain->ForwardHorizontal();
                dir = NS::Core::Vector3{forward.x, 0.0f, forward.z};
                length = std::sqrt(dir.x * dir.x + dir.z * dir.z);
            }
        }
        if (length < NS::Core::k_Epsilon)
        {
            const NS::Core::Vector3 lateral = LateralVelocity();
            dir = NS::Core::Vector3{lateral.x, 0.0f, lateral.z};
            length = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        }
        if (length < NS::Core::k_Epsilon)
            return NS::Core::Vector3{0.0f, 0.0f, 0.0f};

        return NS::Core::Vector3{dir.x / length, 0.0f, dir.z / length};
    }

    void PlayerComponent::MarkBodySlamAim() noexcept
    {
        m_bodySlamAimDir = AimDirection();
        m_bodySlamAimAge = 0.0f;
    }

    void PlayerComponent::SteerToward(const NS::Core::Vector3& targetCenter, float coneDegrees) noexcept
    {
        if (Owner() == nullptr)
        {
            return;
        }
        const bool rushing = IsBodySlamming();
        // タップは短い踏み込みの移動技なので、跳んでいる間の向きは変えない
        if (rushing && m_bodySlamIsTap)
        {
            return;
        }

        // 溜めている間の基準は回していない狙いなので、基準からの角度がそのまま累計の目標になる。
        // 突進の向きは累計だけ回した後の向きなので、基準からの角度に累計を足して目標にする
        NS::Core::Vector3 base = AimDirection();
        float baseAngle = 0.0f;
        if (rushing)
        {
            base = m_bodySlamDir;
            baseAngle = m_homingAngle;
        }
        NS::Core::Vector3 baseDir{};
        NS::Core::Vector3 toTarget{};
        if (!NS::Core::TryNormalizeHorizontal(base, baseDir) ||
            !NS::Core::TryNormalizeHorizontal(targetCenter - RootTransform().Position(), toTarget))
        {
            return;
        }

        const float relative = NS::Core::ToDegrees(NS::Core::Radians{HorizontalAngleBetween(baseDir, toTarget)}).value;
        // 非数の相手は正規化を通り抜ける。切りも比較も非数を止めないので、通すと累計と突進の向きが非数になる
        if (!std::isfinite(relative))
        {
            return;
        }
        // 溜めた量は同じ相手を狙い続けた分だけ。持ち越すと、放す直前に移った相手へ溜めた分が一度に乗る
        // TODO: 相手を中心で見分けている。動いている相手は毎フレーム中心が変わるので、溜めている間の寄せが溜まらない
        // 動く的を置く時は、相手の番号を受け取って見分ける
        if (!rushing && m_hasHomingTarget && !(m_homingTarget == targetCenter))
        {
            m_homingAngle = 0.0f;
        }
        const float limit = std::max(0.0f, m_homingMaxDegrees);
        const float goal = NS::Core::Clamp(baseAngle + relative, -limit, limit);
        const float step = std::max(0.0f, m_homingStepDegrees);
        float next = std::max(goal, m_homingAngle - step);
        if (goal > m_homingAngle)
        {
            next = std::min(goal, m_homingAngle + step);
        }

        const float change = next - m_homingAngle;
        m_homingAngle = next;
        if (!rushing)
        {
            m_homingTarget = targetCenter;
            m_homingTargetConeDegrees = coneDegrees;
            m_hasHomingTarget = true;
            return;
        }
        // 速度は UpdateBodySlam が毎フレーム突進の向きから書き直すので、向きを回せば軌道が曲がる
        m_bodySlamDir = RotateHorizontal(m_bodySlamDir, NS::Core::ToRadians(NS::Core::Degrees{change}).value);
    }

    void PlayerComponent::ForgetHoming() noexcept
    {
        m_homingAngle = 0.0f;
        m_hasHomingTarget = false;
    }

    float PlayerComponent::HomingAngleForRelease(const NS::Core::Vector3& releaseDir) const noexcept
    {
        if (!m_hasHomingTarget || Owner() == nullptr)
        {
            return 0.0f;
        }
        NS::Core::Vector3 toTarget{};
        if (!NS::Core::TryNormalizeHorizontal(m_homingTarget - RootTransform().Position(), toTarget))
        {
            return 0.0f;
        }
        const float relative =
            NS::Core::ToDegrees(NS::Core::Radians{HorizontalAngleBetween(releaseDir, toTarget)}).value;
        // 探した角度の外の相手へ回すと、狙っていない相手へ引かれる。放す向きが溜めていた狙いと違う時に起きる
        if (!(std::abs(relative) <= m_homingTargetConeDegrees))
        {
            return 0.0f;
        }
        // 累計は溜めていた狙いから測った角度なので、符号は使わず大きさだけを溜めた量として使う
        const float earned = std::abs(m_homingAngle);
        return NS::Core::Clamp(relative, -earned, earned);
    }

    void PlayerComponent::SetCurled(bool curled) noexcept
    {
        // 掴まりからは突進が出ない。玉のままぶら下がると、押しても突進が出ないのに玉の見た目だけが残る
        // 掴まっている間に玉にすると縁を測り直す手の高さが下がり、押したフレームに縁を放して 0.5 m 落ちた
        if (curled && m_stateManager != nullptr &&
            (m_stateManager->IsCurrent<LedgeHangingPlayerState>() ||
             m_stateManager->IsCurrent<LedgeClimbingPlayerState>()))
        {
            return;
        }
        ChangeCurled(curled);
    }

    void PlayerComponent::ChangeCurled(bool curled) noexcept
    {
        if (curled == m_curled)
        {
            return;
        }
        m_curled = curled;
        // 立ち姿へ戻った後まで寄せた角度を残すと、次の溜めへ持ち越す
        if (!curled)
        {
            ForgetHoming();
        }
        SetSphereShape(curled);
        // 立ち姿の下端は 中心 − 半長 − 半径、玉の下端は 中心 − 半径。中心を立ち姿の半長ぶん上げ下げすると下端が揃う
        // 下げずに玉にすると、当たりの下端が半長ぶん上がる
        float rise = StandingHalfHeight();
        if (curled)
        {
            rise = -rise;
        }
        // TODO: 低い天井の下で立ち姿へ戻す時の検査は無い。コースに低い天井が無いうちは、
        // 作り直したキャラクターの食い込みは次の Step の接触の解決に任せる
        if (Owner() == nullptr)
        {
            return;
        }
        // 形の持ち替えは動きではないので、前フレームの位置も一緒にずらす。今の位置だけを動かすと、持ち替えたフレームの
        // 描画の補間で玉が床から浮き (立ち姿は床へ沈み)、立ち姿の中心を見る追従カメラの注視点も半長ぶん揺れる
        RootTransform().ShiftPosition(NS::Core::Vector3{0.0f, rise, 0.0f});
    }

    void PlayerComponent::SetBodySlamHeld(bool held) noexcept
    {
        m_bodySlamHeld = held;
    }

    void PlayerComponent::UncurlWhenSettled() noexcept
    {
        if (!m_curled)
        {
            return;
        }
        if (m_bodySlamHeld || IsBodySlamming() || !IsGrounded())
        {
            return;
        }
        // 反動が明けたフレームは接地の印が残ったまま上向きの速度が入る。速度を見ないと宙へ出る前に解ける
        if (VerticalVelocity() > 0.0f)
        {
            return;
        }
        // 当てたフレームは ImpactResolver が自分より先に突進を終える。今の状態だけを見ると、当てた瞬間に解ける
        if (m_wasBodySlamming)
        {
            return;
        }
        // 放したフレームに出せなかった突進は予約に残る。解くと、予約から出るまでの間だけ立ち姿に戻る
        if (m_bodySlamBufferRemaining > 0.0f)
        {
            return;
        }
        ChangeCurled(false);
    }

    float PlayerComponent::BodySlamAimBlend01() const noexcept
    {
        const float hold = m_slamAimHoldTime;
        const float fade = m_slamAimFadeTime;
        if (m_bodySlamAimAge <= hold)
            return 1.0f;
        // 巻き戻し秒を消える秒より後ろにできる。幅が 0 以下なら割らずに切る
        if (!(fade > hold) || m_bodySlamAimAge >= fade)
            return 0.0f;
        return (fade - m_bodySlamAimAge) / (fade - hold);
    }

    bool PlayerComponent::BodySlam() noexcept
    {
        NS::Core::Vector3 dir = AimDirection();

        const float aimLength =
            std::sqrt(m_bodySlamAimDir.x * m_bodySlamAimDir.x + m_bodySlamAimDir.z * m_bodySlamAimDir.z);
        if (aimLength >= NS::Core::k_Epsilon)
        {
            const float blend = BodySlamAimBlend01();
            if (blend > 0.0f)
            {
                NS::Core::Vector3 mixed{dir.x * (1.0f - blend) + m_bodySlamAimDir.x * blend,
                                        0.0f,
                                        dir.z * (1.0f - blend) + m_bodySlamAimDir.z * blend};
                const float mixedLength = std::sqrt(mixed.x * mixed.x + mixed.z * mixed.z);
                // 正反対の向きを同じくらいの重みで混ぜると長さが 0 近くになる。その時は濃い側をそのまま採る
                if (mixedLength >= NS::Core::k_Epsilon)
                    dir = NS::Core::Vector3{mixed.x / mixedLength, 0.0f, mixed.z / mixedLength};
                else if (blend >= 0.5f)
                    dir = m_bodySlamAimDir;
            }
        }

        if (std::sqrt(dir.x * dir.x + dir.z * dir.z) < NS::Core::k_Epsilon)
            return false;

        // 溜めている間に寄せた分は、控えた相手を放す向きから測り直して乗せる。突進中の寄せはその続きから数える。
        // タップは短い踏み込みの移動技なので、溜めた分も乗せない
        const bool isTap = !(m_bodySlamRequestCharge01 > 0.0f);
        float releaseHoming = 0.0f;
        if (!isTap)
        {
            releaseHoming = HomingAngleForRelease(dir);
        }
        dir = RotateHorizontal(dir, NS::Core::ToRadians(NS::Core::Degrees{releaseHoming}).value);
        m_bodySlamDir = dir;
        m_bodySlamCharge01 = m_bodySlamRequestCharge01;
        m_bodySlamIsTap = isTap;
        m_bodySlamTravelled = 0.0f;
        m_bodySlamJustStarted = true;

        if (m_bodySlamIsTap)
        {
            m_bodySlamDistanceTarget = m_tapSlamDistance;
            SetVelocity(NS::Core::Vector3{dir.x * m_tapSlamSpeed, m_tapSlamUpSpeed, dir.z * m_tapSlamSpeed});
        }
        else
        {
            m_bodySlamDistanceTarget = m_bodySlamDistance;
            SetVelocity(NS::Core::Vector3{dir.x * m_bodySlamSpeed, VerticalVelocity(), dir.z * m_bodySlamSpeed});
        }

        // 距離が 0 以下だと 1 フレーム目で終わって発動が消えるため、出さずに通常移動のままにする
        if (!(m_bodySlamDistanceTarget > 0.0f))
            return false;

        // 控えた相手は放す時に使い切る。突進中は突進の向きから探し直した相手へ寄せる
        ForgetHoming();
        m_homingAngle = releaseHoming;
        m_bodySlamSpent = true;
        // 突進はどの経路で出ても玉で走らせる。掴まり中に放した押しは予約に残り、先行入力の秒の内に
        // 縁を離れれば出るが、その時の丸まりは掴まりで解けている
        ChangeCurled(true);

        if (m_stateManager != nullptr)
        {
            m_stateManager->Change<BodySlamPlayerState>();
        }
        m_playerEvents.onBodySlamStarted.Invoke();
        return true;
    }

    void PlayerComponent::TapSlamGravity(float dt) noexcept
    {
        // 進み切る前に着地すると残りを地面の上で滑り、走っていないのに動いて見える。
        // 滞空秒を踏み込みの秒へ合わせ、進み切った所で足が着くようにする
        const float airSeconds = (m_tapSlamSpeed > 0.0f) ? m_tapSlamDistance / m_tapSlamSpeed : 0.0f;
        // Inspector で 0 を置くと 0 除算で位置まで非有限値が伝わるため、距離か初速が 0 なら通常の重力へ戻す
        if (!(airSeconds > NS::Core::k_Epsilon))
        {
            Gravity(dt);
            return;
        }

        // 上下対称の弧なので、山の高さは tapSlamUpSpeed * airSeconds / 4 で決まる。
        // 高さを変えたい時に触るのは tapSlamUpSpeed で、ここは触らない
        const float g = -2.0f * m_tapSlamUpSpeed / airSeconds;
        NS::Game::Entity::EntityComponent::Gravity(g, dt);
    }

    void PlayerComponent::UpdateBodySlam(float dt) noexcept
    {
        // 突進中に向きを変えられると当てる間合いを詰める意味が消えるので、水平は発動時の値で書き直す
        if (!m_bodySlamIsTap)
        {
            SetLateralVelocity(
                NS::Core::Vector3{m_bodySlamDir.x * m_bodySlamSpeed, 0.0f, m_bodySlamDir.z * m_bodySlamSpeed});
        }

        if (m_bodySlamIsTap)
        {
            TapSlamGravity(dt);
        }
        else
        {
            Gravity(dt);
        }
    }

    bool PlayerComponent::BeginRebound(const ReboundArc& arc) noexcept
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

        m_reboundDir = direction;
        SetVelocity(velocity);
        if (m_stateManager != nullptr)
        {
            m_stateManager->Change<ReboundPlayerState>();
        }
        return true;
    }

    NS::Core::Vector3 PlayerComponent::ReboundVelocityFor(const ReboundArc& arc) const noexcept
    {
        // 下りは普段の落ち方のままにする
        // 曲線は下りの重力を上りの重力に対する倍率で持つので、下降重力を上りの重力で割る
        const float riseGravity = -m_gravityUp * m_reboundRiseGravityScale;
        const NS::Game::Level::LaunchArc launchArc{.direction = arc.direction,
                                                   .distance = arc.distance,
                                                   .apexHeight = arc.apexHeight,
                                                   .riseGravity = riseGravity,
                                                   .fallGravityScale = -m_gravityDown / riseGravity,
                                                   .apexBandSpeed = m_apexHangVy,
                                                   .apexBandGravityScale = m_apexHangScale};
        return NS::Game::Level::LaunchArcInitialVelocity(launchArc);
    }

    bool PlayerComponent::IsRebounding() const noexcept
    {
        return m_stateManager != nullptr && m_stateManager->IsCurrent<ReboundPlayerState>();
    }

    void PlayerComponent::ReboundGravity(float dt) noexcept
    {
        if (!(VerticalVelocity() > 0.0f))
        {
            Gravity(dt);
            return;
        }

        float g = m_gravityUp * m_reboundRiseGravityScale;
        if (std::abs(VerticalVelocity()) < m_apexHangVy)
        {
            g = g * m_apexHangScale;
        }
        NS::Game::Entity::EntityComponent::Gravity(g, dt);
    }

    void PlayerComponent::AccelerateDuringRebound(float dt) noexcept
    {
        NS::Core::Vector3 direction{};
        if (!HasMoveInput() || !NS::Core::TryNormalizeHorizontal(m_desiredDir, direction))
        {
            return;
        }

        const float topSpeed = std::max(MaxSpeed() * m_desiredSpeedScale, m_walkSpeed);
        // 入力の向きからずれた速度は削らない。削ると横へ倒しただけで相手から離れる流れが消え、
        // 弾かれる向きが当て方でなくスティックで決まる。触って詰める値ではないので欄にしない
        const float turningDrag = 0.0f;
        // 明けのフレームは止める前の接地の印が残っている。接地を見て地上の加速度を選ぶと、そのフレームだけ大きく曲がる
        Accelerate(direction, turningDrag, m_reboundAirAcceleration, topSpeed, dt);
    }

    bool PlayerComponent::ShouldLand() const noexcept
    {
        // 明けのフレームは止める前の接地の印が残ったまま上向きの速度が入る。接地だけを見ると宙へ出る前に立ちへ移る
        return IsGrounded() && !(VerticalVelocity() > 0.0f);
    }

    void PlayerComponent::ResetState() noexcept
    {
        SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
        m_desiredDir = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        m_desiredSpeedScale = 0.0f;
        m_climbRight = 0.0f;
        m_climbForward = 0.0f;
        m_jumpHeld = false;
        m_prevJumpHeld = false;
        m_jumpPressedThisFrame = false;
        m_releaseLedgePressedThisFrame = false;
        m_jumpsRemaining = 1;
        m_coyoteTimer = 0.0f;
        m_bufferTimer = 0.0f;
        SetGrounded(false);
        m_ledgeTopY = 0.0f;
        m_ledgeFaceNormal = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        m_ledgeMantleTimer = 0.0f;
        m_facingDir = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        m_lastMoveDistance = 0.0f;
        m_bodySlamBufferRemaining = 0.0f;
        m_bodySlamSpent = false;
        m_bodySlamIsTap = false;
        m_bodySlamRequestCharge01 = 0.0f;
        m_bodySlamCharge01 = 0.0f;
        m_bodySlamTravelled = 0.0f;
        m_bodySlamDistanceTarget = 0.0f;
        m_bodySlamJustStarted = false;
        m_bodySlamDir = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        m_reboundDir = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        ForgetHoming();
        // 当たりの形だけを立ち姿へ戻し、根は動かさない。出直しは根を出現位置へ置いてから呼ぶので、
        // 丸まりを解く時のように根を上げると出現位置より半長ぶん高く湧いた
        m_curled = false;
        SetSphereShape(false);
        m_bodySlamHeld = false;
        m_wasBodySlamming = false;

        if (m_stateManager != nullptr)
            m_stateManager->ResetToFirst();
    }

    void PlayerComponent::OnStart()
    {
        NS::Game::Entity::EntityComponent::OnStart();

        if (Owner() == nullptr)
            return;

        m_stateManager = Owner()->FindComponent<PlayerStateManager>();
    }

    NS::Game::Entity::EntityStateManager* PlayerComponent::States() const noexcept
    {
        return m_stateManager;
    }

    void PlayerComponent::TickTimers(float dt) noexcept
    {
        m_bufferTimer -= dt;
        if (m_jumpPressedThisFrame)
            m_bufferTimer = m_jumpBufferTime;

        const bool inAir = !IsGrounded();
        if (inAir)
            m_coyoteTimer -= dt;
    }

    void PlayerComponent::AccelerateToInputDirection(float dt) noexcept
    {
        NS::Core::Vector3 direction{};
        if (!HasMoveInput() || !NS::Core::TryNormalizeHorizontal(m_desiredDir, direction))
        {
            return;
        }

        const float topSpeed = std::max(MaxSpeed() * m_desiredSpeedScale, m_walkSpeed);
        float acceleration = m_airAcceleration;
        if (IsGrounded())
        {
            acceleration = m_acceleration;
        }
        Accelerate(direction, m_turningDrag, acceleration, topSpeed, dt);
    }

    void PlayerComponent::ApplyFriction(float dt) noexcept
    {
        Decelerate(m_friction, dt);
    }

    void PlayerComponent::ApplyBrake(float dt) noexcept
    {
        Decelerate(m_deceleration, dt);
    }

    void PlayerComponent::Jump(float) noexcept
    {
        const bool canGroundJump = (IsGrounded() || m_coyoteTimer > 0.0f) && m_jumpsRemaining > 0;
        const bool wantJump = m_jumpPressedThisFrame || m_bufferTimer > 0.0f;
        if (canGroundJump && wantJump)
        {
            SetVerticalVelocity(m_jumpImpulse);
            --m_jumpsRemaining;
            m_bufferTimer = 0.0f;
            m_coyoteTimer = 0.0f;
            m_playerEvents.onJump.Invoke();
        }
    }

    void PlayerComponent::CutJumpRelease() noexcept
    {
        if (m_prevJumpHeld && !m_jumpHeld && VerticalVelocity() > 0.0f)
            SetVerticalVelocity(VerticalVelocity() * m_jumpReleaseScale);
    }

    void PlayerComponent::Gravity(float dt) noexcept
    {
        const bool apex = std::abs(VerticalVelocity()) < m_apexHangVy;

        float baseG = m_gravityDown;
        if (VerticalVelocity() > 0.0f)
            baseG = m_gravityUp;

        float g = baseG;
        if (apex)
            g = baseG * m_apexHangScale;

        NS::Game::Entity::EntityComponent::Gravity(g, dt);
    }

    void PlayerComponent::HandleMovement(float dt) noexcept
    {
        // 壁に当たった後の速度からは面へ向かう分が抜ける。掴む向きに使うので、抜ける前の向きを覚える
        NS::Core::Vector3 target{};
        if (NS::Core::TryNormalizeHorizontal(LateralVelocity(), target))
        {
            // 一定の速さで回す。速さの理由は m_turnSpeed の欄
            NS::Core::Vector3 current{};
            if (m_turnSpeed > 0.0f && NS::Core::TryNormalizeHorizontal(m_facingDir, current))
            {
                const float maxTurn = NS::Core::ToRadians(NS::Core::Degrees{m_turnSpeed * dt}).value;
                m_facingDir = TurnHorizontalToward(current, target, maxTurn);
            }
            else
            {
                m_facingDir = target;
            }
        }

        const NS::Core::Vector3 before = RootTransform().Position();
        NS::Game::Entity::EntityComponent::Move(dt, m_maxStepHeight);
        const NS::Core::Vector3 delta = RootTransform().Position() - before;
        m_lastMoveDistance = delta.Length();
        SyncGroundState();
        AdvanceBodySlamTravel(delta);
        m_wasBodySlamming = IsBodySlamming();
    }

    void PlayerComponent::AdvanceBodySlamTravel(const NS::Core::Vector3& delta) noexcept
    {
        if (!IsBodySlamming())
        {
            return;
        }

        // 進んだ距離は実際に動いた量から測る。突進の速さから積むと壁で止められたフレームも進んだ扱いになる
        const float stepDistance = std::sqrt(delta.x * delta.x + delta.z * delta.z);
        m_bodySlamTravelled += stepDistance;

        // 進めないフレームで打ち切る。壁で止められると進んだ距離が伸びず、突進から出られなくなる
        // 発動したフレームは見ない。ここで打ち切ると発動から打ち切りまでに ImpactResolver が
        // 一度も走らず、突進を見ないまま終わる
        const bool stalled = !m_bodySlamJustStarted && stepDistance < NS::Core::k_Epsilon;
        m_bodySlamJustStarted = false;

        if (m_bodySlamTravelled >= m_bodySlamDistanceTarget || stalled)
        {
            EndBodySlam();
        }
    }

    void PlayerComponent::SyncGroundState() noexcept
    {
        if (!WasGrounded() && IsGrounded())
            m_jumpsRemaining = 1;

        if (IsGrounded())
        {
            m_coyoteTimer = m_coyoteTime;
            // 着地のフレームだけで戻すと、接地したまま走り抜けた突進の後に次が出せない
            m_bodySlamSpent = false;
        }
    }

    bool PlayerComponent::LedgeGrab() noexcept
    {
        // 空中で下降中に、向いている方の縁を掴む
        if (IsGrounded() || VerticalVelocity() > 0.0f)
        {
            return false;
        }
        // 当てたフレームは ImpactResolver が自分より先に突進を終え、落下の 1 フレームがここを通る
        // 掴むと止めの前に立ち姿へ戻り、ぶら下がりの位置から反動に入る
        if (m_wasBodySlamming)
        {
            return false;
        }

        NS::Core::Vector3 dir{};
        if (!NS::Core::TryNormalizeHorizontal(m_facingDir, dir))
        {
            return false;
        }

        // 手の高さ = カプセルの円柱部の上端。そこから前方へ伸ばした probe 点がブロックの XZ 内に入り、
        // かつブロック上端が手の上下の帯に収まれば縁とみなす
        // 縁は立ち姿で掴む。当たりの足元に立ち姿を立てた中心と、立ち姿の半長で測る。玉の間は根が半長ぶん下がっている
        // 玉の寸法のまま測ると手が円柱の長さぶん低い所を探し、縁の横を玉で落ちている間は掴めなかった
        const float halfHeight = StandingHalfHeight();
        NS::Core::Vector3 pos = RootTransform().Position();
        pos.y += halfHeight - CapsuleHalfHeight();
        const float handY = pos.y + halfHeight;
        const NS::Core::Vector3 probe{
            pos.x + dir.x * (CapsuleRadius() + m_ledgeReach),
            handY,
            pos.z + dir.z * (CapsuleRadius() + m_ledgeReach),
        };

        // 帯の上は今フレーム動いた距離まで。速く落ちると 1 フレームで縁の上端を通り過ぎて掴み損ねる
        const float above = m_lastMoveDistance;
        for (const NS::Core::AABB& box : BoxesTouchingBand(ScenePhysics(), probe, m_ledgeGrabBelowHand, above))
        {
            const float top = box.Center.y + box.Extents.y;
            if (top < handY - m_ledgeGrabBelowHand || top > handY + above)
            {
                continue;
            }
            if (probe.x < box.Center.x - box.Extents.x || probe.x > box.Center.x + box.Extents.x)
                continue;
            if (probe.z < box.Center.z - box.Extents.z || probe.z > box.Center.z + box.Extents.z)
                continue;

            // 接近軸の優勢成分で掴む手前面を決め、その外側にカプセルを寄せた hang 位置を出す
            NS::Core::Vector3 faceNormal{0.0f, 0.0f, 0.0f};
            NS::Core::Vector3 hang = pos;
            if (std::abs(dir.x) >= std::abs(dir.z))
            {
                float sgn = -1.0f;
                if (dir.x >= 0.0f)
                    sgn = 1.0f;
                const float faceX = box.Center.x - sgn * box.Extents.x;
                faceNormal = NS::Core::Vector3{-sgn, 0.0f, 0.0f};
                hang.x = faceX - sgn * CapsuleRadius();
                hang.z = NS::Core::Clamp(pos.z, box.Center.z - box.Extents.z, box.Center.z + box.Extents.z);
            }
            else
            {
                float sgn = -1.0f;
                if (dir.z >= 0.0f)
                    sgn = 1.0f;
                const float faceZ = box.Center.z - sgn * box.Extents.z;
                faceNormal = NS::Core::Vector3{0.0f, 0.0f, -sgn};
                hang.z = faceZ - sgn * CapsuleRadius();
                hang.x = NS::Core::Clamp(pos.x, box.Center.x - box.Extents.x, box.Center.x + box.Extents.x);
            }
            hang.y = top - halfHeight;

            // 上面手前の登り先が別ブロックで塞がっているなら縁ではない。掴まない
            const float mantleStep = 2.0f * CapsuleRadius();
            const NS::Core::Vector3 mantleCheck{
                hang.x - faceNormal.x * mantleStep,
                top + halfHeight,
                hang.z - faceNormal.z * mantleStep,
            };
            bool blocked = false;
            for (const NS::Core::AABB& other : BoxesAtPoint(ScenePhysics(), mantleCheck))
            {
                if (AABBContainsPoint(other, mantleCheck))
                {
                    blocked = true;
                    break;
                }
            }
            if (blocked)
                continue;

            // 掴まりからは突進が出ないので、立ち姿でぶら下がる。ぶら下がる位置は立ち姿の中心なので、
            // 置く前に解く。置いた後に解くと根が半長ぶん上がる
            ChangeCurled(false);
            RootTransform().SetPosition(hang);
            SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
            m_ledgeTopY = top;
            m_ledgeFaceNormal = faceNormal;
            if (m_stateManager != nullptr)
            {
                m_stateManager->Change<LedgeHangingPlayerState>();
            }
            m_playerEvents.onLedgeGrabbed.Invoke();
            return true;
        }
        return false;
    }

    bool PlayerComponent::HoldLedge() noexcept
    {
        float top = 0.0f;
        if (!FindLedgeTopAt(RootTransform().Position(), top))
        {
            DropLedge();
            return false;
        }

        m_ledgeTopY = top;
        NS::Core::Vector3 pos = RootTransform().Position();
        pos.y = m_ledgeTopY - CapsuleHalfHeight();
        RootTransform().SetPosition(pos);
        SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
        return true;
    }

    bool PlayerComponent::ShouldClimbLedge() const noexcept
    {
        return m_climbForward > 0.0f;
    }

    bool PlayerComponent::LedgeJump() noexcept
    {
        if (!m_jumpPressedThisFrame)
        {
            return false;
        }

        SetVerticalVelocity(m_jumpImpulse);
        SetGrounded(false);
        if (m_stateManager != nullptr)
        {
            m_stateManager->Change<FallPlayerState>();
        }
        m_playerEvents.onJump.Invoke();
        return true;
    }

    bool PlayerComponent::ShouldDropLedge() const noexcept
    {
        return m_releaseLedgePressedThisFrame;
    }

    void PlayerComponent::ClimbLedge() noexcept
    {
        const NS::Core::Vector3 pos = RootTransform().Position();
        // ぶら下がりの中心は面から半径ぶん外。直径ぶん奥へ進めると中心が縁から半径ぶん内側に入り、体が上面に乗る
        const float mantleStep = 2.0f * CapsuleRadius();
        m_ledgeMantleStart = pos;
        m_ledgeMantleEnd = NS::Core::Vector3{
            pos.x - m_ledgeFaceNormal.x * mantleStep,
            m_ledgeTopY + CapsuleHalfHeight() + CapsuleRadius(),
            pos.z - m_ledgeFaceNormal.z * mantleStep,
        };
        m_ledgeMantleTimer = 0.0f;
        if (m_stateManager != nullptr)
        {
            m_stateManager->Change<LedgeClimbingPlayerState>();
        }
        SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
        m_playerEvents.onLedgeClimbing.Invoke();
    }

    void PlayerComponent::DropLedge() noexcept
    {
        if (m_stateManager != nullptr)
        {
            m_stateManager->Change<FallPlayerState>();
        }
        SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
        SetGrounded(false);
        // 壁と逆を向いて落ちる。壁を向いたままだと、帯の上の余白に縁が入って次のフレームで掴み直す
        m_facingDir = m_ledgeFaceNormal;
    }

    void PlayerComponent::Shimmy(float dt) noexcept
    {
        if (m_climbRight != 0.0f)
        {
            // 面法線に水平直交する縁方向。動いても面からの距離は変わらない
            const NS::Core::Vector3 pos = RootTransform().Position();
            const NS::Core::Vector3 alongDir{-m_ledgeFaceNormal.z, 0.0f, m_ledgeFaceNormal.x};
            NS::Core::Vector3 shimmied = pos;
            shimmied.x += alongDir.x * m_climbRight * m_ledgeShimmySpeed * dt;
            shimmied.z += alongDir.z * m_climbRight * m_ledgeShimmySpeed * dt;
            // 移動先にも掴める縁が続いている時だけ動く。端なら止めて落とさない
            float top = 0.0f;
            if (FindLedgeTopAt(shimmied, top))
            {
                RootTransform().SetPosition(shimmied);
            }
        }
    }

    void PlayerComponent::UpdateLedgeClimb(float dt) noexcept
    {
        m_ledgeMantleTimer += dt;
        float t = 1.0f;
        if (m_ledgeClimbDuration > 0.0f)
        {
            t = NS::Core::Clamp(m_ledgeMantleTimer / m_ledgeClimbDuration, 0.0f, 1.0f);
        }

        // 2 段に割るのは角への食い込みを避けるため。前半は上昇だけで前へ進まない
        NS::Core::Vector3 pos{0.0f, 0.0f, 0.0f};
        if (t < 0.5f)
        {
            const float u = t / 0.5f;
            pos.x = m_ledgeMantleStart.x;
            pos.z = m_ledgeMantleStart.z;
            pos.y = m_ledgeMantleStart.y + (m_ledgeMantleEnd.y - m_ledgeMantleStart.y) * u;
        }
        else
        {
            const float u = (t - 0.5f) / 0.5f;
            pos.x = m_ledgeMantleStart.x + (m_ledgeMantleEnd.x - m_ledgeMantleStart.x) * u;
            pos.z = m_ledgeMantleStart.z + (m_ledgeMantleEnd.z - m_ledgeMantleStart.z) * u;
            pos.y = m_ledgeMantleEnd.y;
        }
        RootTransform().SetPosition(pos);
        SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});

        if (t >= 1.0f)
        {
            RootTransform().SetPosition(m_ledgeMantleEnd);
            if (m_stateManager != nullptr)
            {
                m_stateManager->Change<IdlePlayerState>();
            }
            SetGrounded(true);
            m_jumpsRemaining = 1;
            m_coyoteTimer = m_coyoteTime;
        }
    }

    bool PlayerComponent::FindLedgeTopAt(const NS::Core::Vector3& hangPos, float& outTop) const noexcept
    {
        const NS::Core::Vector3 inward{-m_ledgeFaceNormal.x, 0.0f, -m_ledgeFaceNormal.z};
        const float handY = hangPos.y + CapsuleHalfHeight();
        const NS::Core::Vector3 probe{
            hangPos.x + inward.x * (CapsuleRadius() + m_ledgeReach),
            handY,
            hangPos.z + inward.z * (CapsuleRadius() + m_ledgeReach),
        };

        for (const NS::Core::AABB& box : BoxesTouchingBand(ScenePhysics(), probe, m_ledgeGrabBelowHand, 0.0f))
        {
            const float top = box.Center.y + box.Extents.y;
            if (top < handY - m_ledgeGrabBelowHand || top > handY)
            {
                continue;
            }
            if (probe.x < box.Center.x - box.Extents.x || probe.x > box.Center.x + box.Extents.x)
                continue;
            if (probe.z < box.Center.z - box.Extents.z || probe.z > box.Center.z + box.Extents.z)
                continue;

            // 乗り上がり先が別ブロックで塞がっていたら縁とみなさない。オーバーハングの下では掴めない
            const float mantleStep = 2.0f * CapsuleRadius();
            const NS::Core::Vector3 mantleCheck{
                hangPos.x - m_ledgeFaceNormal.x * mantleStep,
                top + CapsuleHalfHeight(),
                hangPos.z - m_ledgeFaceNormal.z * mantleStep,
            };
            bool blocked = false;
            for (const NS::Core::AABB& other : BoxesAtPoint(ScenePhysics(), mantleCheck))
            {
                if (AABBContainsPoint(other, mantleCheck))
                {
                    blocked = true;
                    break;
                }
            }
            if (blocked)
                continue;

            outTop = top;
            return true;
        }
        return false;
    }

    bool PlayerComponent::IsLocomotion() const noexcept
    {
        if (m_stateManager == nullptr)
            return false;
        return m_stateManager->IsCurrent<IdlePlayerState>() || m_stateManager->IsCurrent<WalkPlayerState>() ||
               m_stateManager->IsCurrent<FallPlayerState>() || m_stateManager->IsCurrent<ReboundPlayerState>();
    }

    bool PlayerComponent::ShouldWalk() const noexcept
    {
        if (!IsGrounded())
            return false;
        if (m_desiredSpeedScale >= m_stickDeadzone)
            return true;

        return !IsStopped();
    }

    bool PlayerComponent::ShouldBrake() const noexcept
    {
        NS::Core::Vector3 direction{};
        if (!HasMoveInput() || !NS::Core::TryNormalizeHorizontal(m_desiredDir, direction))
        {
            return false;
        }
        const NS::Core::Vector3 lateral = LateralVelocity();
        return direction.x * lateral.x + direction.z * lateral.z < m_brakeThreshold;
    }

    bool PlayerComponent::HasMoveInput() const noexcept
    {
        return m_desiredSpeedScale >= m_stickDeadzone;
    }

    bool PlayerComponent::IsStopped() const noexcept
    {
        const NS::Core::Vector3 lateral = LateralVelocity();
        return lateral.x == 0.0f && lateral.z == 0.0f;
    }

    bool PlayerComponent::ShouldIdle() const noexcept
    {
        return IsGrounded() && !ShouldWalk();
    }

    bool PlayerComponent::ShouldFall() const noexcept
    {
        return !IsGrounded();
    }

    void PlayerComponent::HandleStates(float dt)
    {
        if (m_stateManager != nullptr)
        {
            // 発動の判定が現在状態を見るので、組むのは 1 フレームの頭。Step の初回に任せると
            // 1 フレーム目だけ現在状態が空になり、そのフレームの押しが 1 フレーム遅れて出る
            m_stateManager->EnsureBuilt(*this);

            // 突進の中で見ると通常移動の 1 フレームを走ってから移ることになり、突進の初速がそのフレームに乗らない
            // 空中の押しを捨てると連打で出ないフレームができるため、接地は求めない
            // 突進を出すのは通常移動のフレームだけ。掴まり中に出せると縁から離れる操作が 1 つ増える
            // 空中で 2 発目まで出せると 1 発の重みが消える。接地するまで次は出さない
            // 当てたフレームは ImpactResolver が自分より先に突進を終えている。ここで出すと突進のまま止められ、
            // 明けの反動に終わりの通知なしで上書きされる。押しは先行入力に残し、明けに反動から出す
            if (m_bodySlamBufferRemaining > 0.0f && !m_bodySlamSpent && !m_wasBodySlamming && IsLocomotion())
            {
                if (BodySlam())
                    m_bodySlamBufferRemaining = 0.0f;
            }

            m_stateManager->Step(*this, dt);
        }

        // 接地は動かした後に決まるので、着地で解けるのは着地した次のフレーム
        UncurlWhenSettled();

        // 1 フレーム限りの入力は、どの状態でも通るここで落とす
        m_prevJumpHeld = m_jumpHeld;
        m_jumpPressedThisFrame = false;
        m_releaseLedgePressedThisFrame = false;
        // 突進中は期限を数えない。踏み込みが先行入力の秒より長いので、数えると明ける前に押しが消える
        if (m_bodySlamBufferRemaining > 0.0f && !IsBodySlamming())
            m_bodySlamBufferRemaining = std::max(0.0f, m_bodySlamBufferRemaining - dt);

        // 発動の判定より後で数える。前だと押した時の狙いが、同じフレームの中で 1 つ古い値になる
        if (m_bodySlamAimAge < m_slamAimFadeTime)
            m_bodySlamAimAge += dt;
    }

    void PlayerComponent::OnStepSkipped()
    {
        m_jumpPressedThisFrame = false;
        m_releaseLedgePressedThisFrame = false;
        m_prevJumpHeld = m_jumpHeld;
    }

    NS_CLASS(PlayerComponent)
} // namespace NS::Game::Player
