#pragma once

#include "Game/Level/HitTier.h"
#include "Game/Level/ImpactOutcome.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/SlamAim.h"
#include "Game/Player/ReboundArc.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/Gfx/Render/FrameConstants.h"
#include "NSlib/Core/Windows/Gamepad.h"

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
    class HitSensorDirector;
} // namespace NS

namespace GL
{
    namespace Level
    {
        //! PlayerParams と現在の固定ステップから、配分に使う全調整値を作る
        [[nodiscard]] ImpactTuning MakeImpactTuning(const GL::Player::PlayerParams* params) noexcept;

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
            std::string setName;            //!< 自機が始める段の束の名前。例 体当たり[真ん中]
            float faceU = 0.0f;        //!< 段を決めた面の上の左右の位置。自機から見て右が正。判定できない体は 0
            float faceV = 0.0f;        //!< 段を決めた面の上の上下の位置。上が正。判定できない体は 0
            //! 反動の配分が使った面の向き (ImpactOutcome::faceNormal)。外れの火花もこの面に沿って流す
            NS::Vector3 faceNormal;
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

        //! @brief 裁定が自機から読む、シーン・位置・除く相手
        //! @details 自機が呼ぶたびに組んで渡し、ImpactResolver は控えない
        struct ImpactSelf
        {
            //! 自機のシーンのセンサーの管理クラス
            const NS::HitSensorDirector* sensors = nullptr;
            NS::Vector3 position;                   //!< 自機のルートの位置。世界座標
            const NS::SceneActor* ignore = nullptr; //!< 重なりの問いから除く相手。自機
        };

        //! @brief 自機の衝突結果と当たりの記録を決め、突進の線の先の相手を予測する部品
        //! @details 身体が動く前に突進の予定速度で判定する。物理停止で実速度が消えるため。
        //! 触れる前の演出は自機の当たる直前の状態が、検知の後の止め・明け・反動は自機の当たりの状態が進め、
        //! 相手への明けと反動の速度は Release が出す。
        //! 相手にはメッセージで飛ばしを通知し、相手の部品を直接操作しない
        class ImpactResolver : public NS::Object
        {
        public:
            //! @brief 次の固定ステップで重なる相手を控える。身体の実速度ではなく突進の予定速度を使う
            //! @details 突進外と自機の停止中は探さない。結果は StepState が一度だけ使う
            //! @param[in] collider 自機の当たりの形。空でないこと
            void ObserveImpact(const ImpactSelf& self, const BodySlamMotion& motion, const NS::Collider* collider);

            //! @brief ObserveImpact の結果を一度だけ裁定し、検知しなければ突進の線の先の相手を予測して、答えを書き直す
            //! @details 検知したら当たりの記録を書く。予測は motion.predictsContact の時だけ線を引く。
            //! 自機を動かす事 (突進の打ち切り・当たりの状態へ移る事・エフェクト) は答えに書くだけで、呼ぶのは Player
            //! @param[in] params・body・collider 空でないこと
            //! @param[in] hitReaction 自機の束の止めの長さと紫の明けの束を読む先。空なら止めは 0
            void StepState(const ImpactSelf& self,
                           const BodySlamMotion& motion,
                           const GL::Player::PlayerParams* params,
                           const NS::Body* body,
                           const NS::Collider* collider,
                           const NS::HitReaction* hitReaction);

            //! 直近の StepState の答え。StepState の終わりと CancelImpact で書き直す
            [[nodiscard]] const ImpactAnswer& Answer() const noexcept { return m_answer; }

            //! 当たりを検知してから、次の StepState で beginHit を答えに書くまで true
            [[nodiscard]] bool IsHitAwaitingEntry() const noexcept { return m_hitAwaitsEntry; }

            //! @brief 止めが明けたフレームに呼ぶ。相手へ明けを知らせ、止めた自機を動かし直す速度と反動の曲線を答えに書く
            //! @details 呼ぶのは Player の決定の段で、StepState の後。答えの速度と曲線は同じフレームに Player が読む
            void Release();

            //! @brief 控えた相手と、当たりの状態へ移る待ちを捨てる
            //! @details 相手へ明けを送らない。束で始めた演出・世界の速さ・自機以外の止めは Player が戻す。
            //! 最後の当たりの記録は残し、答えの Player がする事は空にする。
            //! 何度呼んでも同じ結果になる。プレイの終わりにも Player が呼び、控えが次のプレイへ残らない
            void CancelImpact() noexcept;

