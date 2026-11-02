#include "Game/Level/ThirdPersonFollow.h"

#include "Game/Level/IGetCameraTarget.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IGet/IGetCollision.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/Core/Windows/Gamepad.h"
#include "NSlib/Core/Windows/Input.h"
#include "NSlib/Core/Windows/Mouse.h"

#include <algorithm>
#include <cmath>

namespace
{
    [[nodiscard]] float SpringApproach(float curr, float target, float omega, float dt) noexcept
    {
        if (omega <= 0.0f || dt <= 0.0f)
        {
            return target;
        }
        const float a = 1.0f - std::exp(-omega * dt);
        return curr + (target - curr) * a;
    }

    // 臨界減衰のバネを dt 進める。止まった所から一定の目標へは、τ 秒で差の 1 − (1 + ωτ) e^(−ωτ) が詰まる
    void CriticalSpringStep(float& value, float& velocity, float target, float omega, float dt) noexcept
    {
        if (omega <= 0.0f || dt <= 0.0f)
        {
            value = target;
            velocity = 0.0f;
            return;
        }
        const float decay = std::exp(-omega * dt);
        const float gap = value - target;
        const float push = (velocity + omega * gap) * dt;
        velocity = (velocity - omega * push) * decay;
        value = target + (gap + push) * decay;
    }

    void CriticalSpringStep(NS::Vector3& value,
                            NS::Vector3& velocity,
                            const NS::Vector3& target,
                            float omega,
                            float dt) noexcept
    {
        CriticalSpringStep(value.x, velocity.x, target.x, omega, dt);
        CriticalSpringStep(value.y, velocity.y, target.y, omega, dt);
        CriticalSpringStep(value.z, velocity.z, target.z, omega, dt);
    }

    void CriticalSpringStepHorizontal(NS::Vector3& value,
                                      NS::Vector3& velocity,
                                      const NS::Vector3& target,
                                      float omega,
                                      float dt) noexcept
    {
        CriticalSpringStep(value.x, velocity.x, target.x, omega, dt);
        CriticalSpringStep(value.z, velocity.z, target.z, omega, dt);
    }

    // 右は水平で、上は視線と右に直交する
    struct ViewAxes
    {
        NS::Vector3 forward;
        NS::Vector3 right;
        NS::Vector3 up;
    };

    [[nodiscard]] ViewAxes ViewAxesFromYawPitch(float yaw, float pitch) noexcept
    {
        const float cy = std::cos(yaw);
        const float sy = std::sin(yaw);
        const float cp = std::cos(pitch);
        const float sp = std::sin(pitch);
        return ViewAxes{.forward = NS::Vector3{sy * cp, sp, cy * cp},
                        .right = NS::Vector3{cy, 0.0f, -sy},
                        .up = NS::Vector3{-sp * sy, cp, -sp * cy}};
    }
} // namespace

namespace GL
{
    namespace Level
    {

        ThirdPersonFollow::ThirdPersonFollow() noexcept
        {
            // 追う相手を中心に見るので遠景は要らない。編集カメラの 5000 と違いプレイ視点は 100 で足りる
            SetFarPlane(100.0f);
        }

        const NS::Transform* ThirdPersonFollow::Target() const noexcept
        {
            if (m_target == nullptr)
            {
                return nullptr;
            }
            return &m_target->Root();
        }

        void ThirdPersonFollow::SetFollowMotion(bool grounded, const NS::Vector3& velocity) noexcept
        {
            m_followGrounded = grounded;
            const float horiz = NS::HorizontalLength(velocity);
            // 非数を持つと走り判定の比較が常に偽になり、走っても待機時距離のまま
            if (std::isfinite(horiz))
            {
                m_followHorizontalSpeed = horiz;
            }
            else
            {
                m_followHorizontalSpeed = 0.0f;
            }
            m_hasFollowMotion = true;
        }

