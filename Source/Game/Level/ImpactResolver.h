#pragma once

#include "Game/Level/HitTier.h"
#include "Game/Level/HitTimeline.h"
#include "Game/Level/ImpactOutcome.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/SlamAim.h"
#include "Game/Player/ReboundArc.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/ActorRef.h"
#include "NSlib/Gfx/Render/FrameConstants.h"
#include "NSlib/Windows/Gamepad.h"

#include "NSlib/Core/Types.h"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GL
{
    namespace Player
    {
        class PlayerParams;
    } // namespace Player
} // namespace GL

namespace NS
{
    class SceneActor;
    class Body;
    class Collider;
    class HitSensor;
    class Model;
} // namespace NS

namespace GL
{
    namespace Level
    {
        //! PlayerParams と現在の固定ステップから、配分に使う全調整値を作る
        [[nodiscard]] ImpactTuning MakeImpactTuning(const GL::Player::PlayerParams& params) noexcept;

        //! @brief 当たり 1 回の裁定の内訳
        struct ImpactRecord
        {
            NS::uint32 sequence = 0;
            NS::uint32 targetId = 0;
            float power = 0.0f;
            float charge01 = 0.0f;
            float overcharge01 = 0.0f; //!< 突進を出した時の溜めすぎの深さ 0..1。赤で放した突進と通常突進は 0
            float positionFactor = 0.0f;
            float offset01 = 0.0f;          //!< 面の判定の横ずれ。相手の半幅と自機の半径の和で割った 0..1
            HitTier tier = HitTier::Center; //!< 当たりの段。相手の面の判定で決まる
            std::string timeline;           //!< 自機が始める段の束の名前。例 体当たり[真ん中]
            //! 面の上の位置から決めた外れの向き。タイムラインの向きの付いた行を選ぶ
            HitDirection direction = HitDirection::Any;
            float faceU = 0.0f;        //!< 段を決めた面の上の左右の位置。自機から見て右が正。判定できない体は 0
            float faceV = 0.0f;        //!< 段を決めた面の上の上下の位置。上が正。判定できない体は 0
            //! 反動の配分が使った面の向き (ImpactOutcome::faceNormal)。外れの火花もこの面に沿って流す
            NS::Vector3 faceNormal;
            float cameraShake = 0.0f;  //!< 揺れの最初の振れの大きさ。横と縦を合わせた長さで、単位は m
            float cameraTrauma = 0.0f; //!< トラウマの揺れで足すトラウマ。トラウマの揺れの無い当たりは 0
            int flashStart = 0;        //!< 白の残りフレーム数の始めの値。白の無い当たりは 0
            float zoomStart = 1.0f;    //!< 寄りの倍率の始めの値。寄りの無い当たりは 1
            float rollStart = 0.0f; //!< 傾きの始めの値 (度)。正は画面の上端をカメラの右へ倒す向き。傾きの無い当たりは 0
            NS::OS::GamepadVibration padStart; //!< パッドの振動の始めの値。振動の無い当たりは 0
            //! 止めのフレーム数。段の束の止めの長さで、貫通は欄「貫通の止め秒」の長さ。束が無い当たりは 0
            int hitStopSteps = 0;
            bool localStop = false; //!< ローカル・ヒットストップ (自機と相手の止め) を使った場合 true
            //! 段階的な明け (明けの後の遅い世界) を使う場合 true。溜めすぎて放した突進で、段の紫の明けの束がある時
            bool gradualRelease = false;
            bool centerHit = false;
            bool broke = false;
            NS::Vector3 selfVelocity; //!< 明けに自機が持つ速度。反動は初速、貫通は減速した突進の速度。単位は m/s
            // 反動の頂点の高さは押し飛ばしの当たりだけが埋める。貫通の当たりは反動しないので 0
            float reboundApexHeight = 0.0f; //!< 自機の反動の、弾かれ始めの高さから頂点までの高さ。単位は m
            // 飛ばす曲線の 3 つの欄は押し飛ばしの当たりだけが埋める。貫通の当たりは相手を飛ばさないので 0
            NS::Vector3 launchVelocity;    //!< 相手の曲線の発射の瞬間の速度。単位は m/s
            float launchDistance = 0.0f;   //!< 相手の曲線が発射の高さへ戻るまでに水平に進む距離。単位は m
            float launchApexHeight = 0.0f; //!< 相手の曲線の、発射の高さから頂点までの高さ。単位は m
            NS::Vector3 impactDir;         //!< 相手の飛ぶ水平の向き。食い込みの向きも同じ
            //! 自機の玉が相手の表面に触れた点。JudgeHitFace の触れる点で、判定できない体の時は相手の体の中心
            NS::Vector3 surfacePoint;
            NS::Vector3 targetPos;
            float targetBottom = 0.0f; //!< 相手の体の外接箱の底の高さ (m)。当たりの粉と照りを置く床
            float targetMass = 1.0f;   //!< 相手の質量。相手が答えた重さ
            bool targetPlaced = true;  //!< 相手が置かれていた (飛んでいなかった) 場合 true
            float launchScale = 0.0f;  //!< 相手の曲線の距離と高さに掛けた比。威力 ÷ 質量の指数乗で、質量 1・威力 1 で 1
            float reboundScale = 0.0f; //!< 自機の反動の高さと距離に掛けた比。威力 × 2 × 質量 ÷ (質量 + 1)
        };

