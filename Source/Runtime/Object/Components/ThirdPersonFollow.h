#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/VirtualCamera.h"
#include "Runtime/Object/Reflection/ActorRef.h"

namespace NS::Obj
{
    class Transform;
    class Actor;

    //! @brief 追従カメラが 1 フレームぶん受ける溜めの状態
    //! @details 溜め量は押している間だけ意味を持つ。押していない値は放したのと同じに扱う
    struct FollowChargeDesc
    {
        float charge01 = 0.0f;               // 押している間の溜め量 (0〜1)
        bool held = false;                   // 押しているか
        bool hasAimTarget = false;           // 狙う相手がいるか
        NS::Core::Vector3 aimTargetCenter{}; // 狙う相手の中心。世界座標
        float aimTargetRadius = 0.0f;        // 狙う相手の半径 (m)。構図は中心でなく中心 ± 半径を枠に入れる
    };

    //! @brief 追従カメラが 1 フレームぶん受ける反動の状態
    //! @details 突進の向きは反動になったフレームにだけ読む
    struct FollowReboundDesc
    {
        bool rebounding = false; // 追う相手が反動の状態か
        // 追う相手が溜めすぎで勝手に出た突進の最中か。真の間は注視点の横と前後が遅れて付いていく
        bool forcedSlamming = false;
        // 反動を起こした突進を出したフレームの向き。世界座標で、縦の成分は使わない
        // 水平の長さが 0 なら回さない
        NS::Core::Vector3 slamDirection{};
        // 反動の間に画面へ残したい相手の今の位置。世界座標。空なら追う相手だけを見る
        std::optional<NS::Core::Vector3> partnerPosition{};
    };

    //! @brief Mario 系ジャンプアクションの追従カメラ
    //! @details 実カメラは持たず、追従姿勢を pose として返す。CameraManager が実カメラへ書く
    //! distance は臨界減衰バネでなめらかに寄せ、マウス / 右スティックで手動回転できる
    //! 感度・反転と idle / run / jump 3 段の自動ズーム距離は setter で調整できる
    //! FOV は基底 VirtualCamera が持つ
    //! 受けた溜めから視野角の締め・縦の揺れ・構図のずらしを作って姿勢に足す
    //! 構図のずらしは追う相手と狙う相手を枠に収めるよう、位置と注視点を同じだけ動かす
    //! 反動の状態の間は、注視点の高さを反動の始まりに留め、横と前後は遅れて付いていく
    //! 反動の間に相手の位置を受けた時は、注視点を相手へ重みで寄せ、二人が離れた分だけ後ろへ引く。
    //! 重みは相手が見送りの距離へ近づくほど 0 へ落ち、見送った後は追う相手だけを見る
    //! 勝手に出た突進の間は、注視点の横と前後がその場に取り残されてから引っ張られるように追い付く
    //! 追う相手が画面の上下の帯を越えそうな時だけ追い、
    //! 反動の状態が外れたら普通の追い方へ寄せ戻す
    //! 反動になったフレームから、水平の向きを反動を起こした突進を出した向きへ回し、
    //! 距離の目標を当たった瞬間の距離より伸ばす。接地するまで回す入力を受けない
    class ThirdPersonFollow : public VirtualCamera
    {
    public:
        ThirdPersonFollow() noexcept;

        //! 追従対象の Transform。参照から使うたびに引き、未設定と解決不可は nullptr
        [[nodiscard]] Transform* Target() const noexcept;

        //! 追従対象の永続参照。データ経由の構築がリフレクション set で書く
        [[nodiscard]] ActorRef TargetRef() const noexcept { return m_targetRef; }

        //! プレイ開始 / rebuild ごとに初期姿勢へ戻す
        void OnStart() override;