        void ThirdPersonFollow::SetTargetHeightOffset(float offset) noexcept { m_targetHeightOffset = offset; }

        bool ThirdPersonFollow::SetFollowRebound(const FollowReboundDesc& desc) noexcept
        {
            // 非数の向きを持つと、回す角度が非数になって向きごと壊れる
            if (!NS::IsFinite(desc.slamDirection))
            {
                return false;
            }
            // 非数の縦の向きは帯の中心を非数にし、注視点ごと壊す
            if (!std::isfinite(desc.verticalLean) || desc.verticalLean < -1.0f || desc.verticalLean > 1.0f)
            {
                return false;
            }
            m_rebound = desc;
            return true;
        }

        void ThirdPersonFollow::ClearRebound() noexcept
        {
            m_rebound = FollowReboundDesc{};
            m_wasRebounding = false;
            m_reboundLagCap = 0.0f;
            m_reboundLookHeld = false;
            m_reboundTurnAngle = 0.0f;
            m_reboundTurnFrame = 0;
            m_reboundPhase = ReboundPhase::None;
            m_reboundAnchor = NS::Vector3{0.0f, 0.0f, 0.0f};
            m_reboundAnchorVelocity = NS::Vector3{0.0f, 0.0f, 0.0f};
            m_reboundReturnOffset = NS::Vector3{0.0f, 0.0f, 0.0f};
            m_reboundReturnFrame = 0;
            m_hasLook = false;
            m_reboundBaseDistance = 0.0f;
            m_partnerLean = NS::Vector3{0.0f, 0.0f, 0.0f};
            m_partnerLeanVelocity = NS::Vector3{0.0f, 0.0f, 0.0f};
            m_partnerPull = 0.0f;
            m_partnerPullVelocity = 0.0f;
        }

        void ThirdPersonFollow::UpdateReboundTurn(bool began, const FollowReboundDesc& rebound) noexcept
        {
            // 空中の 1 発が当たって反動になり直した時も、その 1 発を出した向きで回し直す
            if (began)
            {
                m_reboundLookHeld = rebound.holdLook;
                m_reboundTurnAngle = 0.0f;
                m_reboundTurnFrame = 0;
                NS::Vector3 direction{};
                if (NS::TryNormalizeHorizontal(rebound.slamDirection, direction))
                {
                    // 差を −π〜π へ丸めて近い側へ回す
                    // 丸めないと、向きの角度が積もった分だけ余計に回る
                    const float goal = std::atan2(direction.x, direction.z);
                    m_reboundTurnAngle = std::remainder(goal - m_yaw, 2.0f * NS::k_Pi);
                }
                if (m_reboundTurnFrames <= 0)
                {
                    m_yaw += m_reboundTurnAngle;
                    m_reboundTurnAngle = 0.0f;
                }
            }
            else if (m_reboundLookHeld && !rebound.rebounding && (m_followGrounded || !m_hasFollowMotion))
            {
                // 反動の状態が外れても空中の間は受けない
                // 空中の 1 発の後と縁を掴んだ後も、接地までは向きを変えない
                m_reboundLookHeld = false;
            }

            // 回しは接地しても最後まで進める
            // 途中で止めると、回る速さがそのフレームで 0 に落ちる
            // 重みの差を足していくので、回す入力を受けるようになった後はその入力と重なる
            if (m_reboundTurnAngle != 0.0f && m_reboundTurnFrame < m_reboundTurnFrames)
            {
                const float frames = static_cast<float>(m_reboundTurnFrames);
                const float before = NS::SmoothStep(static_cast<float>(m_reboundTurnFrame) / frames);
                ++m_reboundTurnFrame;
                const float after = NS::SmoothStep(static_cast<float>(m_reboundTurnFrame) / frames);
                m_yaw += m_reboundTurnAngle * (after - before);
            }
        }