        //! 突進の今。Player が裁定を呼ぶたびに組む
        struct BodySlamMotion
        {
            bool slamming = false; //!< 突進の状態の間 true
            //! 突進中は予定の水平速度と身体の今の縦速度、それ以外は身体の速度。単位は m/s
            //! 壁で消える実速度では衝突を先読みできないので、裁定はこの速度で見る
            NS::Vector3 velocity;
            float charge01 = 0.0f;     //!< 突進を出した時の溜め量 0..1
            float overcharge01 = 0.0f; //!< 突進を出した時の溜めすぎの深さ 0..1。赤で放した突進と通常突進は 0
            //! 突進を終えるまでに進む残りの水平距離 (m)。突進の状態でない時は 0
            //! 触れる前の予測は、この長さで線を引く
            float remainingDistance = 0.0f;
            //! 触れる前の予測の線を引く場合 true。自機の .actor に当たる直前の動作がある時
            bool predictsContact = false;
        };

        //! @brief 当たりの裁定の、直近の StepState の答え
        //! @details 書き直すのは StepState の終わりと CancelImpact だけ。見た目の段で読む値は、どの読み先でも同じになる
        struct ImpactAnswer
        {
            ImpactRecord lastImpact; //!< 直近の当たりの内訳。まだ当たっていない間は sequence が 0
            bool rebounded = false;  //!< 直近の更新で反発を検知した
            bool broke = false;      //!< 直近の更新で貫通を検知した
            //! 突進の線の先の相手を検知するまでの見込みのフレーム数。検知の 1 フレーム前で 1
            //! 突進していない時・線の先に相手がいない時・検知したフレームと、予測の線を引かない突進 (predictsContact) は
            //! -1
            int framesToPredictedContact = -1;
            //! 突進の線の先に予測した相手。framesToPredictedContact が -1 の時は空
            std::optional<SlamLineTarget> predicted;

            // ここから下は Player がこのフレームにする事。StepState の頭と CancelImpact で空にする
            //! 当たりを検知した。明けた後の速度を通常移動に乗せるため、止める前に突進を打ち切り、前の当たりの演出を止める
            bool cancelBodySlam = false;
            //! 検知の次のフレーム。当たりの状態へ移り、段の束を始める
            bool beginHit = false;
            //! 止めた自機を動かし直す速度 (m/s)。reboundArc がある時は反動を始め、曲線にならない時だけこの速度を書く
            //! 書くのは Release
            std::optional<NS::Vector3> releaseVelocity;
            std::optional<GL::Player::ReboundArc> reboundArc; //!< 反動で弾く曲線。貫通の当たりは空
        };

        //! @brief 自機の衝突結果と当たりの記録を決め、突進の線の先の相手を予測する部品
        //! @details 身体が動く前に突進の予定速度で判定する。物理停止で実速度が消えるため。
        //! 触れる前の演出は自機の当たる直前の状態が、検知の後の止め・明け・反動は自機の当たりの状態が進め、
        //! 相手への明けと反動の速度は Release が出す。
        //! 相手にはメッセージで飛ばしを通知し、相手の部品を直接操作しない
        class ImpactResolver : public NS::Object
        {
        public:
            explicit ImpactResolver(NS::SceneActor* owner) noexcept : m_owner(owner) {}