        //! 自動ズームの判定に使う接地と速度を値で受け取る。移動 Component の型を include せずに済む
        void SetFollowMotion(bool grounded, const NS::Core::Vector3& velocity) noexcept;
        //! @brief 追う相手の根から、注視の高さを測り始める所までの縦のずれを受け取る
        //! @details 注視点は 根 + ずれ + 頭の高さ。追う相手が形を変えて根だけを上げ下げする時に、見る高さを保つ。
        //! 既定は 0。ずれには描画の補間を掛けないので、根を上げ下げする側は
        //! Transform::ShiftPosition で前フレームの位置も一緒にずらす
        void SetTargetHeightOffset(float offset) noexcept;

        //! @brief このフレームの溜めの状態を受け取る
        //! @details 受けた値は次の OnUpdate だけで使う。渡されなかったフレームは押していないのと同じに扱う
        //! 非数・0 未満・1 を超える溜め量と、狙う相手がいる時の非数の中心・非数か負の半径は壊れた値
        //! @param[in] desc 溜めの状態
        //! @return 受け取った場合 true、壊れた値で何も変えなかった場合は false
        bool SetFollowCharge(const FollowChargeDesc& desc) noexcept;

        //! @brief 溜めで締めている視野角を返す
        //! @return 基準の視野角から引いている角度 (度)。締めていない時は 0
        [[nodiscard]] float ChargeNarrowDegrees() const noexcept { return m_chargeNarrowDegrees; }

        //! @brief 溜めの構図のずらしを返す
        //! @return x がカメラの右、y がカメラの上の向きのずれ (m)。ずらしていない時は 0
        [[nodiscard]] NS::Core::Vector2 ChargeFrameOffset() const noexcept { return m_chargeFrameOffset; }

        //! @brief 溜めの締め・揺れ・構図のずらしをその場で 0 にし、受けた溜めの状態を捨てる
        void ClearCharge() noexcept;

        //! @brief このフレームに追う相手が反動の状態かと、
        //! 反動を起こした突進を出した向きを受け取る
        //! @details 受けた値は次の OnUpdate だけで使う。
        //! 渡されなかったフレームは反動でないのと同じに扱う。
        //! 反動になったフレームから反動の状態が外れるまで反動の間の追い方で追い、
        //! 外れたら普通の追い方へ寄せ戻す。
        //! 反動になったフレームから、水平の向きを突進の向きへ近い側から回し始める。
        //! 反動になってから、反動の状態でなく接地したフレームまでは、
        //! マウスと右スティックで回せない。
        //! 接地は SetFollowMotion で受けた値で見て、
        //! 一度も受けていなければ反動の状態が外れたフレームで回せるようにする。
        //! 突進の向きに有限でない成分があれば壊れた値
        //! @param[in] desc 反動の状態
        //! @return 受け取った場合 true、壊れた値で何も変えなかった場合は false
        bool SetFollowRebound(const FollowReboundDesc& desc) noexcept;

        //! @brief 反動の間の追い方と寄せ戻しと向きの回しをその場で止め、
        //! 受けた反動の状態を捨てる
        //! @details 次の姿勢から普通の追い方になり、次のフレームから回す入力が効く。
        //! 回している途中の向きはそのまま残す
        void ClearRebound() noexcept;

        //! 将来 Settings UI から繋ぐ
        void SetSensX(float radPerPixel) noexcept;
        [[nodiscard]] float SensX() const noexcept { return m_sensX; }
        void SetSensY(float radPerPixel) noexcept;
        [[nodiscard]] float SensY() const noexcept { return m_sensY; }
        void SetInvertX(bool invert) noexcept;
        [[nodiscard]] bool IsInvertX() const noexcept { return m_invertX; }
        void SetInvertY(bool invert) noexcept;
        [[nodiscard]] bool IsInvertY() const noexcept { return m_invertY; }

        //! 自動ズームの距離 3 段を idle / run / jump で設定する。非正値は無視する
        void SetAutoDistances(float idle, float run, float jump) noexcept;
        //! 自動ズームで run 距離へ切替える水平速度しきい値
        void SetRunSpeedThreshold(float speed) noexcept;

