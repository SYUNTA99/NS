#pragma once

#include "Game/Level/HitTier.h"
#include "Game/Level/ImpactOutcome.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/SlamAim.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/HitReaction.h"
#include "Runtime/Object/Reflection/ActorRef.h"
#include "Runtime/Platform/Gamepad.h"

#include <cstdint>

class Player;

namespace NS::Game::Player
{
    class PlayerParams;
}

namespace NS::Obj
{
    class Actor;
    class Body;
    class HitSensor;
} // namespace NS::Obj

namespace NS::Game::Level
{
    //! @brief 配分の計算 ComputeImpactOutcome へ渡す調整値を、params の欄から全部入れて作る
    //! @details ImpactTuning の値の正は PlayerParams 1 つで、ここがその写し先の組み立てを 1 か所で持つ
    //! @param[in] params 自機の調整値の欄
    //! @return params の欄と今の固定ステップの秒を写した調整値
    [[nodiscard]] ImpactTuning MakeImpactTuning(const NS::Game::Player::PlayerParams& params) noexcept;

    //! @brief 当たり 1 回の裁定の内訳
    struct ImpactRecord
    {
        std::uint32_t sequence = 0;
        std::uint32_t targetId = 0;
        float power = 0.0f;
        float charge01 = 0.0f;
        float positionFactor = 0.0f;
        float offset01 = 0.0f;          //!< 面の判定の横ずれ。相手の半幅と自機の半径の和で割った 0..1
        HitTier tier = HitTier::Center; //!< 当たりの段。相手の面の判定で決まる
        float cameraShake = 0.0f;       //!< 揺れの最初の振れの大きさ。横と縦を合わせた長さで、単位は m
        int flashStart = 0;             //!< 白の残りフレーム数の始めの値。白の無い当たりは 0
        float zoomStart = 1.0f;         //!< 寄りの倍率の始めの値。寄りの無い当たりは 1
        float rollStart = 0.0f; //!< 傾きの始めの値 (度)。正は画面の上端をカメラの右へ倒す向き。傾きの無い当たりは 0
        NS::Platform::GamepadVibration padStart; //!< パッドの振動の始めの値。振動の無い当たりは 0
        int hitStopSteps = 0;
        bool centerHit = false; //!< 段が Center の場合 true
        bool broke = false;
        NS::Core::Vector3 selfVelocity; //!< 明けに自機が持つ速度。反動は初速、貫通は減速した突進の速度。単位は m/s
        // 反動の頂点の高さは押し飛ばしの当たりだけが埋める。貫通の当たりは反動しないので 0
        float reboundApexHeight = 0.0f; //!< 自機の反動の、弾かれ始めの高さから頂点までの高さ。単位は m
        // 飛ばす曲線の 3 つの欄は押し飛ばしの当たりだけが埋める。貫通の当たりは相手を飛ばさないので 0
        NS::Core::Vector3 launchVelocity; //!< 相手の曲線の発射の瞬間の速度。単位は m/s
        float launchDistance = 0.0f;      //!< 相手の曲線が発射の高さへ戻るまでに水平に進む距離。単位は m
        float launchApexHeight = 0.0f;    //!< 相手の曲線の、発射の高さから頂点までの高さ。単位は m
        NS::Core::Vector3 impactDir;      //!< 相手の飛ぶ水平の向き。食い込みと振動の向きも同じ
        //! 自機の玉が相手の表面に触れた点。JudgeHitFace の触れる点で、判定できない体の時は相手の体の中心
        NS::Core::Vector3 surfacePoint;
        NS::Core::Vector3 targetPos;
        float targetBottom = 0.0f; //!< 相手の体の外接箱の底の高さ (m)。当たりの粉と照りを置く床
        float targetMass = 1.0f;   //!< 相手の質量。相手が答えた重さ
        bool targetPlaced = true;  //!< 相手が置かれていた (飛んでいなかった) 場合 true
        float launchScale = 0.0f;  //!< 相手の曲線の距離と高さに掛けた比。威力 ÷ 質量の指数乗で、質量 1・威力 1 で 1
        float reboundScale = 0.0f; //!< 自機の反動の高さと距離に掛けた比。威力 × 2 × 質量 ÷ (質量 + 1)
    };