            //! @brief 次の固定ステップで重なる相手を控える。身体の実速度ではなく突進の予定速度を使う
            //! @details 突進外と自機の停止中は探さない。結果は StepState が一度だけ使う
            //! @param[in] collider 自機の当たりの形。空でないこと
            void ObserveImpact(const BodySlamMotion& motion, const NS::Collider* collider);

            //! @brief ObserveImpact の結果を一度だけ裁定し、検知しなければ突進の線の先の相手を予測して、答えを書き直す
            //! @details 検知したら当たりの記録を書く。予測は motion.predictsContact の時だけ線を引く。
            //! 自機を動かす事 (突進の打ち切り・当たりの状態へ移る事・エフェクト) は答えに書くだけで、呼ぶのは Player
            //! @param[in] params・body・collider 空でないこと
            //! @param[in] model 震えの線で自機の形を挟むのに読む。空なら線を出さない
            //! @param[in,out] hitReaction 当たりの演出と、自機の束の止めの長さを読む先。空なら書かず、止めは 0
            void StepState(const BodySlamMotion& motion,
                           const GL::Player::PlayerParams* params,
                           const NS::Body* body,
                           const NS::Collider* collider,
                           const NS::Model* model,
                           NS::HitReaction* hitReaction);

            //! 直近の StepState の答え。StepState の終わりと CancelImpact で書き直す
            [[nodiscard]] const ImpactAnswer& Answer() const noexcept { return m_answer; }

            //! 当たりを検知してから、次の StepState で beginHit を答えに書くまで true
            [[nodiscard]] bool IsHitAwaitingEntry() const noexcept { return m_hitAwaitsEntry; }

            //! @brief 止めが明けたフレームに呼ぶ。相手へ明けを知らせ、止めた自機を動かし直す速度と反動の曲線を答えに書く
            //! @details 呼ぶのは Player の決定の段で、StepState の後。答えの速度と曲線は同じフレームに Player が読む
            void Release();

            //! @brief 控えた相手と、当たりの状態へ移る待ちを捨てる
            //! @details 相手へ明けを送らない。世界の速さと自機以外の止めは時計が変えた時だけ戻し、形も元へ戻す。
            //! 束で始めた演出は止めない (Player が止める)。最後の当たりの記録は残し、答えの Player がする事は空にする。
            //! 何度呼んでも同じ結果になる。プレイの終わりにも Player が呼び、止めが次のプレイへ残らない
            void CancelImpact(NS::HitReaction* hitReaction) noexcept;

            //! @brief 突進の玉を掃いて、最初に触れる配置物を探す。段は裁定と同じ面判定を使う
            //! @details 赤の高さへ狙う縦速度も予測する。問い合わせに応じない相手は Wide・横ずれ 1・縦速度 0。
            //! 壁や地形による停止は考慮しない。direction は水平成分、maxDistance は m
            //! @param[in] params・body・collider 空でないこと
            //! @return 無効な方向・距離、相手なしの場合は false で outTarget を変更しない
            [[nodiscard]] bool FindSlamLineTarget(const NS::Vector3& direction,
                                                  float maxDistance,
                                                  const GL::Player::PlayerParams* params,
                                                  const NS::Body* body,
                                                  const NS::Collider* collider,
                                                  SlamLineTarget& outTarget) const;

            NS_REFLECT_NONE(ImpactResolver, NS::Object)

        private:
            // StepState が受けた自機の部品。StepState の中で呼ぶ関数へそのまま渡し、控えない
            struct PlayerParts
            {
                const GL::Player::PlayerParams* params = nullptr;
                const NS::Body* body = nullptr;
                const NS::Collider* collider = nullptr;
                const NS::Model* model = nullptr;
                NS::HitReaction* hitReaction = nullptr;
            };

            // 次の固定ステップで重なる体から、中心が最も近い相手を選ぶ。無ければ nullptr
            [[nodiscard]] NS::HitSensor* FindOverlapped(const NS::Vector3& predictedVelocity,
                                                        const NS::Collider& collider) const;

