#pragma once

#include "Game/Level/HitTier.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/HitReaction.h"
#include "Runtime/Object/Reflection/ActorRef.h"
#include "Runtime/Platform/Gamepad.h"

#include <cstdint>

namespace NS::Game::Player
{
    class PlayerParams;
}

namespace NS::Obj
{
    class Actor;
    class HitSensor;
} // namespace NS::Obj

namespace NS::Game::Level
{
    class CollisionInput;

    //! @brief 当たり 1 回の裁定の内訳
    struct ImpactRecord
    {
        std::uint32_t sequence = 0;
        std::uint32_t targetId = 0;
        float power = 0.0f;
        float charge01 = 0.0f;
        float positionFactor = 0.0f;
        float offset01 = 0.0f;          //!< 相手の中心からの横ずれ。相手の半幅と自機の半径の和で割った 0..1
        HitTier tier = HitTier::Center; //!< 当たりの段。CollisionInput が無い時は Center だが演出は掛けない
        float cameraShake = 0.0f;       //!< 揺れの最初の振れの大きさ。横と縦を合わせた長さで、単位は m
        int flashStart = 0;             //!< 白の残りフレーム数の始めの値。白の無い当たりは 0
        float zoomStart = 1.0f;         //!< 寄りの倍率の始めの値。寄りの無い当たりは 1
        float rollStart = 0.0f; //!< 傾きの始めの値 (度)。正は画面の上端をカメラの右へ倒す向き。傾きの無い当たりは 0
        NS::Platform::GamepadVibration padStart; //!< パッドの振動の始めの値。振動の無い当たりは 0
        int hitStopSteps = 0;
        bool centerHit = false; //!< 白の光と止めの倍率を掛けた場合 true。CollisionInput が無い時は false
        bool broke = false;
        NS::Core::Vector3 selfVelocity; //!< 明けに自機が持つ速度。反動は初速、貫通は減速した突進の速度。単位は m/s
        // 反動の頂点の高さは押し飛ばしの当たりだけが埋める。貫通の当たりは反動しないので 0
        float reboundApexHeight = 0.0f; //!< 自機の反動の、弾かれ始めの高さから頂点までの高さ。単位は m
        // 飛ばす曲線の 3 つの欄は押し飛ばしの当たりだけが埋める。貫通の当たりは相手を飛ばさないので 0
        NS::Core::Vector3 launchVelocity; //!< 相手の曲線の発射の瞬間の速度。単位は m/s
        float launchDistance = 0.0f;      //!< 相手の曲線が発射の高さへ戻るまでに水平に進む距離。単位は m
        float launchApexHeight = 0.0f;    //!< 相手の曲線の、発射の高さから頂点までの高さ。単位は m
        NS::Core::Vector3 impactDir;      //!< 相手の飛ぶ水平の向き。食い込みと振動の向きも同じ
        NS::Core::Vector3 targetPos;
        float targetBottom = 0.0f; //!< 相手の体の外接箱の底の高さ (m)。当たりの粉と照りを置く床
        float targetMass = 1.0f;   //!< 相手の質量。相手が答えた重さ
        bool targetPlaced = true;  //!< 相手が置かれていた (飛んでいなかった) 場合 true
        float launchScale = 0.0f;  //!< 相手の曲線の距離と高さに掛けた比。威力 ÷ 質量の指数乗で、質量 1・威力 1 で 1
        float reboundScale = 0.0f; //!< 自機の反動の高さと距離に掛けた比。威力 × 2 × 質量 ÷ (質量 + 1)
        //! 惜しい当たりの寄りと振動を保つフレーム数。止めの頭から数え、このフレームから引き始める。他の段は 0
        int pullBackFrames = 0;
    };

    //! @brief 突進の線で最初に触れる相手の予測
    struct SlamLineTarget
    {
        NS::Obj::ActorRef target{};  //!< 相手の配置物
        NS::Core::AABB bounds{};     //!< 相手の当たりの外接箱。世界座標
        NS::Core::Vector3 origin;    //!< 探した時の自機の位置。世界座標
        NS::Core::Vector3 direction; //!< 探した水平の向き。正規化済みで y は 0
        float along = 0.0f;          //!< 自機の位置から相手の外接箱の中心までの、線に沿った水平の距離。単位は m
        float offset = 0.0f; //!< 線から相手の中心までの横ずれ。相手の半幅と自機の半径の和で割った比で、0 以上 1 以下
        //! 線を進む自機の当たりの玉が相手の当たりの形に初めて触れるまでに、玉の中心が線に沿って進む距離。単位は m。
        //! 1 mm の幅で、触れている側へ丸める
        float contact = 0.0f;
    };

