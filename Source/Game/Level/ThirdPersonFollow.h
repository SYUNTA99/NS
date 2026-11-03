#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/VirtualCamera.h"
#include "NSlib/CoreObject/Reflection/ActorRef.h"

#include <optional>

namespace NS
{
    class IGetCollision;
    class SceneActor;
    class Transform;
} // namespace NS

namespace GL
{
    namespace Level
    {
        class IGetCameraTarget;

        //! 追従カメラが 1 フレームぶん受ける反動の状態
        //! 突進の向きは反動になったフレームにだけ読む
        struct FollowReboundDesc
        {
            bool rebounding = false; // 追う相手が反動の状態か
            // 反動を起こした突進を出したフレームの向き。世界座標で、縦の成分は使わない
            // 水平の長さが 0 なら回さない
            NS::Vector3 slamDirection{};
            // 反動の間に画面へ残したい相手の今の位置。世界座標。空なら追う相手だけを見る
            std::optional<NS::Vector3> partnerPosition{};
            // 反動になったフレームに、欄「反動の間に下げる距離」だけ後ろへ下げるか。反動になったフレームにだけ読む
            bool pullBack = true;
            // 反動になったフレームから接地まで、回す入力を止めるか。反動になったフレームにだけ読む
            bool holdLook = true;
            // 反動の向きの縦の成分 (−1〜1)。正なら追う相手を画面の下寄りに、負なら上寄りに留め、進む側に空きを作る
            float verticalLean = 0.0f;
            // 速く動いている間か。真の間は視野角を欄「速い間に広げる視野角」へ寄せる
            bool widenView = false;
        };

        //! 反動に応じて構図を調整する三人称の追従カメラ
        //! 実カメラは変更せず、CameraManager が採用する姿勢を返す
        class ThirdPersonFollow : public NS::VirtualCamera
        {
        public:
            ThirdPersonFollow() noexcept;

            //! 追う相手の根。相手を受けていなければ nullptr
            [[nodiscard]] const NS::Transform* Target() const noexcept;
            //! 追う相手。相手を受けていなければ nullptr
            [[nodiscard]] const NS::SceneActor* TargetActor() const noexcept { return m_target; }

            //! 追従対象の永続参照。データ経由の構築がリフレクション set で書く
            [[nodiscard]] NS::ActorRef TargetRef() const noexcept { return m_targetRef; }

            //! 追う相手を受ける。持ち主のアクターが TargetRef の指す相手を取得して渡す
            //! 相手が一覧から外れる前に、持ち主が取得し直して渡し直す。target が nullptr なら相手なし。
            //! state は同じ相手が自分の状態を渡す口で、渡さない相手は nullptr。Advance が進める直前に読む
            void SetTarget(const NS::SceneActor* target, const IGetCameraTarget* state) noexcept
            {
                m_target = target;
                m_targetState = state;
            }

            //! プレイ開始 / rebuild ごとに初期姿勢へ戻す
            void ResetPose() noexcept;

            //! 自動ズームの判定に使う接地と速度を値で受け取る。移動部品の型を include せずに済む
            void SetFollowMotion(bool grounded, const NS::Vector3& velocity) noexcept;
            //! 根を上下させても見る高さを保つためのずれ。注視点は根 + ずれ + 頭の高さ
            //! ずれは補間しないため、根の移動は Transform::ShiftPosition で前後の値を揃える
            void SetTargetHeightOffset(float offset) noexcept;

            //! 距離を次の更新で固定する
            //! ヒットストップ中に画面が引くのを防ぐ。更新ごとに渡し、未指定なら固定しない
            void SetFollowFramingHeld(bool held) noexcept { m_framingHeld = held; }

            //! 次の更新で使う反動の状態を受け取る
            //! 未指定なら反動なし。反動開始時に突進方向へ回し、接地まで手動回転を止める
            //! 接地が未入力なら反動終了時に回転を許可する。突進方向の非有限値と、−1〜1 の外か非数の縦の向きは拒否する
            //! @return 受け取った場合 true、壊れた値で何も変えなかった場合は false
            bool SetFollowRebound(const FollowReboundDesc& desc) noexcept;