    //! @brief ぶつかった結果を自機側で決める Component
    //! @details Player の観測の段 (ObserveStep) と決定の段 (DecideStep) が、状態と移動の段より前に呼ぶ。
    //! 身体が動く前にその 1 固定ステップの結末を決めるので、
    //! 壁の手前で止められて速度を消された後から結果を推測し直さずに済む
    //! 相手は次の固定ステップの自機のカプセルに重なる体のセンサーから選ぶ。センサーの段の照合を待たず、
    //! HitSensorDirector::FindOverlaps へ先に問う。相手にするのは置物の体の種類だけ。
    //! 選んだ相手には MsgAskTackleTarget で重さと置かれ方を問い、応じた物だけを相手にする
    //! 衝突の瞬間は自機を数固定ステップ止め、止めの頭に MsgTackleFreeze、明けに MsgTackleRelease を相手へ送る
    //! 相手が食い込み・縮み・飛ぶ・壊れるかは相手が決める。相手の部品は触らない
    //! 白の光・カメラの揺れと寄り・パッドの振動は同居する HitReaction へ組んで渡す
    //! 依存: NS::Obj::Body, PlayerParams, HitTier, NS::Obj::HitSensor, NS::Obj::HitReaction
    class ImpactResolver : public NS::Obj::Component
    {
    public:
        ImpactResolver() noexcept;

        //! 持ち主の Player と移動と当たりの演出を引き当てる。持ち主が Player でなければ以後何もしない
        void OnStart() override;

        //! @brief 次の固定ステップで重なる相手を探し、MsgAskTackleTarget に応じた相手と答えを控える
        //! @details 突進中でない時、止めの最中と止めの頭を待つ間は探さない。
        //! 控えた相手は StepState が 1 回だけ使う。
        //! 重なりを探す位置と向かっているかの判定は、次の固定ステップの自機の速度の見込みで見る。
        //! 見込みは持ち主の Player::BodySlamVelocity を読む。身体の実速度は壁へ押し付けられたフレームで 0 に潰れ、
        //! 今の位置から先を探せなくなるので使わない
        void ObserveImpact();
        //! @brief 止めと戻りを 1 フレーム進め、ObserveImpact が控えた相手へ向かっていれば衝突の結果を決める
        //! @details ObserveImpact の後に 1 回だけ効き、2 回目は何もしない。
        //! 止めの最中は止めを数え、明けで相手へ放しを送る
        void StepState();

        //! 直近の更新で反発を検知した場合 true、それ以外の場合は false
        [[nodiscard]] bool DidRebound() const noexcept { return m_didRebound; }

        //! 直近の更新で貫通を検知した場合 true、それ以外の場合は false
        [[nodiscard]] bool DidBreak() const noexcept { return m_didBreak; }

        //! @brief 直近の更新で止めを始めた場合 true、それ以外の場合は false
        //! @details 立つのは検知の次のフレーム (止めの頭) の 1 回だけ。止めが 0 の当たりは止めを通らないので立たない
        [[nodiscard]] bool FreezeBeganThisStep() const noexcept { return m_freezeBeganThisStep; }

        //! @brief 直近の更新で止めが明けた場合 true、それ以外の場合は false
        //! @details 立つのは止めの頭から止めのフレーム数だけ後のフレームの 1 回だけ。止めが 0 の当たりでは立たない
        [[nodiscard]] bool ReleasedThisStep() const noexcept { return m_releasedThisStep; }

        //! 直近の裁定が中心近くで当たった場合 true、それ以外の場合は false
        [[nodiscard]] bool WasCenterHit() const noexcept { return m_wasCenterHit; }

        //! 直近の裁定で読んだ溜め量 0..1
        [[nodiscard]] float LastCharge01() const noexcept { return m_lastCharge01; }

        //! 直近の裁定の当たり位置係数。相手の面で当てはまった決まりの威力の倍率
        [[nodiscard]] float LastPositionFactor() const noexcept { return m_lastPositionFactor; }

        //! 直近の裁定の最終威力
        [[nodiscard]] float LastPower() const noexcept { return m_lastPower; }

        //! 直近の当たりで控えた内訳。まだ当たっていない間は sequence が 0
        [[nodiscard]] const ImpactRecord& LastImpact() const noexcept { return m_lastImpact; }

        //! 白フラッシュの残りフレーム数。出していない場合 0
        [[nodiscard]] int CenterHitFlashStepsRemaining() const noexcept;

        //! @brief 当たりの止めの最中の場合 true、それ以外の場合は false
        //! @details 止めの正はこの部品の数え 1 つ。止めの頭のフレームから明けの 1 つ前のフレームまで真で、
        //! 止めの予約だけが残る検知のフレームは偽。身体を動かしてよいかは Player::CanMoveBody が答える
        [[nodiscard]] bool IsHitStopping() const noexcept { return m_hitStopRemaining > 0; }

        //! @brief 持っている止めと止めの予約と、控えた相手を捨てる
        //! @details 相手へ明けを送らない。止めか予約が残っていた時だけ、同居の HitReaction の白・揺れ・振動を止める。
        //! 潰れと伸びの戻しも捨てる。最後の当たりの記録は残す。
        //! 何度呼んでも同じ結果になる
        void CancelImpact() noexcept;

        //! 止めと予約を捨てる。プレイの途中で外れても止めが次のプレイへ残らない
        void OnEndPlay() override;

