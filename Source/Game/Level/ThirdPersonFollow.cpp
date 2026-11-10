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
            if (std::isfinite(velocity.y))
            {
                m_followVerticalSpeed = velocity.y;
            }
            else
            {
                m_followVerticalSpeed = 0.0f;
            }
            m_hasFollowMotion = true;
        }

        void ThirdPersonFollow::SetTargetHeightOffset(float offset) noexcept { m_targetHeightOffset = offset; }

        bool ThirdPersonFollow::SetFollowRebound(const FollowReboundDesc& desc) noexcept
        {
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
            m_reboundPhase = ReboundPhase::None;
            m_reboundAnchor = NS::Vector3{0.0f, 0.0f, 0.0f};
            m_reboundAnchorVelocity = NS::Vector3{0.0f, 0.0f, 0.0f};
            m_reboundReturnOffset = NS::Vector3{0.0f, 0.0f, 0.0f};
            m_reboundReturnFrame = 0;
            m_hasLook = false;
            m_reboundBaseDistance = 0.0f;
            m_floorViewActive = false;
            m_floorViewStickUsed = false;
            m_floorOrbit = 0.0f;
            m_floorTilt = 0.0f;
            m_floorOrbitVelocity = 0.0f;
            m_floorTiltVelocity = 0.0f;
            m_floorOrbitGoal = 0.0f;
            m_floorTiltGoal = 0.0f;
        }

        void ThirdPersonFollow::UpdateReboundPhase(bool began, bool rebounding, const NS::Vector3& head) noexcept
        {
            // 反動の状態になったフレームに、前のフレームに見ていた所から留める
            // 反動の中からエアダッシュを出して地形に当て直した時も留め直す
            if (began)
            {
                m_reboundPhase = ReboundPhase::Following;
                m_reboundAnchor = head;
                if (m_hasLook)
                {
                    m_reboundAnchor = m_look.Current();
                }
                m_reboundAnchorVelocity = NS::Vector3{0.0f, 0.0f, 0.0f};
                // 入った時の遅れが反動の上限より大きくても、それを上限の初めにして、切って跳ばさない
                m_reboundLagCap = std::max(m_reboundMaxLag, NS::HorizontalLength(m_reboundAnchor - head));
                return;
            }

            // 反動の状態が外れたフレームから寄せ戻す
            // 着地と落下へ移った時のほか、反動の中からエアダッシュを出した時も外れる
            if (m_reboundPhase == ReboundPhase::Following && !rebounding)
            {
                if (m_reboundReturnFrames > 0 && m_hasLook)
                {
                    m_reboundPhase = ReboundPhase::Returning;
                    m_reboundReturnOffset = m_look.Current() - head;
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

        NS::Vector3 ThirdPersonFollow::UpdateReboundLook(const NS::Vector3& head,
                                                         const NS::Vector3& ball,
                                                         float verticalLean,
                                                         float dt) noexcept
        {
            m_look.Snapshot();
            m_hasLook = true;
            if (m_reboundPhase == ReboundPhase::None)
            {
                m_look.Set(head);
                return m_look.Current();
            }

            if (m_reboundPhase == ReboundPhase::Returning)
            {
                ++m_reboundReturnFrame;
                const float t = static_cast<float>(m_reboundReturnFrame) / static_cast<float>(m_reboundReturnFrames);
                if (t >= 1.0f)
                {
                    m_look.Set(head);
                }
                else
                {
                    m_look.Set(head + m_reboundReturnOffset * (1.0f - NS::SmoothStep(t)));
                }
                return m_look.Current();
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
            m_look.Set(m_reboundAnchor);

            // 追う相手が画面の上下の帯を越えそうな時だけ、
            // 越えない所まで位置と注視点をカメラの上の向きへ動かす
            // 動かした量は留めた注視点へ戻さない。帯の内へ戻れば留めた高さへ戻る
            // 帯の中心は縦の向きの逆へずらす。上へ返る間は自機を下寄りに留め、進む先の上を空ける
            const float viewFov = ViewFovRadians();
            if (m_reboundScreenBand > 0.0f && viewFov > 0.0f)
            {
                const ViewAxes axes = ViewAxesFromYawPitch(m_yaw, FollowPositionPitch());
                const NS::Vector3 toBall = ball - (m_look.Current() - axes.forward * ShownDistance());
                const float depth = NS::Dot(toBall, axes.forward);
                if (depth > 0.0f)
                {
                    const float halfHeight = depth * std::tan(viewFov * 0.5f);
                    const float center = -m_reboundBandLean * verticalLean * halfHeight;
                    const float limit = m_reboundScreenBand * halfHeight;
                    const float height = NS::Dot(toBall, axes.up) - center;
                    if (height > limit)
                    {
                        m_look.Set(m_look.Current() + axes.up * (height - limit));
                    }
                    else if (height < -limit)
                    {
                        m_look.Set(m_look.Current() + axes.up * (height + limit));
                    }
                }
            }
            return m_look.Current();
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

        float ThirdPersonFollow::FollowPositionPitch() const noexcept
        {
            return std::max(m_pitch - m_floorOrbit, m_pitchMin);
        }

        void ThirdPersonFollow::UpdateFloorView(const NS::IGetCollision* collision,
                                                const NS::Vector3& root,
                                                const NS::Vector3& look,
                                                bool began,
                                                bool rebounding,
                                                bool pitchMoved,
                                                float dt)
        {
            NS_ASSERT(Game, collision != nullptr, "UpdateFloorView: 渡したアクターか部品が無い");
            if (began)
            {
                m_floorViewActive = true;
                m_floorViewStickUsed = false;
            }
            else if (m_followGrounded && !rebounding)
            {
                m_floorViewActive = false;
            }
            if (m_floorViewActive && pitchMoved && !m_floorViewStickUsed)
            {
                m_floorViewStickUsed = true;
                // バネの勢いで、入力の後にも角度が増えるのを止める
                m_floorOrbitVelocity = 0.0f;
                m_floorTiltVelocity = 0.0f;
            }

            float omega = m_floorViewOmega;
            if (!m_floorViewActive || m_floorViewStickUsed || m_followVerticalSpeed >= m_floorViewStartSpeed)
            {
                m_floorOrbitGoal = 0.0f;
                m_floorTiltGoal = 0.0f;
                omega = m_floorViewReturnOmega;
            }
            else
            {
                float floorDistance = 0.0f;
                // 床が見つからない間は前の目標を保つ。足場の端を越えた瞬間にカメラが跳ねない
                if (NS::RaycastCollision(collision, root, NS::Vector3{0.0f, -1.0f, 0.0f}, m_floorViewProbe, floorDistance))
                {
                    const NS::Vector3 floorPoint{root.x, root.y - floorDistance, root.z};
                    const float limit = std::atan(m_floorViewBottom * std::tan(ViewFovRadians() * 0.5f));
                    // その向きのカメラから見た床の点を、許す角度の内へ入れるのに下げる角度
                    const auto angleNeeded = [&](float pitch)
                    {
                        const ViewAxes axes = ViewAxesFromYawPitch(m_yaw, pitch);
                        const NS::Vector3 toFloor = floorPoint - (look - axes.forward * ShownDistance());
                        return std::atan2(-NS::Dot(toFloor, axes.up), NS::Dot(toFloor, axes.forward)) - limit;
                    };
                    const float orbitMax = std::min(NS::DegreesToRadians(m_floorViewOrbitMax), m_pitch - m_pitchMin);
                    // 床が近づくと要る角度は減るが、目標を小さくしない。落下の途中で回転の向きが逆になるのを避ける
                    m_floorOrbitGoal = std::max(m_floorOrbitGoal, NS::Clamp(angleNeeded(m_pitch), 0.0f, std::max(orbitMax, 0.0f)));
                    const float positionPitch = m_pitch - m_floorOrbitGoal;
                    m_floorTiltGoal = std::max(m_floorTiltGoal,
                                               NS::Clamp(angleNeeded(positionPitch), 0.0f, std::max(positionPitch - m_pitchMin, 0.0f)));
                }
            }
            // 足し始めのフレームに目標が大きく跳ぶので、速さが 0 から上がるバネで寄せる
            CriticalSpringStep(m_floorOrbit, m_floorOrbitVelocity, m_floorOrbitGoal, omega, dt);
            CriticalSpringStep(m_floorTilt, m_floorTiltVelocity, m_floorTiltGoal, omega, dt);
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
            const ViewAxes axes = ViewAxesFromYawPitch(m_yaw, FollowPositionPitch());
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
            // 反動を立てるのは地形の反動 (DashRebound) だけ。反動の中からエアダッシュを出して当て直した時は、
            // 間に反動でないフレームを 1 つでも挟んだ時だけここを通り、挟まなければ前の反動の注視点と距離のまま追う
            const bool reboundBegan = rebound.rebounding && !m_wasRebounding;
            m_wasRebounding = rebound.rebounding;

            const float pitchBeforeInput = m_pitch;
            // マウスと右スティックの手動回転
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

            // 上限や下限へ入力した時はクランプでピッチが変わらないので、クランプの前に見る
            const bool pitchInput = m_pitch != pitchBeforeInput;
            m_pitch = NS::Clamp(m_pitch, m_pitchMin, m_pitchMax);

            const NS::Vector3 root = target->Position();
            const NS::Vector3 head{root.x, root.y + m_targetHeightOffset + m_headHeight, root.z};
            UpdateReboundPhase(reboundBegan, rebound.rebounding, head);

            // 反動の間の距離は現在の距離から取る。補間中の目標から取ると、見えている距離からずれる。
            // 反動になったフレームは UpdateReboundPhase が段を Following にしているので、下の分岐で同じ距離を使う
            if (reboundBegan)
            {
                m_reboundBaseDistance = m_distance;
            }
            float desiredDistance = 0.0f;
            if (m_reboundPhase == ReboundPhase::Following)
            {
                // 反動の間は反動になったフレームの距離のまま
                desiredDistance = m_reboundBaseDistance;
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
            UpdateFloorView(collision, root, look, reboundBegan, rebound.rebounding, pitchInput, dt);
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

            const float positionPitch = FollowPositionPitch();
            const ViewAxes axes = ViewAxesFromYawPitch(m_yaw, positionPitch);

            // PlayerActor Mesh の補間と整合させ、相対位置のガタつきを防ぐ
            const NS::Vector3 tgtPos = target->InterpolatedWorldMatrix(alpha).Translation();
            // ずれは補間しない。根を ShiftPosition で上げ下げしていれば、補間の途中でも 根 + ずれ は動かない
            NS::Vector3 headPos{tgtPos.x, tgtPos.y + m_targetHeightOffset + m_headHeight, tgtPos.z};
            // 固定更新で決めた注視点を描画時に補間する。
            if (m_reboundPhase != ReboundPhase::None)
            {
                headPos = m_look.Lerp(alpha);
            }
            const NS::Vector3 camPos = headPos - axes.forward * ShownDistance();

            NS::Vector3 lookAt = headPos;
            if (m_floorTilt != 0.0f)
            {
                const float drawnPitch = std::max(positionPitch - m_floorTilt, m_pitchMin);
                lookAt = camPos + ViewAxesFromYawPitch(m_yaw, drawnPitch).forward * ShownDistance();
            }

            NS::CameraPose pose = MakePose(camPos, lookAt, NS::Vector3{0.0f, 1.0f, 0.0f});
            if (m_widenNow != 0.0f)
            {
                pose.fovY = NS::Radians{ViewFovRadians()};
            }
            return pose;
        }
    } // namespace Level
} // namespace GL