            //! 反動の追従・回転と入力制限を解除する。現在の向きは保つ
            void ClearRebound() noexcept;

            void SetSensX(float radPerPixel) noexcept;
            [[nodiscard]] float SensX() const noexcept { return m_sensX; }
            void SetSensY(float radPerPixel) noexcept;
            [[nodiscard]] float SensY() const noexcept { return m_sensY; }
            void SetInvertX(bool invert) noexcept;
            [[nodiscard]] bool IsInvertX() const noexcept { return m_invertX; }
            void SetInvertY(bool invert) noexcept;
            [[nodiscard]] bool IsInvertY() const noexcept { return m_invertY; }

            [[nodiscard]] float Distance() const noexcept { return m_distance; }

            [[nodiscard]] float Yaw() const noexcept { return m_yaw; }
            [[nodiscard]] float Pitch() const noexcept { return m_pitch; }

            //! プレイ開始時の向き。ResetPose で現在方位角・仰角へ写し、以降はプレイ中の手動回転で動く
            [[nodiscard]] float InitialYaw() const noexcept { return m_initialYaw; }
            [[nodiscard]] float InitialPitch() const noexcept { return m_initialPitch; }

            //! 指定位置から初期姿勢を逆算し、現在姿勢にも反映する。対象なし・距離ゼロなら変更しない
            void SetInitialPoseFromCameraPosition(const NS::Vector3& cameraPosition) noexcept;

            //! 追う相手の状態を受けてから、固定ステップで向きと距離を更新する
            //! EvaluatePose が補間した姿勢を返し、描画のガタつきを避ける
            //! @param[in] collision 注視点とカメラの間の地形を探すシーン
            void Advance(const NS::IGetCollision* collision) override;

            //! alpha で補間した target を追う最終姿勢を返す。CameraManager が選択時に実カメラへ書く
            [[nodiscard]] NS::CameraPose EvaluatePose(float alpha) const noexcept override;

            NS_REFLECT_BEGIN(ThirdPersonFollow, NS::VirtualCamera)
            NS_FIELD(m_targetRef, "追従対象")
            NS_FIELD(m_initialYaw, "初期ヨー")
            NS_FIELD(m_initialPitch, "初期ピッチ")
            NS_FIELD(m_springOmega, "バネ角速度")
            NS_FIELD(m_idleDistance, "待機時距離")
            NS_FIELD(m_runDistance, "走行時距離")
            NS_FIELD(m_jumpDistance, "ジャンプ時距離")
            NS_FIELD(m_runSpeedThreshold, "走り判定速度")
            NS_FIELD(m_headHeight, "頭の高さ")
            NS_FIELD(m_sensX, "感度 X")
            NS_FIELD(m_sensY, "感度 Y")
            NS_FIELD(m_stickSensX, "スティック感度 X")
            NS_FIELD(m_stickSensY, "スティック感度 Y")
            NS_FIELD(m_invertX, "反転 X")
            NS_FIELD(m_invertY, "反転 Y")
            NS_FIELD(m_pitchMin, "ピッチ下限")
            NS_FIELD(m_pitchMax, "ピッチ上限")
            NS_FIELD(m_reboundFollowOmega, "反動の間の横と前後のバネ角速度")
            NS_FIELD(m_reboundMaxLag, "反動の間の横と前後の遅れの上限")
            NS_FIELD(m_reboundTowardCameraMaxLag, "カメラへ向かう遅れの上限")
            NS_FIELD(m_reboundScreenBand, "反動の間の上下の帯")
            NS_FIELD(m_reboundBandLean, "反動の縦の向きで帯をずらす割合")
            NS_FIELD(m_widenDegrees, "速い間に広げる視野角")
            NS_FIELD(m_widenOmega, "視野角を寄せるバネ角速度")
            NS_FIELD(m_occlusionMargin, "遮った地形から離す距離")
            NS_FIELD(m_reboundReturnFrames, "反動の後に戻すフレーム数")
            NS_FIELD(m_reboundTurnFrames, "反動の向きへ回すフレーム数")
            NS_FIELD(m_reboundPullBack, "反動の間に下げる距離")
            NS_FIELD(m_reboundPartnerWeight, "反動の間に相手を収める重み")
            NS_FIELD(m_reboundPartnerReleaseDistance, "相手を見送る距離")
            NS_FIELD(m_reboundPullStartDistance, "引き始めの二人の距離")
            NS_FIELD(m_reboundPullPerMeter, "二人の距離 1 m あたりに引く距離")
            NS_FIELD(m_reboundPullMax, "相手を収める引きの上限")
            NS_FIELD(m_reboundPartnerHalfSeconds, "相手を収めるばねの半分の秒")
            NS_FIELD(m_floorViewOrbitMax, "足元を映す回り込みの上限")
            NS_FIELD(m_floorViewBottom, "足元を映す画面の位置")
            NS_FIELD(m_floorViewOmega, "足元を映すバネ角速度")
            NS_FIELD(m_floorViewProbe, "足元を探す距離")
            NS_FIELD(m_floorViewStartSpeed, "足元を映し始める縦の速さ")
            NS_FIELD(m_floorViewReturnOmega, "足元から戻すバネ角速度")
            NS_ACCESSOR(float, "ファークリップ", FarPlane(), SetFarPlane)
            NS_ACCESSOR(int, "優先度", VcamPriority(), SetVcamPriority)
            NS_REFLECT_END()