            // StepState の中身。Player がする事を空にした後、答えを書き直す前に呼ぶ
            void AdvanceState(const BodySlamMotion& motion, const PlayerParts& parts);
            // 答えのうち Player がする事を空にする
            void ClearOrders() noexcept;
            [[nodiscard]] bool IsHoldingPlayer() const noexcept { return m_holdArmed && !m_holdReleased; }

            struct EventRunner;

            // 走っているタイムラインの事象を止め、時計と形を元へ戻す
            void AbortTimeline(NS::HitReaction* hitReaction) noexcept;

            // 控えた相手へ向かっていれば衝突の結果を決め、段のタイムラインを始める。当たりにならなかった場合 false
            [[nodiscard]] bool ResolveObservedHit(bool hadObservation,
                                                  const BodySlamMotion& motion,
                                                  const PlayerParts& parts);

            // 突進の線の先に触れる相手を探し、検知までの見込みのフレーム数と相手を答えに書く
            void PredictContact(const BodySlamMotion& motion, const PlayerParts& parts);

            // 時計の今のフレームに始まる事象を並びの順に起こし、止めの明けを数える
            void AdvanceTimeline(const PlayerParts& parts);

            void RunEvent(const HitEvent& event, const PlayerParts& parts);

            // 揺れの事象から、この当たりのカメラの揺れを組む。最初の振れは 強さ × 威力 × 質量の効きで、
            // 横と縦の重みの比で分ける。重みが両方 0 なら揺らさない (フレーム数 0)
            // 事前条件: 反動の向き・威力・質量の効き・揺れの種を控え終えている
            [[nodiscard]] NS::CameraShakeDesc ShakeDescFor(const CameraShakeEvent& shake, int length) const noexcept;

            // トラウマの揺れの事象から、この当たりのトラウマを組む。量は欄のトラウマ × 威力
            // 一撃は外した側 (面の上の位置) へ振る。面の上の位置が無い当たりは、自機が弾かれる向きの画面の側へ振る
            // 事前条件: 威力・揺れの種・反動の向きを控え終えている
            [[nodiscard]] NS::CameraTraumaDesc TraumaDescFor(const CameraTraumaEvent& trauma) const noexcept;
            // 自機の横揺れを始める。カメラが無い世界では揺らさない
            void StartBodyShake(const BodyShakeEvent& shake, int length);
            void StopBodyShake() noexcept;
            // 自機の奥揺れを始める。押す向きが水平に無ければ揺らさない
            void StartDepthShake(const DepthShakeEvent& shake, int length);
            void StartSelfShake(const NS::DrawShakeDesc& self);
            [[nodiscard]] NS::SceneActor* PendingTargetActor() const noexcept;
            // 自機の衝撃の震えを始める。自機の Model に震えを頼む
            // 裏まで届くフレーム数は length の半分までに抑える。collider は空でないこと
            void StartTremor(const ImpactTremorEvent& tremor, int length, const NS::Collider* collider);
            // 触れた点の真下を中心にした床の波を、length フレームだけ出す
            void StartGroundWave(const GroundWaveEvent& wave, int length);
            // 床の波の半径と強さを今の時計で曲線から引き、場面へ書く。長さの終わりで消す
            void AdvanceGroundWave();
            void StopGroundWave() noexcept;
            // 触れた点を中心にした歪みの輪を、length フレームだけ HitReaction へ頼む
            void StartDistortionRing(const DistortionRingEvent& ring, int length, NS::HitReaction* hitReaction);
            void StopDistortionRing(NS::HitReaction* hitReaction) noexcept;
            // 相手と自機の今の形をまとめて挟む震えの線を、length フレームだけ出す
            void StartShakeLines(const ShakeLinesEvent& lines, int length, const PlayerParts& parts);
            void StopTremor() noexcept;
            // 跳ね返りは同じタイムラインの反動開始時に合わせる
            [[nodiscard]] NS::CameraSinkDesc SinkDescFor(const CameraSinkEvent& sink,
                                                         const HitEvent& event) const noexcept;
            // カメラを direction の水平の向きへ、distance の曲線の距離だけずらし始める。水平の成分が無ければずらさない
            // onScreen が真なら、向きをカメラから見た画面の上の向きへ写してずらす
            void StartCameraNudge(const NS::Vector3& direction,
                                  const NS::Curve& distance,
                                  int length,
                                  bool onScreen,
                                  NS::HitReaction* hitReaction);