        //! 距離を手動固定。自動ズームを止め、ClearManualDistance() で戻す
        void SetDistance(float distance) noexcept;
        void ClearManualDistance() noexcept;
        [[nodiscard]] float Distance() const noexcept { return m_distance; }
        [[nodiscard]] bool IsManualDistance() const noexcept { return m_manualDistance; }

        [[nodiscard]] float Yaw() const noexcept { return m_yaw; }
        [[nodiscard]] float Pitch() const noexcept { return m_pitch; }

        //! プレイ開始時の向き。OnStart で現在 yaw/pitch へ写し、以降はプレイ中の手動回転で動く
        [[nodiscard]] float InitialYaw() const noexcept { return m_initialYaw; }
        [[nodiscard]] float InitialPitch() const noexcept { return m_initialPitch; }

        //! editor のギズモで置いたカメラ world 位置から、target 頭を基準に yaw/pitch/距離を逆算し初期姿勢へ書く
        //! target 未解決や距離ほぼ 0 なら何もしない。初期姿勢は data 保存され、プレイ開始時の向きになる
        //! 逆算後の yaw/pitch/distance を現在値にも即反映し、edit 中の EvaluatePose 表示をその場で追従させる
        void SetInitialPoseFromCameraPosition(const NS::Core::Vector3& cameraPosition) noexcept;

        //! fixed step で yaw/pitch・distance spring を更新。最終姿勢は EvaluatePose が返してガタつきを避ける
        void OnUpdate() override;

        //! alpha で補間した target を追う最終姿勢を返す。CameraManager が選択時に実カメラへ書く
        [[nodiscard]] CameraPose EvaluatePose(float alpha) const noexcept override;

        // 追従カメラの感触を Inspector へ公開する。毎フレーム読まれるのでライブで効く
        // 追従対象は永続参照で、使うたびに引くので書き換えもその場で効く
        NS_REFLECT_BEGIN(ThirdPersonFollow, VirtualCamera)
        NS_REFLECT_FIELD(m_targetRef, "追従対象")
        NS_REFLECT_FIELD(m_initialYaw, "初期ヨー")
        NS_REFLECT_FIELD(m_initialPitch, "初期ピッチ")
        NS_REFLECT_FIELD(m_springOmega, "バネ角速度")
        NS_REFLECT_FIELD(m_idleDistance, "待機時距離")
        NS_REFLECT_FIELD(m_runDistance, "走行時距離")
        NS_REFLECT_FIELD(m_jumpDistance, "ジャンプ時距離")
        NS_REFLECT_FIELD(m_runSpeedThreshold, "走り判定速度")
        NS_REFLECT_FIELD(m_headHeight, "頭の高さ")
        NS_REFLECT_FIELD(m_sensX, "感度 X")
        NS_REFLECT_FIELD(m_sensY, "感度 Y")
        NS_REFLECT_FIELD(m_stickSensX, "スティック感度 X")
        NS_REFLECT_FIELD(m_stickSensY, "スティック感度 Y")
        NS_REFLECT_FIELD(m_invertX, "反転 X")
        NS_REFLECT_FIELD(m_invertY, "反転 Y")
        NS_REFLECT_FIELD(m_pitchMin, "ピッチ下限")
        NS_REFLECT_FIELD(m_pitchMax, "ピッチ上限")
        NS_REFLECT_FIELD(m_chargeNarrowMaxDegrees, "溜めで締める視野角")
        NS_REFLECT_FIELD(m_chargeNarrowReturnFrames, "締めを戻すフレーム数")
        NS_REFLECT_FIELD(m_chargeShakePitchDegrees, "溜めの揺れの縦")
        NS_REFLECT_FIELD(m_chargeShakeYawDegrees, "溜めの揺れの横")
        NS_REFLECT_FIELD(m_chargeShakeFrequency, "溜めの揺れの細かさ")
        NS_REFLECT_FIELD(m_chargeShakeDecayPerSecond, "溜めの揺れの消える速さ")
        NS_REFLECT_FIELD(m_chargeFrameRatio, "溜めの構図の枠")
        NS_REFLECT_FIELD(m_chargeFrameOmega, "溜めの構図のバネ角速度")
        NS_REFLECT_FIELD(m_reboundFollowOmega, "反動の間の横と前後のバネ角速度")
        NS_REFLECT_FIELD(m_reboundMaxLag, "反動の間の横と前後の遅れの上限")
        NS_REFLECT_FIELD(m_reboundScreenBand, "反動の間の上下の帯")
        NS_REFLECT_FIELD(m_reboundReturnFrames, "反動の後に戻すフレーム数")
        NS_REFLECT_FIELD(m_reboundTurnFrames, "反動の向きへ回すフレーム数")
        NS_REFLECT_FIELD(m_reboundPullBack, "反動の間に下げる距離")
        NS_REFLECT_FIELD(m_reboundPartnerWeight, "反動の間に相手を収める重み")
        NS_REFLECT_FIELD(m_reboundPartnerReleaseDistance, "相手を見送る距離")
        NS_REFLECT_FIELD(m_reboundPullStartDistance, "引き始めの二人の距離")
        NS_REFLECT_FIELD(m_reboundPullPerMeter, "二人の距離 1 m あたりに引く距離")
        NS_REFLECT_FIELD(m_reboundPullMax, "相手を収める引きの上限")
        NS_REFLECT_FIELD(m_reboundPartnerHalfSeconds, "相手を収めるばねの半分の秒")
        NS_REFLECT_FIELD(m_forcedLaunchFollowOmega, "強制発射の追う速さ")
        NS_REFLECT_FIELD(m_forcedLaunchMaxLag, "強制発射の遅れの上限")
        NS_REFLECT_ACCESSOR(float, "ファークリップ", FarPlane(), SetFarPlane)
        NS_REFLECT_ACCESSOR(int, "優先度", VcamPriority(), SetVcamPriority)
        NS_REFLECT_END()