        private:
            // 距離と注視点を決める前に、反動の状態を更新する
            void UpdateReboundPhase(bool began, bool rebounding, const NS::Vector3& head) noexcept;

            // 手動入力を適用する前に、反動の回転と入力制限を更新する
            void UpdateReboundTurn(bool began, const FollowReboundDesc& rebound) noexcept;
            // 反動の間に受けた相手の位置から、注視点の寄せと引きのばねを 1 フレーム進める。反動の間でなければ 0 へ戻す
            void UpdateReboundPartner(const FollowReboundDesc& rebound, const NS::Vector3& head, float dt) noexcept;

            // ball を上下の帯に保つ注視点。通常時は head を返す。verticalLean で帯の中心を上下へずらす
            [[nodiscard]] NS::Vector3 UpdateReboundLook(const NS::Vector3& head,
                                                        const NS::Vector3& ball,
                                                        float verticalLean,
                                                        float dt) noexcept;

            // 追う相手の頭から置きたいカメラの位置への光線で地形を探し、見せる距離の上限を 1 フレーム進める。
            // 注視点から撃たないのは、反動の間の注視点が遅れと帯のずらしで地形の中へ入る事があるため。
            // root は地形の外にある点 (追う相手の体の中心) で、頭が天井に入る時はその間で遮られた手前から撃つ
            void UpdateOcclusion(const NS::IGetCollision* collision,
                                 const NS::Vector3& root,
                                 const NS::Vector3& head,
                                 const NS::Vector3& look,
                                 float dt);

            // 反動の後に落ちる間、真下の床を画面の下端の内側へ入れる角度を 1 フレーム進める。
            // 反動になってから接地するまでの間で、本人がカメラを縦に動かしていない時だけ角度を足す
            void UpdateFloorView(const NS::IGetCollision* collision,
                                 const NS::Vector3& root,
                                 const NS::Vector3& look,
                                 bool began,
                                 bool rebounding,
                                 bool pitchMoved,
                                 float dt);

            // カメラの位置を作る向き。本人が決めた向きから回り込みの分を下げる。遮りと帯もこの向きで調べる
            [[nodiscard]] float FollowPositionPitch() const noexcept;

            // 描く視野角。速い間の広げを足す。帯と描く時が同じ値を読む
            [[nodiscard]] float ViewFovRadians() const noexcept;
            // 描く距離。追う距離を、遮りで縮めた上限で切る
            [[nodiscard]] float ShownDistance() const noexcept;

            // 反動の間の追い方の段
            enum class ReboundPhase
            {
                None,      // 普通の追い方
                Following, // 反動の状態の間
                Returning, // 反動の状態が外れてから普通の追い方へ寄せ戻している
            };

            NS::ActorRef m_targetRef{};                          // 追従対象の永続参照。シーンに保存する
            const NS::SceneActor* m_target = nullptr;            // m_targetRef から持ち主が取得した相手 (非所有)
            const IGetCameraTarget* m_targetState = nullptr; // m_target が状態を渡す口。渡さない相手は nullptr (非所有)