            //! @brief 突進の玉を掃いて、最初に触れるアクターを探す。段は裁定と同じ面判定を使う
            //! @details 赤の高さへ狙う縦速度も予測する。問い合わせに応じない相手は Wide・横ずれ 1・縦速度 0。
            //! 壁や地形による停止は考慮しない。direction は水平成分、maxDistance は m
            //! @param[in] params・body・collider 空でないこと
            //! @return 無効な方向・距離、相手なしの場合は false で outTarget を変更しない
            [[nodiscard]] bool FindSlamLineTarget(const ImpactSelf& self,
                                                  const NS::Vector3& direction,
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
                const NS::HitReaction* hitReaction = nullptr;
            };

            // 次の固定ステップで重なる体から、中心が最も近い相手を選ぶ。無ければ nullptr
            [[nodiscard]] NS::HitSensor* FindOverlapped(const ImpactSelf& self,
                                                        const NS::Vector3& predictedVelocity,
                                                        const NS::Collider* collider) const;

            // StepState の中身。Player がする事を空にした後、答えを書き直す前に呼ぶ
            void AdvanceState(const ImpactSelf& self, const BodySlamMotion& motion, const PlayerParts& parts);
            // 答えのうち Player がする事を空にする
            void ClearOrders() noexcept;

            // 控えた相手へ向かっていれば衝突の結果を決め、当たりの記録を書く。当たりにならなかった場合 false
            [[nodiscard]] bool ResolveObservedHit(const ImpactSelf& self,
                                                  bool hadObservation,
                                                  const BodySlamMotion& motion,
                                                  const PlayerParts& parts);

            // 衝突の計算結果から、貫通・反動の保留値と答えのフラグを更新する
            void ApplyImpactOutcome(const ImpactOutcome& outcome,
                                    const TackleTargetAnswer& answer,
                                    const GL::Player::PlayerParams* params,
                                    float power,
                                    bool centerHit);

            // 直近の当たりの内訳 (m_answer.lastImpact) を書き換える
            void RecordLastImpact(const TackleTargetAnswer& answer,
                                  const BodySlamMotion& motion,
                                  const HitFaceJudgement& judgement,
                                  const ImpactOutcome& outcome,
                                  const PlayerParts& parts,
                                  float power);

            // 突進の線の先に触れる相手を探し、検知までの見込みのフレーム数と相手を答えに書く
            void PredictContact(const ImpactSelf& self, const BodySlamMotion& motion, const PlayerParts& parts);

            // 止めていた自機を動かし直す速度と反動の曲線を答えに書く。反発は反動を始め、貫通は速度を書くよう Player
            // に頼む
            void ApplyRebound();

            // 相手へ明けを知らせて飛ばすか壊させる
            void LaunchTarget();

            // 明けたフレームに自機が持つ速度。反動の当たりは、明けに BeginRebound が同じ m_pendingReboundArc
            // から出し直す
            NS::Vector3 m_pendingSelfVelocity{0.0f, 0.0f, 0.0f};
            GL::Player::ReboundArc m_pendingReboundArc{}; // 明けたフレームに自機を弾く反動の向きと高さと距離
            LaunchArc m_pendingLaunchArc{};
            NS::Vector3 m_pendingImpactDir{0.0f, 0.0f, 0.0f}; // 発射の水平方向。食い込みの向き
            NS::SceneActor* m_pendingTarget = nullptr;        // 明けを知らせる相手
            // 検知してから、次の StepState で当たりの状態へ移る事を答えに書くまで true
            bool m_hitAwaitsEntry = false;
            bool m_pendingBreak = false;
            // 直近の更新の答え。1 フレームだけの欄 (反発・貫通・予測) は StepState の頭で戻す
            ImpactAnswer m_answer{};
            NS::SceneActor* m_observedTarget = nullptr;
            TackleTargetAnswer m_observedAnswer{};
            NS::Vector3 m_observedVelocity{};
            bool m_hasObservedTarget = false;
            bool m_stateReady = false;
        };
    } // namespace Level
} // namespace GL