    private:
        // 受けた溜めの状態から締め・揺れ・構図のずらしを 1 フレーム進める
        // look はこのフレームの注視点
        void UpdateCharge(const FollowChargeDesc& charge,
                          const Transform& target,
                          const NS::Core::Vector3& look,
                          float dt) noexcept;

        // 反動になったフレームかと反動の状態から、反動の間の追い方の段を進める
        // 勝手に出た突進になったフレームと、その最中かも見る
        // 距離と注視点を決める前に呼ぶ
        // head は追う相手の頭
        void UpdateReboundPhase(
            bool began, bool rebounding, bool launchBegan, bool launching, const NS::Core::Vector3& head) noexcept;

        // 反動になったフレームかと受けた反動の状態から、回す入力を受けないかを決め、
        // 水平の向きの回しを 1 フレーム進める
        // 回す入力を足す前に呼ぶ
        void UpdateReboundTurn(bool began, const FollowReboundDesc& rebound) noexcept;
        // 反動の間に受けた相手の位置から、注視点の寄せと引きのばねを 1 フレーム進める。反動の間でなければ 0 へ戻す
        void UpdateReboundPartner(const FollowReboundDesc& rebound, const NS::Core::Vector3& head, float dt) noexcept;

        // 今の段でこのフレームの注視点を決めて控える
        // 普通の追い方の時は head をそのまま返す
        // ball は画面の上下の帯に入れておく追う相手の点
        [[nodiscard]] NS::Core::Vector3 UpdateReboundLook(const NS::Core::Vector3& head,
                                                          const NS::Core::Vector3& ball,
                                                          float dt) noexcept;

        // 反動の間の追い方の段
        enum class ReboundPhase
        {
            None,      // 普通の追い方
            Launching, // 勝手に出た突進の間
            Following, // 反動の状態の間
            Returning, // 反動の状態が外れてから普通の追い方へ寄せ戻している
        };

        ActorRef m_targetRef{}; // 追従対象の永続参照。ポインタで控えないので、相手が先に消えても空を引くだけ

