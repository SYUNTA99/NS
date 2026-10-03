#pragma once

#include "Game/Level/HitTier.h"
#include "Game/Level/HitTimeline.h"
#include "Game/Level/ImpactOutcome.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/SlamAim.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/HitReaction.h"
#include "Runtime/Object/Reflection/ActorRef.h"
#include "Runtime/Platform/Gamepad.h"

#include <cstddef>
#include <cstdint>
#include <vector>

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
        //! 面の上の位置から決めた外れの向き。タイムラインの向きの付いた行を選ぶ
        HitDirection direction = HitDirection::Any;
        float faceU = 0.0f;        //!< 段を決めた面の上の左右の位置。自機から見て右が正。判定できない体は 0
        float faceV = 0.0f;        //!< 段を決めた面の上の上下の位置。上が正。判定できない体は 0
        float cameraShake = 0.0f;  //!< 揺れの最初の振れの大きさ。横と縦を合わせた長さで、単位は m
        float cameraTrauma = 0.0f; //!< トラウマの揺れで足すトラウマ。トラウマの揺れの無い当たりは 0
        int flashStart = 0;        //!< 白の残りフレーム数の始めの値。白の無い当たりは 0
        float zoomStart = 1.0f;    //!< 寄りの倍率の始めの値。寄りの無い当たりは 1
        float rollStart = 0.0f;    //!< 傾きの始めの値 (度)。正は画面の上端をカメラの右へ倒す向き。傾きの無い当たりは 0
        NS::Platform::GamepadVibration padStart; //!< パッドの振動の始めの値。振動の無い当たりは 0
        int hitStopSteps = 0;                    //!< 止めのフレーム数。タイムラインが引けない当たりは 0
        bool localStop = false;                  //!< ローカル・ヒットストップ (自機と相手の止め) を使った場合 true
        bool gradualRelease = false;             //!< 段階的な明け (明けの後の遅い世界) を使った場合 true
        bool centerHit = false;                  //!< 段が Center の場合 true
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
    //! 当たりの後の返りは段のタイムライン (HitTimelineLibrary) の事象が決める。検知のフレームを 0 にした時計を
    //! 1 フレームずつ進め、始まりのフレームに来た事象を受け持ちへ渡す。自機の止め・形・反動はここが持ち、
    //! 相手の止めと飛ばしは MsgTackleFreeze と MsgTackleRelease で相手へ渡す
    //! 相手が食い込み・縮み・飛ぶ・壊れるかは相手が決める。相手の部品は触らない
    //! 白の光・カメラの揺れと寄り・パッドの振動の事象は同居する HitReaction で始め、
    //! 当たりと飛びの絵の事象は同居する ImpactEffects へ頼みを置く
    //! 依存: NS::Obj::Body / Collider, PlayerParams, HitTier, NS::Obj::HitSensor, NS::Obj::HitReaction, ImpactEffects
    class ImpactResolver : public NS::Obj::Component
    {
    public:
        ImpactResolver() noexcept;

        //! 持ち主の Player と移動と当たりの演出を引き当てる。持ち主が Player でなければ以後何もしない
        void OnStart() override;

        //! @brief 次の固定ステップで重なる相手を探し、MsgAskTackleTarget に応じた相手と答えを控える
        //! @details 突進中でない時と、検知から止めた自機を動かし直すまでの間は探さない。
        //! 控えた相手は StepState が 1 回だけ使う。
        //! 重なりを探す位置と向かっているかの判定は、次の固定ステップの自機の速度の見込みで見る。
        //! 見込みは持ち主の Player::BodySlamVelocity を読む。身体の実速度は壁へ押し付けられたフレームで 0 に潰れ、
        //! 今の位置から先を探せなくなるので使わない
        void ObserveImpact();
        //! @brief タイムラインの時計を 1 フレーム進め、ObserveImpact が控えた相手へ向かっていれば衝突の結果を決める
        //! @details ObserveImpact の後に 1 回だけ効き、2 回目は何もしない。
        //! 時計が走っている間は、始まりのフレームに来た事象を並びの順に起こす。
        //! 新しい当たりは、走っているタイムラインを打ち切り、同居の HitReaction の返りを止めてから始め直す。
        //! 触れる前 (マイナスのフレーム) の事象を置いた段では、当たらなかった突進のフレームに線の先の相手を予測し、
        //! 検知までのフレーム数で時計をマイナスから始める。予測どおりの相手と段に当たれば同じ時計を 0 から続ける
        void StepState();

        //! 直近の更新で反発を検知した場合 true、それ以外の場合は false
        [[nodiscard]] bool DidRebound() const noexcept { return m_didRebound; }

        //! 直近の更新で貫通を検知した場合 true、それ以外の場合は false
        [[nodiscard]] bool DidBreak() const noexcept { return m_didBreak; }

        //! @brief 直近の更新で止めを始めた場合 true、それ以外の場合は false
        //! @details 立つのは止めの事象の始まりのフレーム (止めの頭) の 1 回だけ。止めの事象の無い当たりでは立たない
        [[nodiscard]] bool FreezeBeganThisStep() const noexcept { return m_freezeBeganThisStep; }

        //! @brief 直近の更新で止めが明けた場合 true、それ以外の場合は false
        //! @details 立つのは止めの事象が終わった次のフレームの 1 回だけ。止めの事象の無い当たりでは立たない
        [[nodiscard]] bool ReleasedThisStep() const noexcept { return m_releasedThisStep; }

        //! @brief 直近の更新で始まった事象の、段のタイムラインのファイルの並びでの番号
        //! @details 始まった順に並ぶ。エディタの下見が、置いた事象と実際に起きたフレームを重ねるのに読む
        [[nodiscard]] const std::vector<std::size_t>& RowsStartedThisStep() const noexcept
        {
            return m_rowsStartedThisStep;
        }

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
        //! @details 止めの正はこの部品の時計 1 つ。止めの事象の始まりから終わりのフレームまで真で、
        //! 止めを待つ検知のフレームは偽。身体を動かしてよいかは Player::CanMoveBody が答える
        [[nodiscard]] bool IsHitStopping() const noexcept;

        //! @brief 止めが明けた後、反動の事象を待って自機を止めている間の場合 true、それ以外の場合は false
        //! @details 反動の事象が止めの終わりより後にあるタイムラインだけが真になる
        [[nodiscard]] bool IsAwaitingRebound() const noexcept;

        //! @brief 走っているタイムラインと、控えた相手を捨てる
        //! @details 相手へ明けを送らない。自機を止めていた時だけ、同居の HitReaction の白・揺れ・振動を止める。
        //! 形も元へ戻す。最後の当たりの記録は残す。
        //! 何度呼んでも同じ結果になる
        void CancelImpact() noexcept;

        //! 走っているタイムラインを捨てる。プレイの途中で外れても止めが次のプレイへ残らない
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

        //! 形の事象の始まりから長さの間の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsShapeAnimating() const noexcept;

        //! @brief 当たりで自機の描く形に掛ける倍率を返す
        //! @details 形の事象の間は、事象の始まりからのフレーム数で引いた曲線の値。突進の向きの倍率は、当たりの水平の
        //! 向きの軸成分の 2 乗で x と z へ混ぜる。点の無い曲線は 1。事象の外はちょうど 1。
        //! 描く形へ書くのは PlayerAppearance で、ここは何も書かない
        //! @return 元の形を 1 とした世界の x / y / z の倍率
        [[nodiscard]] NS::Core::Vector3 ShapeFactors() const noexcept;

        // 返り方は当てた時の手触りそのもの。プレイ中に Inspector で触って詰められるよう公開する
        NS_REFLECT_NONE(ImpactResolver, NS::Obj::Component)

    private:
        [[nodiscard]] const NS::Game::Player::PlayerParams& Tuning() const noexcept;
        // 次の固定ステップの自機に重なる置物の体のセンサーのうち中心が最も近い 1 つ。無ければ nullptr
        // 事前条件: m_movement が非 null
        [[nodiscard]] NS::Obj::HitSensor* FindOverlapped(const NS::Core::Vector3& predictedVelocity) const;

        // 当たりの向きで起きる事象の並びを控え、時計を 0 にして 0 フレームの事象を起こす
        // breakStopSteps は貫通の当たりの止めのフレーム数で、0 以上なら止めの事象の長さの代わりに使う
        // 事前条件: 相手・反動と飛ばしの曲線を控え終えている
        // rows は events のそれぞれの、段のタイムラインのファイルの並びでの番号
        // startEarlyEvents が真なら、マイナスに置いた事象のうち検知のフレームより後まで続く物を、置いたフレームを
        // 始まりにして今起こす。触れる前の時計を続けている時は、それらはもう始まっているので偽を渡す
        void StartTimeline(std::vector<HitEvent> events,
                           std::vector<std::size_t> rows,
                           int breakStopSteps,
                           bool startEarlyEvents);

        // 事象の種類ごとの受け持ち。std::visit で事象の値の種類から呼ぶ
        struct EventRunner;

        // 走っているタイムラインの事象を止め、時計と形を元へ戻す
        void AbortTimeline() noexcept;

        // 控えた相手へ向かっていれば衝突の結果を決め、段のタイムラインを始める。当たりにならなかった場合 false
        [[nodiscard]] bool ResolveObservedHit(bool hadObservation);

        // 突進の間、触れる前の事象を置いた段があれば狙いの線の先の相手を予測し、検知までのフレーム数が最初の
        // マイナスの事象に届いたら、時計をマイナスから始める。予測した相手か段が変わった時と、相手が線から
        // 外れた時は、触れる前に始めた事象を止める
        void UpdateBeforeContact();

        // 触れる前に始めた事象を止め、時計と形を元へ戻す。始めていなければ何もしない
        void DropBeforeContact() noexcept;

        // 時計の今のフレームに始まる事象を並びの順に起こし、止めの明けを数える
        void AdvanceTimeline();

        // 検知のフレームから、止めた自機を動かし直すまでの場合 true
        [[nodiscard]] bool IsHoldingPlayer() const noexcept { return m_holdArmed && !m_holdReleased; }

        // 揺れの事象から、この当たりのカメラの揺れを組む。最初の振れは 強さ × 威力 × 質量の効きで、
        // 横と縦の重みの比で分ける。重みが両方 0 なら揺らさない (フレーム数 0)
        // 事前条件: 反動の向き・威力・質量の効き・揺れの種を控え終えている
        [[nodiscard]] NS::Obj::CameraShakeDesc ShakeDescFor(const CameraShakeEvent& shake, int length) const noexcept;

        // トラウマの揺れの事象から、この当たりのトラウマを組む。量は欄のトラウマ × 威力
        // 一撃は外した側 (面の上の位置) へ振る。面の上の位置が無い当たりは、自機が弾かれる向きの画面の側へ振る
        // 事前条件: 威力・揺れの種・反動の向きを控え終えている
        [[nodiscard]] NS::Obj::CameraTraumaDesc TraumaDescFor(const CameraTraumaEvent& trauma) const noexcept;
        // 沈む揺れの事象から 4 拍の形を作る。深さは威力の頭打ちの曲線、跳ね返りは反動の事象の始まりから
        [[nodiscard]] NS::Obj::CameraSinkDesc SinkDescFor(const CameraSinkEvent& sink,
                                                          const HitEvent& event) const noexcept;

        // 寄りの事象から、この当たりの寄りと傾きを組む。長さのうち末尾の戻すフレーム数を除いた間を保つ
        // 事前条件: 相手の飛ぶ向きを控え終えている
        [[nodiscard]] NS::Obj::CameraZoomRollDesc ZoomRollDescFor(const ZoomRollEvent& zoomRoll,
                                                                  int length) const noexcept;

        // 検知のフレームに、返りの事象の種類ごとに最初の 1 つから始めの値を記録へ写す。事象の無い返りは無い時の値
        // 事前条件: ShakeDescFor と ZoomRollDescFor の事前条件と同じ
        void RecordReturns(const std::vector<HitEvent>& events);

        // 元の形を 1 とした倍率。進行の軸の成分の 2 乗で along を x と z に混ぜ、縦は height
        [[nodiscard]] NS::Core::Vector3 AlongImpactFactors(float along, float height) const noexcept;

        // 反発は自機の反動を始め、貫通は速度を書く。止めていた自機を動かし直す
        void ApplyRebound();

        // 相手へ明けを知らせて飛ばすか壊させる
        void LaunchTarget();

        std::vector<HitEvent> m_events;                 // 走っているタイムラインのうち、当たりの向きで起きる事象
        std::vector<std::size_t> m_eventRows;           // m_events と同じ並びで、ファイルの並びでの番号
        std::vector<std::size_t> m_rowsStartedThisStep; // 直近の更新で始まった事象のファイルの並びでの番号
        int m_clock = 0;                                // 検知のフレームを 0 にした今のフレーム
        int m_clockEnd = 0;                             // 最後の事象が終わる時計の値。ここまで進めたら時計を止める
        bool m_clockRunning = false;                    // 時計が走っているか
        bool m_holdArmed = false;                       // この当たりで自機を止めるか。止めの事象のある当たりで立つ
        bool m_holdReleased = false;                    // 止めた自機を動かし直したか
        bool m_hasReboundEvent = false;                 // 走っているタイムラインに反動の事象があるか
        bool m_stopStarted = false;                     // 止めの事象が始まったか
        bool m_startedGradualRelease = false;           // 段階的な明けで世界を遅くしたか。打ち切りで普段の速さへ戻す
        bool m_heldOthers = false;                      // 自機以外の止めを置いたか。打ち切りで解く
        int m_stopEnd = 0;                              // 止めの事象の最後のフレーム
        int m_breakStopSteps = -1;                      // 貫通の当たりの止めのフレーム数。負なら止めの事象の長さのまま
        ShapeEvent m_shape{};                           // 走っている形の事象
        int m_shapeStart = 0;                           // 形の事象の始まりのフレーム
        int m_shapeLength = 0;                          // 形の事象の長さ
        bool m_shapeActive = false;                     // 形の事象が始まったか
        bool m_beforeContact = false;                   // 時計が触れる前 (マイナスのフレーム) を進めているか
        NS::Obj::ActorRef m_beforeContactTarget{};      // 触れる前の時計を始めた予測の相手
        HitTier m_beforeContactTier = HitTier::Center;  // 触れる前の時計を始めた予測の段
        float m_pendingTargetMass = 1.0f;               // 検知のフレームに相手が答えた質量。往復の振れ幅を割る
        // 明けたフレームに自機が持つ速度。反動の当たりは、明けに BeginRebound が同じ m_pendingReboundArc から出し直す
        NS::Core::Vector3 m_pendingSelfVelocity{0.0f, 0.0f, 0.0f};
        NS::Game::Player::ReboundArc m_pendingReboundArc{}; // 明けたフレームに自機を弾く反動の向きと高さと距離
        LaunchArc m_pendingLaunchArc{};                     // 明けたフレームに相手を飛ばす曲線
        // 検知のフレームに相手が答えた位置。記録の targetPos と揺れの種に使う
        // 置かれていた相手を元の位置へ戻すのは相手自身
        NS::Core::Vector3 m_pendingTargetPosition{0.0f, 0.0f, 0.0f};
        float m_pendingLaunchScale = 0.0f;                      // この衝突の飛ばしの比。明けに相手の尾の長さへ渡す
        HitTier m_pendingTier = HitTier::Center;                // この衝突の段。明けに相手の尾の色へ渡す
        NS::Core::Vector3 m_pendingImpactDir{0.0f, 0.0f, 0.0f}; // 発射の水平方向。食い込みと振動の軸
        NS::Obj::ActorRef m_pendingTarget{};                    // 知らせる相手。凍結をまたぐので使うたびに引く
        // 検知のフレームに相手が置かれていたか。記録と当たりの演出が読む
        bool m_pendingTargetPlaced = false;
        float m_pendingPower = 0.0f;          // この衝突の威力。揺れの最初の振れに掛ける
        float m_pendingMassFactor = 0.0f;     // この衝突の質量の効き。揺れの最初の振れに掛ける
        std::uint32_t m_pendingShakeSeed = 0; // この衝突の揺れの、入れ替わりの間隔を選ぶ種

        bool m_didRebound = false;          // 直近の更新で反発を検知したか
        bool m_didBreak = false;            // 直近の更新で貫通を検知したか
        bool m_freezeBeganThisStep = false; // 直近の更新で止めの事象が始まったか
        bool m_releasedThisStep = false;    // 直近の更新が止めの事象の終わりの次のフレームだったか
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