        //! @brief 突進の線で最初に触れる相手を探す
        //! @details 相手は置物の体の種類だけで、有効な体のセンサーを持つ物。裁定と同じ絞り。
        //! 自機の当たりの玉
        //! (Player::SlamBallAt。丸まっていれば根の位置、立ち姿なら下の球の位置が中心で、半径は自機の半径) を direction
        //! の水平へ maxDistance 掃き、当たりの裁定と同じく相手の体のセンサーの形に触れるかを見る。
        //! 線から相手の外接箱の中心までの横ずれが、外接箱を線に直交する軸へ投影した半幅と自機の半径の和以内で、
        //! 中心までの線に沿った距離が 0 より大きく、掃いた玉が触れる相手のうち、玉が触れるまでに進む距離が一番短い
        //! 1 体を選ぶ。選んだ相手には MsgAskTackleTarget で面を問い、段と横ずれを裁定と同じ JudgeHitFaceOrWide で出す。
        //! 選んだ相手の赤の高さ (HitFaceAimHeight) へ向ける、溜めて放つ瞬間の縦の速さを LaunchPitch で出す。
        //! 水平の速さは欄「突進速度」、角度の上限は欄「放つ角度の上限」、接地しているかは今の身体の値。
        //! 段を出す玉の中心の高さは、その縦の速さで放った道筋 (LaunchHeightAt) が触れる所で居る高さ。
        //! 応じない相手は外れ・横ずれ 1・縦の速さ 0 とする。
        //! 壁と地形で突進が止まることは見ない
        //! @param[in] direction 線の向き。水平の成分だけを見る
        //! @param[in] maxDistance 線に沿って玉を掃く距離。単位は m
        //! @param[out] outTarget 見つけた相手の予測。見つからない場合は書き換えない
        //! @return 見つかった場合 true。向きの水平の長さが 0 か有限でない場合、見る距離が負か有限でない場合、
        //! 同じ配置物に移動が無い場合と、見つからない場合は false
        [[nodiscard]] bool FindSlamLineTarget(const NS::Core::Vector3& direction,
                                              float maxDistance,
                                              SlamLineTarget& outTarget) const;

        //! 当たりの止めの最中か、明けの伸びから元の形へ戻している途中の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsShapeAnimating() const noexcept { return m_scaleHeld || m_recoverRemaining > 0; }

        //! @brief 当たりで自機の描く形に掛ける倍率を返す
        //! @details 止めの間は進む向きの厚みを欄「潰れの厚み」、縦を欄「潰れの伸び上がり」にした潰れで、
        //! 貫通の止めは潰さない。明けの後は伸びた形から縮む側へ行き過ぎて元の形へ戻る途中の倍率で、
        //! 戻し切ったフレームからはちょうど 1。どちらでもない間も 1。描く形へ書くのは PlayerAppearance で、
        //! ここは何も書かない
        //! @return 元の形を 1 とした世界の x / y / z の倍率
        [[nodiscard]] NS::Core::Vector3 ShapeFactors() const noexcept;

        // 返り方は当てた時の手触りそのもの。プレイ中に Inspector で触って詰められるよう公開する
        NS_REFLECT_NONE(ImpactResolver, NS::Obj::Component)

    private:
        [[nodiscard]] const NS::Game::Player::PlayerParams& Tuning() const noexcept;
        // 次の固定ステップの自機に重なる置物の体のセンサーのうち中心が最も近い 1 つ。無ければ nullptr
        // 事前条件: m_movement が非 null
        [[nodiscard]] NS::Obj::HitSensor* FindOverlapped(const NS::Core::Vector3& predictedVelocity) const;

        // 凍結を掛ける。止めの数えと潰れを立て、当たりの返りを始め、相手へ止めの頭を知らせる
        void BeginFreeze(int stopSteps);

        // 当たり 1 回の返り。揺れの向きと種、寄りと傾きの向きは呼び手が入れる
        struct TierReturns
        {
            int flashSteps = 0; // 白の光のフレーム数
            NS::Obj::CameraShakeDesc shake;
            NS::Obj::CameraZoomRollDesc zoomRoll;
            NS::Obj::HitPadVibration pad;
        };

        // 縦だけの揺れを止めのフレーム数で収める返り。白・寄り・傾き・振動は無い。TierReturnsFor の Center
        // 行と段の外の値が使う
        [[nodiscard]] static TierReturns PlainReturns(float swing, int stopSteps) noexcept;

        // 段ごとに 1 行の表から返りを組む。段を足したら行を足す
        // swing は全段で同じ式の最初の振れの大きさで、段の倍率は行が掛ける
        [[nodiscard]] TierReturns TierReturnsFor(HitTier tier, float swing, int stopSteps) const noexcept;