        // 値で受けた追従先の運動。届くまでは待機距離のまま
        bool m_followGrounded = false;
        float m_followHorizontalSpeed = 0.0f; // 速度の向きは使わないので水平の大きさへ畳んで持つ
        bool m_hasFollowMotion = false;
        float m_targetHeightOffset = 0.0f; // 追う相手の根から注視の高さを測り始める所までの縦のずれ

        float m_yaw = 0.0f;       // 水平回転角
        float m_pitch = -0.2618f; // 仰俯角

        // プレイ開始時の初期姿勢。editor のギズモ / Inspector が書き、OnStart で m_yaw/m_pitch へ写す
        float m_initialYaw = 0.0f;
        float m_initialPitch = -0.2618f;

        float m_distance = 6.0f;        // 現在のカメラ距離
        float m_desiredDistance = 6.0f; // 目標カメラ距離
        float m_springOmega = 6.0f;     // 距離バネの追従の速さ
        bool m_manualDistance = false;  // 距離を手動固定中か

        float m_idleDistance = 5.0f;      // 静止時の距離
        float m_runDistance = 6.0f;       // 走行時の距離
        float m_jumpDistance = 7.0f;      // 空中時の距離
        float m_runSpeedThreshold = 4.0f; // run 距離へ切替える水平速度

        float m_headHeight = 1.2f; // 注視点を頭へ上げる高さ

        float m_sensX = 0.0030f;   // マウス水平感度
        float m_sensY = 0.0030f;   // マウス垂直感度
        float m_stickSensX = 2.0f; // スティック水平感度
        float m_stickSensY = 1.5f; // スティック垂直感度
        bool m_invertX = false;    // 水平反転
        bool m_invertY = false;    // 垂直反転

        float m_pitchMin = -1.396f;  // 仰俯角の下限
        float m_pitchMax = -0.0873f; // 仰俯角の上限

        float m_chargeNarrowMaxDegrees = 15.0f;   // 溜めきりで締める視野角 (度)
        int m_chargeNarrowReturnFrames = 6;       // 放してから締めを 0 へ戻すフレーム数
        float m_chargeShakePitchDegrees = 0.3f;   // 溜めきりの溜めの揺れの縦の首振りの最大 (度)
        float m_chargeShakeYawDegrees = 0.15f;    // 溜めきりの溜めの揺れの横の首振りの最大 (度)
        float m_chargeShakeFrequency = 10.0f;     // 溜めの揺れのノイズの格子を 1 秒に進める数
        float m_chargeShakeDecayPerSecond = 4.0f; // 放した後に溜めの揺れのトラウマが 1 秒に減る量
        float m_chargeFrameRatio = 0.7f;          // 構図の枠。視野の半分に対する割合
        float m_chargeFrameOmega = 26.0f;         // 構図のずらしのバネ角速度 (1/秒)

        float m_reboundFollowOmega = 4.0f; // 反動の間に注視点の横と前後が寄るバネ角速度 (1/秒)
        float m_reboundMaxLag = 1.5f;      // 反動の間に注視点が横と前後へ遅れてよい上限 (m)
        float m_reboundScreenBand = 0.5f;  // 反動の間の上下の帯。視野の半分への割合
        int m_reboundReturnFrames = 20;    // 反動が外れてから普通の追い方へ寄せ戻すフレーム数
        int m_reboundTurnFrames = 20;      // 反動になってから突進の向きへ回し終えるフレーム数
        float m_reboundPullBack = 1.0f;    // 反動の間に当たった瞬間の距離より伸ばす距離 (m)
        // 真ん中の反動は、自分の跳ね返り方と飛んでいく相手の飛び方の差を見せる。主役は自機のまま、相手も画面に残す
        // 注視点 = 自機 + 重み × (相手 − 自機)、重み = 最大 × (1 − 二人の距離 ÷ 見送りの距離)。最大 0.3 は 1
        // 節の決定の値
        float m_reboundPartnerWeight = 0.3f;
        // 相手がこれより遠くへ飛ぶと重みが 0 になり、自機だけを追う (m)。出発点で、値は撮って詰める
        float m_reboundPartnerReleaseDistance = 30.0f;
        // 二人がこれより離れた分に、1 m あたりの距離を掛けて引く。引きは上限で止まる (m)
        float m_reboundPullStartDistance = 3.0f;
        float m_reboundPullPerMeter = 0.3f;
        float m_reboundPullMax = 4.0f;
        // 寄せと引きのばねが差の半分まで追いつく秒。1 節の決定の 0.15 秒から
        float m_reboundPartnerHalfSeconds = 0.15f;
        // 勝手に出た突進の間に注視点の横と前後が寄るバネ角速度 (1/秒)。反動と同じ 4 から始める
        float m_forcedLaunchFollowOmega = 4.0f;
        // 勝手に出た突進の間に見せる遅れの上限 (m)。反動の 1.5 m だと約 5 フレームで届き、普通の突進と見分けにくい
        float m_forcedLaunchMaxLag = 3.0f;