    //! @brief ぶつかった結果を自機側で決める Component
    //! @details Player::Update が状態と移動より前に呼ぶ。
    //! PlayerComponent が動く前にその 1 固定ステップの結末を決めるので、
    //! 壁の手前で止められて速度を消された後から結果を推測し直さずに済む
    //! 相手は次の固定ステップの自機のカプセルに重なる物の体のセンサーから選ぶ。調べる種類はプレイヤーの体当たりの
    //! 組み合わせの表に従う。選んだ相手には MsgAskTackleTarget で重さと置かれ方を問い、応じた物だけを相手にする
    //! 衝突の瞬間は自機を数固定ステップ止め、止めの頭に MsgTackleFreeze、明けに MsgTackleRelease を相手へ送る
    //! 相手が食い込み・縮み・飛ぶ・壊れるかは相手が決める。相手の部品は触らない
    //! 白の光・カメラの揺れと寄り・パッドの振動は同居する HitReaction へ組んで渡す
    //! 依存: NS::Game::Player::PlayerComponent, CollisionInput, HitTier, NS::Obj::HitSensor, NS::Obj::HitReaction
    class ImpactResolver : public NS::Obj::Component
    {
    public:
        ImpactResolver() noexcept;

        //! 同じ配置物の移動と体当たりの入力と当たりの演出を引き当てる。移動が無ければ以後何もしない
        void OnStart() override;

        //! この固定ステップで重なる壊せる物を探し、向かっていれば止めてから破壊するか、反発と押し飛ばしを与える
        void OnUpdate() override;
        //! @brief 次の固定ステップで重なる相手を探し、MsgAskTackleTarget に応じた相手と答えを控える
        //! @details 突進中でない時、止めの最中と止めの頭を待つ間は探さない。
        //! 控えた相手は StepState が 1 回だけ使う
        //! @param[in] predictedVelocity 次の固定ステップの自機の速度の見込み。
        //! 重なりを探す位置と、向かっているかの判定に使う
        void ObserveImpact(const NS::Core::Vector3& predictedVelocity);
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

        //! 直近の裁定の当たり位置係数
        [[nodiscard]] float LastPositionFactor() const noexcept { return m_lastPositionFactor; }

        //! 直近の裁定の最終威力
        [[nodiscard]] float LastPower() const noexcept { return m_lastPower; }

        //! 直近の当たりで控えた内訳。まだ当たっていない間は sequence が 0
        [[nodiscard]] const ImpactRecord& LastImpact() const noexcept { return m_lastImpact; }

        //! 白フラッシュの残りフレーム数。出していない場合 0
        [[nodiscard]] int CenterHitFlashStepsRemaining() const noexcept;

        //! 凍結の途中で外れても移動を止めたままにしない
        void OnEndPlay() override;

        //! @brief 突進の向きを寄せる相手を探す
        //! @details 相手はプレイヤーの体当たりが調べる種類の、有効な体のセンサーを持つ物。裁定と同じ絞り。
        //! 自機の位置から外接箱の中心への水平の向きが forward から coneDegrees 以内で、
        //! 水平の距離が maxDistance 以内の相手のうち、preferred が居ればそれを、居なければ一番近い 1 体を選ぶ
        //! @param[in] forward 基準の向き。水平の成分だけを見る
        //! @param[in] coneDegrees 基準の向きから片側に見る角度。単位は度
        //! @param[in] maxDistance 見る水平の距離。単位は m
        //! @param[out] outCenter 見つけた相手の外接箱の中心。見つからない場合は書き換えない
        //! @param[in] preferred 角度と距離の内に居れば、一番近い相手より先に選ぶ相手。未設定なら一番近い相手を選ぶ
        //! @return 見つかった場合 true、それ以外の場合は false
        [[nodiscard]] bool FindHomingTarget(const NS::Core::Vector3& forward,
                                            float coneDegrees,
                                            float maxDistance,
                                            NS::Core::Vector3& outCenter,
                                            NS::Obj::ActorRef preferred = NS::Obj::ActorRef{}) const;