        void ThirdPersonFollow::UpdateReboundPhase(bool began, bool rebounding, const NS::Vector3& head) noexcept
        {
            // 反動の状態になったフレームに、前のフレームに見ていた所から留める
            // 空中で続けて当てた時も留め直す
            if (began)
            {
                m_reboundPhase = ReboundPhase::Following;
                m_reboundAnchor = head;
                if (m_hasLook)
                {
                    m_reboundAnchor = m_look;
                }
                m_reboundAnchorVelocity = NS::Vector3{0.0f, 0.0f, 0.0f};
                // 入った時の遅れが反動の上限より大きくても、それを上限の初めにして、切って跳ばさない
                m_reboundLagCap = std::max(m_reboundMaxLag, NS::HorizontalLength(m_reboundAnchor - head));
                return;
            }

            // 反動の状態が外れたフレームから寄せ戻す
            // 着地のほか、空中の 1 発と縁を掴んだ時も外れる
            if (m_reboundPhase == ReboundPhase::Following && !rebounding)
            {
                if (m_reboundReturnFrames > 0 && m_hasLook)
                {
                    m_reboundPhase = ReboundPhase::Returning;
                    m_reboundReturnOffset = m_look - head;
                    m_reboundReturnFrame = 0;
                }
                else
                {
                    m_reboundPhase = ReboundPhase::None;
                }
                return;
            }

            // 寄せ戻しの最後のフレームは注視点が頭と同じ所にある
            // その次のフレームから普通の追い方の式へ移す
            if (m_reboundPhase == ReboundPhase::Returning && m_reboundReturnFrame >= m_reboundReturnFrames)
            {
                m_reboundPhase = ReboundPhase::None;
            }
        }

        void ThirdPersonFollow::UpdateReboundPartner(const FollowReboundDesc& rebound,
                                                     const NS::Vector3& head,
                                                     float dt) noexcept
        {
            NS::Vector3 leanTarget{0.0f, 0.0f, 0.0f};
            float pullTarget = 0.0f;
            const float release = std::max(m_reboundPartnerReleaseDistance, 0.0f);
            if (m_reboundPhase == ReboundPhase::Following && rebound.partnerPosition.has_value() && release > 0.0f)
            {
                const NS::Vector3 gap = rebound.partnerPosition.value() - head;
                const float distance = gap.Length();
                // 見送った後は引きも戻す。目標は 0 へ跳ぶが、ばねがなめらかに戻す
                if (std::isfinite(distance) && distance < release)
                {
                    const float weight = NS::Clamp(m_reboundPartnerWeight, 0.0f, 1.0f) * (1.0f - distance / release);
                    leanTarget = gap * weight;
                    pullTarget = NS::Clamp(m_reboundPullPerMeter * (distance - m_reboundPullStartDistance),
                                           0.0f,
                                           std::max(m_reboundPullMax, 0.0f));
                }
            }
            // 差の半分まで詰める秒から角速度を出す
            float omega = 0.0f;
            if (m_reboundPartnerHalfSeconds > 0.0f)
            {
                omega = NS::k_CriticalSpringHalfOmegaTau / m_reboundPartnerHalfSeconds;
            }
            CriticalSpringStep(m_partnerLean, m_partnerLeanVelocity, leanTarget, omega, dt);
            CriticalSpringStep(m_partnerPull, m_partnerPullVelocity, pullTarget, omega, dt);
        }