            // 前提: 相手の飛ぶ向きを記録済み
            [[nodiscard]] NS::CameraZoomRollDesc ZoomRollDescFor(const ZoomRollEvent& zoomRoll,
                                                                 int length) const noexcept;

            // 検知のフレームに、返りの事象の種類ごとに最初の 1 つから始めの値を記録へ写す。事象の無い返りは無い時の値
            // 事前条件: ShakeDescFor と ZoomRollDescFor の事前条件と同じ
            void RecordReturns(const std::vector<HitEvent>& events);

            // 止めていた自機を動かし直す速度と反動の曲線を答えに書く。反発は反動を始め、貫通は速度を書くよう Player
            // に頼む
            void ApplyRebound();

            // 相手へ明けを知らせて飛ばすか壊させる
            void LaunchTarget();

            std::vector<HitEvent> m_events;       // 走っているタイムラインのうち、当たりの向きで起きる事象
            std::vector<std::size_t> m_eventRows; // m_events と同じ並びで、ファイルの並びでの番号
            int m_clock = 0;                      // 検知のフレームを 0 にした今のフレーム
            std::optional<int> m_padStartClock;   // 振動を始めた時計の値。同じ値に始まる振動は重ねる
            int m_clockEnd = 0;                   // 最後の事象が終わる時計の値。ここまで進めたら時計を止める
            bool m_clockRunning = false;
            bool m_holdArmed = false; // この当たりで自機を止めるか。止めの事象のある当たりで立つ
            bool m_holdReleased = false;
            bool m_hasReboundEvent = false;
            bool m_stopStarted = false;
            bool m_startedGradualRelease = false; // 段階的な明けで世界を遅くしたか。打ち切りで普段の速さへ戻す
            bool m_heldOthers = false;            // 自機以外の止めを置いたか。打ち切りで解く
            int m_stopEnd = 0;                    // 止めの事象の最後のフレーム
            int m_breakStopSteps = -1;            // 貫通の当たりの止めのフレーム数。負なら止めの事象の長さのまま
            ShapeEvent m_shape{};
            int m_shapeStart = 0;
            int m_shapeLength = 0;
            bool m_shapeActive = false;
            // 明けたフレームに自機が持つ速度。反動の当たりは、明けに BeginRebound が同じ m_pendingReboundArc
            // から出し直す
            NS::Vector3 m_pendingSelfVelocity{0.0f, 0.0f, 0.0f};
            GL::Player::ReboundArc m_pendingReboundArc{}; // 明けたフレームに自機を弾く反動の向きと高さと距離
            LaunchArc m_pendingLaunchArc{};
            NS::Vector3 m_pendingImpactDir{0.0f, 0.0f, 0.0f}; // 発射の水平方向。食い込みと振動の軸
            NS::ActorRef m_pendingTarget{};                   // 知らせる相手。凍結をまたぐので使うたびに引く
            float m_pendingPower = 0.0f;                      // この衝突の威力。揺れの最初の振れに掛ける
            float m_pendingMassFactor = 0.0f;                 // この衝突の質量の効き。揺れの最初の振れに掛ける
            NS::uint32 m_pendingShakeSeed = 0;                // この衝突の揺れの、入れ替わりの間隔を選ぶ種
            // 検知してから、次の StepState で当たりの状態へ移る事を答えに書くまで true
            bool m_hitAwaitsEntry = false;

            // 出している床の波。場面へ書くのはこの裁定役だけ
            struct GroundWaveRun
            {
                GroundWaveEvent event{};
                float centerX = 0.0f;
                float centerZ = 0.0f;
                int length = 0;
                int startClock = 0;
                bool active = false;
            };
            GroundWaveRun m_groundWave;

            bool m_pendingBreak = false;
            // 直近の更新の答え。1 フレームだけの欄 (反発・止めの頭・始まった事象など) は StepState の頭で戻す
            ImpactAnswer m_answer{};
            NS::ActorRef m_observedTarget{};
            TackleTargetAnswer m_observedAnswer{};
            NS::Vector3 m_observedVelocity{};
            bool m_hasObservedTarget = false;
            bool m_stateReady = false;
            NS::SceneActor* m_owner = nullptr; // 持ち主 (非所有)
        };
    } // namespace Level
} // namespace GL