        //! @brief 突進の線で最初に触れる相手を探す
        //! @details 相手の絞りは FindHomingTarget と同じ。自機の当たりの玉 (丸まっていれば根の位置、立ち姿なら下の球の
        //! 位置が中心で、半径は自機の半径) を direction の水平へ maxDistance 掃き、当たりの裁定と同じく
        //! 相手の体のセンサーの形に触れるかを見る。
        //! 線から相手の外接箱の中心までの横ずれが、外接箱を線に直交する軸へ投影した半幅と自機の半径の和以内で、
        //! 中心までの線に沿った距離が 0 より大きく、掃いた玉が触れる相手のうち、玉が触れるまでに進む距離が一番短い
        //! 1 体を選ぶ。横ずれの比は当たりの裁定と同じ式で出し、裁定はそれを 0〜1 に丸めて使う。
        //! 壁と地形で突進が止まることは見ない
        //! @param[in] direction 線の向き。水平の成分だけを見る
        //! @param[in] maxDistance 線に沿って玉を掃く距離。単位は m
        //! @param[out] outTarget 見つけた相手の予測。見つからない場合は書き換えない
        //! @return 見つかった場合 true。向きの水平の長さが 0 か有限でない場合、見る距離が負か有限でない場合、
        //! 同じ配置物に移動が無い場合と、見つからない場合は false
        [[nodiscard]] bool FindSlamLineTarget(const NS::Core::Vector3& direction,
                                              float maxDistance,
                                              SlamLineTarget& outTarget) const;

        //! 潰した形で凍結中か、伸びから元の形へ戻している途中の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsScaleAnimating() const noexcept { return m_scaleHeld || m_recoverRemaining > 0; }

        // 返り方は当てた時の手触りそのもの。プレイ中に Inspector で触って詰められるよう公開する
        NS_REFLECT_NONE(ImpactResolver, NS::Obj::Component)

    private:
        [[nodiscard]] const NS::Game::Player::PlayerParams& Tuning() const noexcept;
        // 次の固定ステップの自機に重なる体のセンサーのうち中心が最も近い 1 つ。無ければ nullptr
        // 事前条件: m_movement が非 null
        [[nodiscard]] NS::Obj::HitSensor* FindOverlapped(const NS::Core::Vector3& predictedVelocity) const;

        // 凍結を掛ける。自機を寝かせて潰し、当たりの返りを始め、相手へ止めの頭を知らせる
        void BeginFreeze(int stopSteps);

        // 当たりの返り (白・揺れ・寄りと傾き・振動) を段から組んで控え、記録へ始めの値を書く。検知のフレームに呼ぶ
        // 事前条件: 反動の向き・相手の飛ぶ向き・相手の番号と位置を控え終えている
        void PrepareHitReturns(HitTier tier, bool tiered, float power, float massFactor, float offset01, int stopSteps);

        // 控えた当たりの返りを同居する HitReaction で始める。前の当たりの返りが残っていても、控えた値で始め直す
        void StartHitReturns();

        // 解放後のフレームで伸びた形から戻す。前半で縮む側へ行き過ぎ、後半で配置で決めた元の形へ戻る
        // 最後のフレームは控えた値を厳密に書く
        void RecoverScale();

        // 進行の軸だけ倍率を効かせた描画スケールを作る。縦は別の倍率で受ける
        [[nodiscard]] NS::Core::Vector3 ScaledAlongImpact(float along, float height) const noexcept;

        // 元の形を 1 とした倍率。進行の軸の成分の 2 乗で along を x と z に混ぜ、縦は height
        [[nodiscard]] NS::Core::Vector3 AlongImpactFactors(float along, float height) const noexcept;

        // 止めていた結果を適用する。反発は自機の反動を始め、貫通は速度を書く。相手へ明けを知らせて飛ばすか壊させる
        void ReleaseHitStop();

        // 秒をフレーム数へ換算して 0 から MaxHitStopSteps までに丸める
        [[nodiscard]] int SecondsToSteps(float seconds) const noexcept;

        // 上限秒をフレーム数へ換算する。非有限と 0 以下は 0 で、止めない
        [[nodiscard]] int MaxHitStopSteps() const noexcept;

        // 最終威力と質量から止めるフレーム数を出す。0 なら止めない
        [[nodiscard]] int ComputeHitStopSteps(float power, float mass, float hitStopScale) const noexcept;

        // 質量 1 の物に威力 1 で当てた時、自機が弾かれ始めの高さから上がる頂点の高さ (m)

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
        NS::Core::Vector3 m_scaleHome{1.0f, 1.0f, 1.0f};        // 配置で決めた元の描画スケールの控え
        NS::Core::Vector3 m_stretchScale{1.0f, 1.0f, 1.0f};     // 解放のフレームの伸びた形
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
        NS::Game::Player::PlayerComponent* m_movement = nullptr; // 同じ配置物の移動。非所有
        CollisionInput* m_collisionInput = nullptr;
        NS::Obj::HitReaction* m_hitReaction = nullptr; // 同じ配置物の当たりの演出。非所有
    };
} // namespace NS::Game::Level