        NS::Vector3 ThirdPersonFollow::UpdateReboundLook(const NS::Vector3& head,
                                                         const NS::Vector3& ball,
                                                         float verticalLean,
                                                         float dt) noexcept
        {
            m_previousLook = m_look;
            m_hasLook = true;
            if (m_reboundPhase == ReboundPhase::None)
            {
                m_look = head;
                return m_look;
            }

            if (m_reboundPhase == ReboundPhase::Returning)
            {
                ++m_reboundReturnFrame;
                const float t = static_cast<float>(m_reboundReturnFrame) / static_cast<float>(m_reboundReturnFrames);
                if (t >= 1.0f)
                {
                    m_look = head;
                }
                else
                {
                    m_look = head + m_reboundReturnOffset * (1.0f - NS::SmoothStep(t));
                }
                return m_look;
            }

            // 横と前後は臨界減衰のバネで遅れて付いていき、遅れは上限の内に留める
            // 高さは留めたまま
            CriticalSpringStepHorizontal(m_reboundAnchor, m_reboundAnchorVelocity, head, m_reboundFollowOmega, dt);
            const NS::Vector3 lag = NS::HorizontalPart(m_reboundAnchor - head);
            const float lagLength = lag.Length();
            const float maxLag = std::max(m_reboundLagCap, 0.0f);
            if (lagLength > maxLag)
            {
                const float scale = maxLag / lagLength;
                m_reboundAnchor.x = head.x + lag.x * scale;
                m_reboundAnchor.z = head.z + lag.z * scale;
            }
            // 上限は欄の値より上にある間だけ、今の遅れまで縮めていく。増やさないので注視点は跳ばない
            m_reboundLagCap = std::max(m_reboundMaxLag, std::min(m_reboundLagCap, std::min(lagLength, maxLag)));
            // カメラへ向かう間は、注視点が自機より奥へ遅れた分だけカメラが自機へ寄る。奥への遅れだけを別の上限で縛る。
            // 横と、カメラから離れる向きの遅れは残すので、離れていく速さの見え方は変えない
            const NS::Vector3 away = ViewAxesFromYawPitch(m_yaw, 0.0f).forward;
            const float lagAway = NS::DotHorizontal(m_reboundAnchor - head, away);
            const float awayCap = std::max(m_reboundTowardCameraMaxLag, 0.0f);
            if (lagAway > awayCap)
            {
                m_reboundAnchor -= NS::HorizontalPart(away) * (lagAway - awayCap);
            }
            m_look = m_reboundAnchor + m_partnerLean;

            // 追う相手が画面の上下の帯を越えそうな時だけ、
            // 越えない所まで位置と注視点をカメラの上の向きへ動かす
            // 動かした量は留めた注視点へ戻さない。帯の内へ戻れば留めた高さへ戻る
            // 帯の中心は縦の向きの逆へずらす。上へ返る間は自機を下寄りに留め、進む先の上を空ける
            const float viewFov = ViewFovRadians();
            if (m_reboundScreenBand > 0.0f && viewFov > 0.0f)
            {
                const ViewAxes axes = ViewAxesFromYawPitch(m_yaw, m_pitch);
                const NS::Vector3 toBall = ball - (m_look - axes.forward * ShownDistance());
                const float depth = NS::Dot(toBall, axes.forward);
                if (depth > 0.0f)
                {
                    const float halfHeight = depth * std::tan(viewFov * 0.5f);
                    const float center = -m_reboundBandLean * verticalLean * halfHeight;
                    const float limit = m_reboundScreenBand * halfHeight;
                    const float height = NS::Dot(toBall, axes.up) - center;
                    if (height > limit)
                    {
                        m_look += axes.up * (height - limit);
                    }
                    else if (height < -limit)
                    {
                        m_look += axes.up * (height + limit);
                    }
                }
            }
            return m_look;
        }

        void ThirdPersonFollow::ResetPose() noexcept
        {
            // プレイ開始 / rebuild ごとに初期姿勢へ戻す。editor で置いた向きからプレイを始め、手動回転はここから積む
            m_yaw = m_initialYaw;
            m_pitch = m_initialPitch;
            m_distance = m_idleDistance;
            m_widenNow = 0.0f;
            m_hasOccludedDistance = false;
            ClearRebound();
        }

        float ThirdPersonFollow::ViewFovRadians() const noexcept
        {
            return FovY().value + NS::DegreesToRadians(m_widenNow);
        }