        // 当たりの返り (白・揺れ・寄りと傾き・振動) を段から組んで控え、記録へ始めの値を書く。検知のフレームに呼ぶ
        // 事前条件: 反動の向き・相手の飛ぶ向き・相手の番号と位置を控え終えている
        void PrepareHitReturns(HitTier tier, float power, float massFactor, float offset01, int stopSteps);

        // 控えた当たりの返りを同居する HitReaction で始める。前の当たりの返りが残っていても、控えた値で始め直す
        void StartHitReturns();

        // 明けの後の伸びの戻しを 1 フレーム進める。形は ShapeFactors が残りのフレーム数から出す
        void AdvanceShapeRecovery() noexcept;

        // 元の形を 1 とした倍率。進行の軸の成分の 2 乗で along を x と z に混ぜ、縦は height
        [[nodiscard]] NS::Core::Vector3 AlongImpactFactors(float along, float height) const noexcept;

        // 止めていた結果を適用する。反発は自機の反動を始め、貫通は速度を書く。相手へ明けを知らせて飛ばすか壊させる
        void ReleaseHitStop();

        int m_freezePendingSteps = 0; // 次のフレームに掛ける凍結のフレーム数。0 は予約なし
        int m_hitStopRemaining = 0;   // 止まっている残りフレーム数。0 は止まっていない
        int m_hitStopTotal = 0;       // 止め始めのフレーム数。振動の減衰の分母
        // 明けたフレームに自機が持つ速度。反動の当たりは、明けに BeginRebound が同じ m_pendingReboundArc から出し直す
        NS::Core::Vector3 m_pendingSelfVelocity{0.0f, 0.0f, 0.0f};
        NS::Game::Player::ReboundArc m_pendingReboundArc{}; // 明けたフレームに自機を弾く反動の向きと高さと距離
        LaunchArc m_pendingLaunchArc{};                     // 明けたフレームに相手を飛ばす曲線
        // 検知のフレームの相手の位置。置かれていた相手は明けたフレームにここへ厳密に戻す
        NS::Core::Vector3 m_pendingTargetHome{0.0f, 0.0f, 0.0f};
        float m_pendingLaunchScale = 0.0f;                      // この衝突の飛ばしの比。明けに相手の尾の長さへ渡す
        HitTier m_pendingTier = HitTier::Center;                // この衝突の段。明けに相手の尾の色へ渡す
        NS::Core::Vector3 m_pendingImpactDir{0.0f, 0.0f, 0.0f}; // 発射の水平方向。食い込みと振動の軸
        float m_pendingShakeAmplitude = 0.0f;                   // この衝突の往復の振れ幅
        int m_pendingFlashSteps = 0;                            // この衝突の白のフレーム数。白の無い段は 0
        NS::Obj::CameraShakeDesc m_pendingShake{};              // この衝突のカメラの揺れ
        NS::Obj::CameraZoomRollDesc m_pendingZoomRoll{};        // この衝突の寄りと傾き。寄りの無い段は倍率 1
        NS::Core::Vector3 m_stretchFactors{1.0f, 1.0f, 1.0f};   // 明けのフレームの伸びの倍率。元の形が 1
        int m_recoverRemaining = 0;                             // 形を戻し切るまでの残りフレーム数
        bool m_scaleHeld = false;                               // 潰した形のまま凍結している最中か
        NS::Obj::ActorRef m_pendingTarget{};                    // 知らせる相手。凍結をまたぐので使うたびに引く
        // 検知のフレームに相手が置かれていたか。記録と当たりの演出が読む
        bool m_pendingTargetPlaced = false;
        NS::Obj::HitPadVibration m_pendingPad{}; // この衝突の振動。振動の無い段は書くフレーム数 0

        bool m_didRebound = false;          // 直近の更新で反発を検知したか
        bool m_didBreak = false;            // 直近の更新で貫通を検知したか
        bool m_freezeBeganThisStep = false; // 直近の更新で BeginFreeze を通ったか
        bool m_releasedThisStep = false;    // 直近の更新で止めの数え下ろしが明けたか
        bool m_pendingBreak = false;        // 保留中の結果が貫通か
        bool m_wasCenterHit = false;
        float m_lastCharge01 = 0.0f;
        float m_lastPositionFactor = 0.0f;
        float m_lastPower = 0.0f;
        ImpactRecord m_lastImpact{};
        NS::Obj::ActorRef m_observedTarget{};
        TackleTargetAnswer m_observedAnswer{};
        NS::Core::Vector3 m_observedVelocity{};
        bool m_hasObservedTarget = false;
        bool m_stateReady = false;
        ::Player* m_player = nullptr;                  // 突進と反動の技の呼び先。非所有
        NS::Obj::Body* m_body = nullptr;               // 同じ配置物の身体。非所有
        NS::Obj::HitReaction* m_hitReaction = nullptr; // 同じ配置物の当たりの演出。非所有
    };
} // namespace NS::Game::Level