        FollowChargeDesc m_charge{};               // 次の OnUpdate で使う溜めの状態
        float m_chargeHoldNarrowDegrees = 0.0f;    // 前のフレームの押している間の締め (度)
        float m_chargeNarrowDegrees = 0.0f;        // 今の締め (度)
        float m_chargeReturnFromDegrees = 0.0f;    // 戻し始めた時の締め (度)
        int m_chargeReturnFrame = 0;               // 戻しの何フレーム目か。0 は戻していない
        NS::Core::Vector2 m_chargeFrameOffset{};   // 今の構図のずらし (m、カメラの右と上)
        NS::Core::Vector2 m_chargeFrameVelocity{}; // 構図のずらしの速さ (m/秒)

        FollowReboundDesc m_rebound{};                    // 次の OnUpdate で使う反動の状態
        bool m_wasRebounding = false;                     // 前のフレームに反動の状態だったか
        bool m_wasForcedSlamming = false;                 // 前のフレームに勝手に出た突進の最中だったか
        bool m_reboundLookHeld = false;                   // 反動になってから接地するまで、回す入力を受けないか
        float m_reboundTurnAngle = 0.0f;                  // 反動になったフレームに決めた、回す角度 (ラジアン)
        int m_reboundTurnFrame = 0;                       // 回しの何フレーム目か。欄のフレーム数で回し終える
        ReboundPhase m_reboundPhase = ReboundPhase::None; // 今の段
        NS::Core::Vector3 m_reboundAnchor{};              // 横と前後を遅らせて追う注視点。高さは留める
        NS::Core::Vector3 m_reboundAnchorVelocity{};      // 注視点の横と前後の速さ (m/秒)。縦は使わない
        float m_reboundLagCap = 0.0f;                     // 反動の間の遅れの今の上限 (m)。欄の上限より下にはしない
        NS::Core::Vector3 m_reboundReturnOffset{};        // 寄せ戻し始めの、注視点 − 追う相手の頭 (m)
        int m_reboundReturnFrame = 0;                     // 寄せ戻しの何フレーム目か
        float m_reboundBaseDistance = 0.0f;        // 反動になったフレームに決めた、相手を収める引きの前の距離 (m)
        NS::Core::Vector3 m_partnerLean{};         // 注視点を相手へ寄せている量 (m)
        NS::Core::Vector3 m_partnerLeanVelocity{}; // 寄せの速さ (m/秒)
        float m_partnerPull = 0.0f;                // 相手を収めるために引いている距離 (m)
        float m_partnerPullVelocity = 0.0f;        // 引きの速さ (m/秒)
        NS::Core::Vector3 m_look{};                // このフレームの注視点。反動と寄せ戻しで使う
        NS::Core::Vector3 m_previousLook{};        // 前のフレームの注視点。描画の補間に使う
        bool m_hasLook = false;                    // m_look が前のフレームの注視点か。休止とプレイ開始の後は偽
    };

} // namespace NS::Obj