        float ThirdPersonFollow::ShownDistance() const noexcept
        {
            if (m_hasOccludedDistance)
            {
                return std::min(m_distance, m_occludedDistance);
            }
            return m_distance;
        }

        void ThirdPersonFollow::UpdateOcclusion(const NS::IGetCollision* collision,
                                                const NS::Vector3& root,
                                                const NS::Vector3& head,
                                                const NS::Vector3& look,
                                                float dt)
        {
            NS_ASSERT(Game, collision != nullptr, "UpdateOcclusion: 渡したアクターか部品が無い");
            const float margin = std::max(m_occlusionMargin, 0.0f);
            // 頭の高さは体より上にあり、天井の下では地形の中へ入る。中から撃つと 0 で当たって潰れるので、
            // 掃引で地形の外にある根から頭へ撃ち、遮られたらその手前から撃つ
            NS::Vector3 origin = head;
            const NS::Vector3 rise = head - root;
            const float riseLength = rise.Length();
            float riseHit = 0.0f;
            if (NS::HasPositiveMagnitude(riseLength) && NS::RaycastCollision(collision, root, rise * (1.0f / riseLength), riseLength, riseHit))
            {
                origin = root + rise * (std::max(riseHit - margin, 0.0f) / riseLength);
            }
            // 出発点から、注視点の後ろの置きたいカメラの位置へ撃つ。前の向きの逆へ撃つと、注視点が頭より
            // 下へずれた時に線がカメラより上を通り、カメラと自機の間に無い天井で縮む
            const ViewAxes axes = ViewAxesFromYawPitch(m_yaw, m_pitch);
            const NS::Vector3 cameraGoal = look - axes.forward * m_distance;
            const auto hitsTerrain = [&](const NS::Vector3& from, float& outDistance)
            {
                const NS::Vector3 toGoal = cameraGoal - from;
                const float length = toGoal.Length();
                return NS::HasPositiveMagnitude(length) && NS::RaycastCollision(collision, from, toGoal * (1.0f / length), length, outDistance);
            };
            const float probe = (cameraGoal - origin).Length();
            float hitDistance = 0.0f;
            float bodyHitDistance = 0.0f;
            // 頭の高さが地形の側面のすぐ横にあると、頭からの線は距離 0 で当たる。体からカメラが見えていれば、
            // その地形はカメラと自機の間に無いので縮めない
            if (hitsTerrain(origin, hitDistance) && hitsTerrain(root, bodyHitDistance))
            {
                // 当たった所までの割合だけ距離を縮める。出発点と注視点は近いので、カメラは当たった所の手前に来る
                const float goal = std::max(m_distance * (hitDistance / probe) - margin, 0.0f);
                // 遮られた時は一度に縮めて壁の中を 1 フレームも見せない。遮りが遠のく時はなめらかに伸ばす
                if (!m_hasOccludedDistance || goal < m_occludedDistance)
                {
                    m_occludedDistance = goal;
                    m_hasOccludedDistance = true;
                    return;
                }
                m_occludedDistance = SpringApproach(m_occludedDistance, goal, m_springOmega, dt);
                return;
            }
            if (!m_hasOccludedDistance)
            {
                return;
            }
            // 外れた後は距離バネと同じ速さで追う距離へ戻し、着いたら上限を外す。外さないと、
            // 追う距離が伸びる時に上限のバネが遅れて重なる
            m_occludedDistance = SpringApproach(m_occludedDistance, m_distance, m_springOmega, dt);
            if (m_distance - m_occludedDistance < NS::k_Epsilon)
            {
                m_hasOccludedDistance = false;
            }
        }

        void ThirdPersonFollow::SetSensX(float radPerPixel) noexcept { m_sensX = radPerPixel; }
        void ThirdPersonFollow::SetSensY(float radPerPixel) noexcept { m_sensY = radPerPixel; }
        void ThirdPersonFollow::SetInvertX(bool invert) noexcept { m_invertX = invert; }
        void ThirdPersonFollow::SetInvertY(bool invert) noexcept { m_invertY = invert; }