            // 値で受けた追従先の運動。届くまでは待機距離のまま
            bool m_followGrounded = false;
            float m_followHorizontalSpeed = 0.0f; // 速度の向きは使わないので水平の大きさへ畳んで持つ
            float m_followVerticalSpeed = 0.0f;   // 負なら落ちている
            bool m_hasFollowMotion = false;
            bool m_framingHeld = false;        // このフレームに距離を保つか。Advance が読んで偽へ戻す
            float m_targetHeightOffset = 0.0f; // 追う相手の根から注視の高さを測り始める所までの縦のずれ

            float m_yaw = 0.0f;       // 水平回転角
            float m_pitch = -0.2618f; // 仰俯角

            // プレイ開始時の初期姿勢。editor のギズモ / Inspector が書き、ResetPose で m_yaw/m_pitch へ写す
            float m_initialYaw = 0.0f;
            float m_initialPitch = -0.2618f;

            float m_distance = 6.0f;
            float m_springOmega = 6.0f; // 距離バネの追従の速さ

            float m_idleDistance = 5.0f;      // 静止時の距離
            float m_runDistance = 6.0f;       // 走行時の距離
            float m_jumpDistance = 7.0f;      // 空中時の距離
            float m_runSpeedThreshold = 4.0f; // run 距離へ切替える水平速度

            float m_headHeight = 1.2f; // 注視点を頭へ上げる高さ

            float m_sensX = 0.0030f;   // マウス水平感度
            float m_sensY = 0.0030f;   // マウス垂直感度
            float m_stickSensX = 2.0f; // スティック水平感度
            float m_stickSensY = 1.5f; // スティック垂直感度
            bool m_invertX = false;
            bool m_invertY = false;

            float m_pitchMin = -1.396f;  // 仰俯角の下限
            float m_pitchMax = -0.0873f; // 仰俯角の上限

            float m_reboundFollowOmega = 4.0f; // 反動の間に注視点の横と前後が寄るバネ角速度 (1/秒)
            float m_reboundMaxLag = 1.5f;      // 反動の間に注視点が横と前後へ遅れてよい上限 (m)
            // 反動でカメラへ向かう間に、注視点が自機より奥へ遅れてよい上限 (m)。遅れの分だけカメラが自機へ寄り、自機が画面を塞ぐ
            float m_reboundTowardCameraMaxLag = 0.5f;
            float m_reboundScreenBand = 0.5f;  // 反動の間の上下の帯。視野の半分への割合
            // 縦の向き 1 で帯の中心を下げる量。視野の半分への割合。上へ返る時に自機を下寄りに置き、上に足場の入る空きを作る
            float m_reboundBandLean = 0.3f;
            float m_widenDegrees = 8.0f; // 速い間に足す視野角 (度)。速さを画面の縁の流れで見せる
            float m_widenOmega = 8.0f;   // 足す視野角を寄せる・戻すバネ角速度 (1/秒)
            float m_occlusionMargin = 0.3f; // 遮った地形の面からカメラを手前へ離す距離 (m)。近面で壁を切らない
            int m_reboundReturnFrames = 20;    // 反動が外れてから普通の追い方へ寄せ戻すフレーム数
            int m_reboundTurnFrames = 20;      // 反動になってから突進の向きへ回し終えるフレーム数
            float m_reboundPullBack = 1.0f;    // 反動の間に当たった瞬間の距離より伸ばす距離 (m)
            // 自機を中心に相手も画面に残し、離れるにつれて相手への重みを減らす
            float m_reboundPartnerWeight = 0.3f;
            // 相手がこれより遠くへ飛ぶと重みが 0 になり、自機だけを追う (m)。出発点で、値は撮って詰める
            float m_reboundPartnerReleaseDistance = 30.0f;
            // 二人がこれより離れた分に、1 m あたりの距離を掛けて引く。引きは上限で止まる (m)
            float m_reboundPullStartDistance = 3.0f;
            float m_reboundPullPerMeter = 0.3f;
            float m_reboundPullMax = 4.0f;
            // 寄せと引きのばねが差の半分まで追いつく秒
            float m_reboundPartnerHalfSeconds = 0.15f;
            float m_floorViewOrbitMax = 10.0f; // 足元を映す時にカメラの位置を回す角度の上限 (度)
            float m_floorViewBottom = 0.9f;    // 真下の床を入れる位置。画面の中央が 0、下端が 1
            float m_floorViewOmega = 5.0f;     // 足す角度を増やす向きのバネ角速度 (1/秒)
            float m_floorViewReturnOmega = 8.0f; // 足した角度を 0 へ戻す向きのバネ角速度 (1/秒)
            float m_floorViewProbe = 60.0f;    // 真下の床を探す光線の長さ (m)
            float m_floorViewStartSpeed = 8.0f; // 縦の速さがこれを下回ると足元を映し始める (m/秒)。上昇の勢いが弱まった所から回す