        void ThirdPersonFollow::SetInitialPoseFromCameraPosition(const NS::Vector3& cameraPosition) noexcept
        {
            const NS::Transform* target = Target();
            if (target == nullptr)
            {
                return;
            }

            const NS::Vector3 tgtPos = target->Position();
            const NS::Vector3 headPos{tgtPos.x, tgtPos.y + m_headHeight, tgtPos.z};
            const NS::Vector3 toHead = headPos - cameraPosition; // = forward * distance
            const float distance = toHead.Length();
            if (distance < 1e-3f)
            {
                return;
            }
            const NS::Vector3 forward = toHead * (1.0f / distance);

            // EvaluatePose の forward = (sin(yaw)cos(pitch), sin(pitch), cos(yaw)cos(pitch)) を解く
            // pitch は仰角の可動域に収める。clamp した分だけギズモ位置と厳密には一致しないが範囲外へは向けない
            const float pitch = NS::Clamp(std::asin(NS::Clamp(forward.y, -1.0f, 1.0f)), m_pitchMin, m_pitchMax);
            const float yaw = std::atan2(forward.x, forward.z);

            m_initialYaw = yaw;
            m_initialPitch = pitch;
            m_idleDistance = distance;

            // 編集中は Update が走らないので現在値も直接書き、EvaluatePose 表示をその場で追従させる
            m_yaw = yaw;
            m_pitch = pitch;
            m_distance = distance;
        }

        void ThirdPersonFollow::Advance(const NS::IGetCollision* collision)
        {
            NS_ASSERT(Game, collision != nullptr, "ThirdPersonFollow::Advance: 地形を探すシーンが null");
            // clang-format off
            if (m_targetState != nullptr) { m_targetState->WriteFollowState(this); }
            // clang-format on
            // 受けた反動の状態はこのフレームだけ使う。渡されなかったフレームは反動でないのと同じ
            const FollowReboundDesc rebound = m_rebound;
            m_rebound = FollowReboundDesc{};
            const bool framingHeld = m_framingHeld;
            m_framingHeld = false;

            const float dt = NS::OS::FrameTimer::FixedDelta();
            const NS::Transform* target = Target();
            if (target == nullptr || dt <= 0.0f)
            {
                // 休止の間は注視点を控えないので、反動の間の追い方も捨てる
                // 再開した時に古い注視点から留めない
                ClearRebound();
                return;
            }

            // 反動の状態になったフレーム
            // 空中で続けて当てた時も、間に反動でないフレームを挟むのでここを通る
            const bool reboundBegan = rebound.rebounding && !m_wasRebounding;
            m_wasRebounding = rebound.rebounding;
            UpdateReboundTurn(reboundBegan, rebound);

            // マウスと右スティックの手動回転。反動になってから接地するまでは受けない
            if (!m_reboundLookHeld)
            {
                NS::OS::Input& input = NS::OS::Input::Get();
                const NS::OS::Mouse& mouse = input.Mouse();
                float mxSign = 1.0f;
                if (m_invertX)
                {
                    mxSign = -1.0f;
                }
                float mySign = 1.0f;
                if (m_invertY)
                {
                    mySign = -1.0f;
                }
                m_yaw += static_cast<float>(mouse.GetDeltaX()) * m_sensX * mxSign;
                m_pitch += static_cast<float>(mouse.GetDeltaY()) * m_sensY * mySign;

                const NS::OS::Gamepad& pad = input.Gamepad();
                const NS::OS::Stick rstick = pad.RightStick();
                m_yaw += rstick.x * m_stickSensX * dt * mxSign;
                m_pitch += rstick.y * m_stickSensY * dt * mySign;
            }

            m_pitch = NS::Clamp(m_pitch, m_pitchMin, m_pitchMax);

            const NS::Vector3 root = target->Position();
            const NS::Vector3 head{root.x, root.y + m_targetHeightOffset + m_headHeight, root.z};
            UpdateReboundPhase(reboundBegan, rebound.rebounding, head);
            // 反動になったフレームは前の反動の寄せと引きを持ち越さない
            if (reboundBegan)
            {
                m_partnerLean = NS::Vector3{0.0f, 0.0f, 0.0f};
                m_partnerLeanVelocity = NS::Vector3{0.0f, 0.0f, 0.0f};
                m_partnerPull = 0.0f;
                m_partnerPullVelocity = 0.0f;
            }
            UpdateReboundPartner(rebound, head, dt);

            // 反動の引きは現在の距離から測る。補間中の目標から測ると、見えている距離からずれる。
            float desiredDistance = 0.0f;
            if (reboundBegan)
            {
                m_reboundBaseDistance = m_distance;
                if (rebound.pullBack)
                {
                    m_reboundBaseDistance += std::max(m_reboundPullBack, 0.0f);
                }
                desiredDistance = m_reboundBaseDistance + m_partnerPull;
            }
            else if (m_reboundPhase == ReboundPhase::Following)
            {
                // 反動の間は当たった瞬間に決めた距離に、相手を収める引きだけを足す
                desiredDistance = m_reboundBaseDistance + m_partnerPull;
            }
            else if (framingHeld)
            {
                // 体当たりから止めの明けまでは今の距離のまま。突進で浮いても空中の距離へ引かない
                desiredDistance = m_distance;
            }
            else if (m_hasFollowMotion && !m_followGrounded)
            {
                desiredDistance = m_jumpDistance;
            }
            else if (m_hasFollowMotion && m_followHorizontalSpeed > m_runSpeedThreshold)
            {
                desiredDistance = m_runDistance;
            }
            else
            {
                desiredDistance = m_idleDistance;
            }
            m_distance = SpringApproach(m_distance, desiredDistance, m_springOmega, dt);
            float widenGoal = 0.0f;
            if (rebound.widenView)
            {
                widenGoal = m_widenDegrees;
            }
            m_widenNow = SpringApproach(m_widenNow, widenGoal, m_widenOmega, dt);

            const NS::Vector3 look = UpdateReboundLook(head, root, rebound.verticalLean, dt);
            UpdateOcclusion(collision, root, head, look, dt);
        }