            FollowReboundDesc m_rebound{};    // 次の Update で使う反動の状態
            bool m_wasRebounding = false;     // 前のフレームに反動の状態だったか
            bool m_reboundLookHeld = false;   // 反動になってから接地するまで、回す入力を受けないか
            float m_reboundTurnAngle = 0.0f;  // 反動になったフレームに決めた、回す角度 (ラジアン)
            int m_reboundTurnFrame = 0;       // 回しの何フレーム目か。欄のフレーム数で回し終える
            ReboundPhase m_reboundPhase = ReboundPhase::None;
            NS::Vector3 m_reboundAnchor{};         // 横と前後を遅らせて追う注視点。高さは留める
            NS::Vector3 m_reboundAnchorVelocity{}; // 注視点の横と前後の速さ (m/秒)。縦は使わない
            float m_reboundLagCap = 0.0f;          // 反動の間の遅れの今の上限 (m)。欄の上限より下にはしない
            NS::Vector3 m_reboundReturnOffset{};   // 寄せ戻し始めの、注視点 − 追う相手の頭 (m)
            int m_reboundReturnFrame = 0;          // 寄せ戻しの何フレーム目か
            float m_reboundBaseDistance = 0.0f;    // 反動になったフレームに決めた、相手を収める引きの前の距離 (m)
            NS::Vector3 m_partnerLean{};           // 注視点を相手へ寄せている量 (m)
            NS::Vector3 m_partnerLeanVelocity{};   // 寄せの速さ (m/秒)
            float m_partnerPull = 0.0f;            // 相手を収めるために引いている距離 (m)
            float m_partnerPullVelocity = 0.0f;    // 引きの速さ (m/秒)
            float m_widenNow = 0.0f;           // 今足している視野角 (度)
            // 遮りで縮めた距離の上限 (m)。遮られた時は面の手前まで一度に縮め、外れたら距離バネと同じ速さで戻す
            float m_occludedDistance = 0.0f;
            bool m_hasOccludedDistance = false; // m_occludedDistance を前のフレームから持っているか
            NS::Vector3 m_look{};                  // このフレームの注視点。反動と寄せ戻しで使う
            NS::Vector3 m_previousLook{};          // 前のフレームの注視点。描画の補間に使う
            bool m_hasLook = false;                // m_look が前のフレームの注視点か。休止とプレイ開始の後は偽
            bool m_floorViewActive = false;        // 反動になってから接地するまでか
            bool m_floorViewStickUsed = false;     // その間に本人がカメラを縦に動かしたか。真なら接地まで角度を足さない
            float m_floorOrbit = 0.0f;             // カメラの位置を回している角度 (ラジアン)
            float m_floorTilt = 0.0f;              // 描く向きだけをさらに下げている角度 (ラジアン)
            float m_floorOrbitVelocity = 0.0f;     // m_floorOrbit の速さ (ラジアン/秒)
            float m_floorTiltVelocity = 0.0f;      // m_floorTilt の速さ (ラジアン/秒)
            float m_floorOrbitGoal = 0.0f;         // m_floorOrbit の目標。床が見つからない間は前の値を保つ
            float m_floorTiltGoal = 0.0f;          // m_floorTilt の目標。床が見つからない間は前の値を保つ
        };

    } // namespace Level
} // namespace GL