        NS::CameraPose ThirdPersonFollow::EvaluatePose(float alpha) const noexcept
        {
            const NS::Transform* target = Target();
            if (target == nullptr)
            {
                return MakePose(
                    NS::Vector3{0.0f, 0.0f, -5.0f}, NS::Vector3{0.0f, 0.0f, 0.0f}, NS::Vector3{0.0f, 1.0f, 0.0f});
            }

            const ViewAxes axes = ViewAxesFromYawPitch(m_yaw, m_pitch);

            // PlayerActor Mesh の補間と整合させ、相対位置のガタつきを防ぐ
            const NS::Vector3 tgtPos = target->InterpolatedWorldMatrix(alpha).Translation();
            // ずれは補間しない。根を ShiftPosition で上げ下げしていれば、補間の途中でも 根 + ずれ は動かない
            NS::Vector3 headPos{tgtPos.x, tgtPos.y + m_targetHeightOffset + m_headHeight, tgtPos.z};
            // 固定更新で決めた注視点を描画時に補間する。
            if (m_reboundPhase != ReboundPhase::None)
            {
                headPos = m_previousLook * (1.0f - alpha) + m_look * alpha;
            }
            const NS::Vector3 camPos = headPos - axes.forward * ShownDistance();

            NS::CameraPose pose = MakePose(camPos, headPos, NS::Vector3{0.0f, 1.0f, 0.0f});
            if (m_widenNow != 0.0f)
            {
                pose.fovY = NS::Radians{ViewFovRadians()};
            }
            return pose;
        }
    } // namespace Level
} // namespace GL
