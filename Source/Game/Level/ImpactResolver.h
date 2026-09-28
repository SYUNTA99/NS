#pragma once

#include "Game/Level/HitTier.h"
#include "Game/Level/LaunchedBody.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/CameraBrain.h"
#include "Runtime/Object/Components/OverlayRenderer.h"
#include "Runtime/Object/Reflection/ObjectRef.h"
#include "Runtime/Platform/Gamepad.h"

#include <cstdint>

namespace NS::Obj
{
    class GameObject;
} // namespace NS::Obj

namespace NS::Game::Level
{
    class Breakable;
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
        float targetMass = 1.0f;   //!< 相手の質量。RigidBody が無ければ 1
        bool targetPlaced = true;  //!< 相手が置かれていた (飛んでいなかった) 場合 true
        float launchScale = 0.0f;  //!< 相手の曲線の距離と高さに掛けた比。威力 ÷ 質量の指数乗で、質量 1・威力 1 で 1
        float reboundScale = 0.0f; //!< 自機の反動の高さと距離に掛けた比。威力 × 2 × 質量 ÷ (質量 + 1)
        //! 惜しい当たりの寄りと振動を保つフレーム数。止めの頭から数え、このフレームから引き始める。他の段は 0
        int pullBackFrames = 0;
    };

    //! @brief 突進の線で最初に触れる相手の予測
    struct SlamLineTarget
    {
        NS::Obj::ObjectRef target{}; //!< 相手の配置物
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
    //! @details 帯は Update より前。PlayerComponent が動く前にその 1 固定ステップの結末を決めるので、
    //! 壁の手前で止められて速度を消された後から結果を推測し直さずに済む
    //! 相手は PhysicsScene::OverlapCapsule で重なった body を集め、ObjectList::ForEachComponent で回した
    //! Breakable の body と照合して決める。NS::Phys は NS::Obj を知らないので、
    //! body から持ち主を引く関数は無い
    //! 衝突の瞬間は自機を数固定ステップ止め、反発・発射・破壊を明けたフレームへ保留する
    //! 反発の止めの間は、置かれていた相手の描く形を MeshRenderer の描く時だけの倍率で縮め、明けに戻す
    //! 重さは相手の RigidBody の質量で、RigidBody が無ければ 1
    //! 依存: NS::Game::Player::PlayerComponent, Breakable, LaunchedBody, NS::Obj::RigidBody, NS::Obj::MeshRenderer,
    //! CollisionInput, HitTier, NS::Platform::Input
    class ImpactResolver : public NS::Obj::OverlayRenderer
    {
    public:
        ImpactResolver() noexcept;

        //! 同じ配置物の移動と体当たりの入力を引き当てる。移動が無ければ以後何もしない
        void OnStart() override;

        //! この固定ステップで重なる壊せる物を探し、向かっていれば止めてから破壊するか、反発と押し飛ばしを与える
        void OnUpdate() override;

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
        [[nodiscard]] int CenterHitFlashStepsRemaining() const noexcept { return m_centerHitFlashRemaining; }

        //! 凍結の途中で外れても移動を止めたままにせず、縮めた相手の描く形も元へ戻す
        void OnEndPlay() override;

        //! 中心近くの当たりの止めの頭から、フレームごとに減衰する白を画面全体へ重ねる
        void OnRenderOverlay(const NS::Gfx::RenderContext& ctx) override;

        //! @brief 突進の向きを寄せる相手を探す
        //! @details 相手は壊せる物のうち、有効で、トリガの箱でなく、当たりの外接箱が取れる物。裁定と同じ絞り。
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
                                            NS::Obj::ObjectRef preferred = NS::Obj::ObjectRef{}) const;

        //! @brief 突進の線で最初に触れる相手を探す
        //! @details 相手の絞りは FindHomingTarget と同じ。自機の当たりの玉 (丸まっていれば根の位置、立ち姿なら下の球の
        //! 位置が中心で、半径は自機の半径) を direction の水平へ maxDistance 掃き、当たりの裁定と同じ
        //! PhysicsScene::OverlapCapsule で相手の body の実物の形に触れるかを見る。
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
        NS_REFLECT_BEGIN(ImpactResolver, NS::Obj::OverlayRenderer)
        NS_REFLECT_FIELD(m_reboundApexHeight, "反動の高さ")
        NS_REFLECT_FIELD(m_reboundDistance, "反動の距離")
        NS_REFLECT_FIELD(m_centerHitReboundDistanceScale, "中心近くの当たりの反動の距離の倍率")
        NS_REFLECT_FIELD(m_launchDistance, "押し飛ばしの距離")
        NS_REFLECT_FIELD(m_launchMassExponent, "押し飛ばしの質量指数")
        NS_REFLECT_FIELD(m_launchApexHeight, "押し飛ばしの高さ")
        NS_REFLECT_FIELD(m_launchFallGravityScale, "下りの速さの倍率")
        NS_REFLECT_FIELD(m_launchApexBandSpeed, "頂点の帯の縦速度")
        NS_REFLECT_FIELD(m_launchApexBandGravityScale, "頂点の帯の重力倍率")
        NS_REFLECT_FIELD(m_hitStopBaseSeconds, "ヒットストップ基準秒")
        NS_REFLECT_FIELD(m_centerHitStopScale, "中心近くの当たりのヒットストップ倍率")
        NS_REFLECT_FIELD(m_hitStopMaxSeconds, "ヒットストップの上限秒")
        NS_REFLECT_FIELD(m_pushInDistance, "食い込み距離")
        NS_REFLECT_FIELD(m_shakeAmplitude, "振動の振幅")
        NS_REFLECT_FIELD(m_cameraShakeScale, "カメラ揺れの強さ")
        NS_REFLECT_FIELD(m_centerHitShakeScale, "中心近くの当たりの揺れの倍率")
        NS_REFLECT_FIELD(m_wideShakeFrames, "大きな外れの揺れのフレーム数")
        NS_REFLECT_FIELD(m_wideShakeUpOverSide, "大きな外れの揺れの縦と横の比")
        NS_REFLECT_FIELD(m_wideShakeLongestFlipFrames, "大きな外れの揺れの入れ替わりの最長フレーム数")
        NS_REFLECT_FIELD(m_centerHitZoom, "中心近くの当たりの寄りの倍率")
        NS_REFLECT_FIELD(m_centerHitRollDegrees, "中心近くの当たりの傾き")
        NS_REFLECT_FIELD(m_zoomRollReturnFrames, "寄りと傾きを戻すフレーム数")
        NS_REFLECT_FIELD(m_nearHitReturnRatio, "惜しい当たりの返りの割合")
        NS_REFLECT_FIELD(m_nearHitPullBackRatio, "惜しい当たりの返りを引き始める割合")
        NS_REFLECT_FIELD(m_centerHitPadStrength, "中心近くの当たりのパッドの振動の強さ")
        NS_REFLECT_FIELD(m_widePadStrength, "大きな外れのパッドの振動の強さ")
        NS_REFLECT_FIELD(m_squashThickness, "潰れの厚み")
        NS_REFLECT_FIELD(m_squashHeight, "潰れの伸び上がり")
        NS_REFLECT_FIELD(m_stretchAlong, "弾け伸びの倍率")
        NS_REFLECT_FIELD(m_stretchOvershoot, "弾け伸びの行き過ぎ")
        NS_REFLECT_FIELD(m_stretchRecoverSteps, "弾け伸びを戻すフレーム数")
        NS_REFLECT_FIELD(m_centerHitFlashAlpha, "中心近くの当たりの白の濃さ")
        NS_REFLECT_FIELD(m_centerHitFlashSteps, "中心近くの当たりの白のフレーム数")
        NS_REFLECT_FIELD(m_breakEnabled, "破壊を許可")
        NS_REFLECT_FIELD(m_breakSpeedScale, "貫通時の減速倍率")
        NS_REFLECT_FIELD(m_breakStopSeconds, "貫通の止め秒")
        NS_REFLECT_FIELD(m_markProbeDistance, "跡の床探しの距離")
        NS_REFLECT_END()

    private:
        // 重なっている壊せる物のうち中心が最も近い 1 体。無ければ nullptr
        // 事前条件: m_movement が非 null
        [[nodiscard]] Breakable* FindOverlapped() const;

        // 凍結を掛ける。自機を寝かせて潰し、当たりの返りを始め、置かれていた相手を食い込ませて描く形を縮める
        void BeginFreeze(int stopSteps);

        // 当たりの返り (白・揺れ・寄りと傾き・振動) を段から組んで控え、記録へ始めの値を書く。検知のフレームに呼ぶ
        // 事前条件: 反動の向き・相手の飛ぶ向き・相手の番号と位置を控え終えている
        void PrepareHitReturns(HitTier tier, bool tiered, float power, float massFactor, float offset01, int stopSteps);

        // 控えた当たりの返りを始める。前の当たりの返りが残っていても、控えた値で始め直す
        void StartHitReturns();

        // 振動を始めてからのフレーム数に応じた速さをパッドへ書く。書くフレーム数に届いたフレームは 0 を書いて止める
        void WritePadVibration();

        // 解放後のフレームで伸びた形から戻す。前半で縮む側へ行き過ぎ、後半で配置で決めた元の形へ戻る
        // 最後のフレームは控えた値を厳密に書く
        void RecoverScale();

        // 進行の軸だけ倍率を効かせた描画スケールを作る。縦は別の倍率で受ける
        [[nodiscard]] NS::Core::Vector3 ScaledAlongImpact(float along, float height) const noexcept;

        // 元の形を 1 とした倍率。進行の軸の成分の 2 乗で along を x と z に混ぜ、縦は height
        [[nodiscard]] NS::Core::Vector3 AlongImpactFactors(float along, float height) const noexcept;

        // 置かれていた相手の描く形を、自機の潰れと同じ倍率で突進の向きに縮める。描く形の無い相手には何もしない
        void ShrinkPlacedTarget(NS::Obj::GameObject& target);

        // 縮めた相手の描く形を元の形へ戻す。縮めていない時と、相手が消えていた時は何もしない
        void RestoreTargetShape();

        // 止めていた結果を適用する。反発は自機の反動を始めて相手を発射する。貫通は速度を書いて破壊する
        void ReleaseHitStop();

        // 秒をフレーム数へ換算して 0 から MaxHitStopSteps までに丸める
        [[nodiscard]] int SecondsToSteps(float seconds) const noexcept;

        // 上限秒をフレーム数へ換算する。非有限と 0 以下は 0 で、止めない
        [[nodiscard]] int MaxHitStopSteps() const noexcept;

        // 凍結中のフレームで、置かれていた相手を発射軸に沿って食い込み位置の周りで往復させる
        // 見せるための動きで、明けたフレームに元位置へ戻す
        // 飛んでいる相手は物理が根を書くので触らない
        void ApplyFreezeVibration();

        // 最終威力と質量から止めるフレーム数を出す。0 なら止めない
        [[nodiscard]] int ComputeHitStopSteps(float power, float mass, float hitStopScale) const noexcept;

        // 質量 1 の物に威力 1 で当てた時、自機が弾かれ始めの高さから上がる頂点の高さ (m)
        float m_reboundApexHeight = 1.15f;
        // 質量 1 の物に威力 1 で当てた時、自機が弾かれ始めの高さへ戻るまでに水平に進む距離 (m)
        // 中心近くの当たりは倍率を掛ける
        float m_reboundDistance = 0.575f;
        // 中心近くの当たりの反動の距離に掛ける倍率。高さには掛けないので、真ん中に当てた時は弾かれ始めが後ろへ倒れる
        float m_centerHitReboundDistanceScale = 2.0f;
        float m_launchDistance = 29.0f;     // 質量 1 の物に威力 1 で当てた時、発射の高さへ戻るまでに水平に飛ぶ距離 (m)
        float m_launchMassExponent = 0.35f; // 押し飛ばしの距離と高さを割る質量の指数。1 で反比例、0 で質量を見ない
        float m_launchApexHeight = 2.0f;    // 質量 1 の物に威力 1 で当てた時の、発射の高さから頂点までの高さ (m)
        // 飛ばした物の曲線の形。上りは既定の重力で減速する
        float m_launchFallGravityScale = 1.4f;     // 下りの重力 ÷ 上りの重力
        float m_launchApexBandSpeed = 1.0f;        // 頂点の帯の縦速度 (m/s)
        float m_launchApexBandGravityScale = 0.5f; // 頂点の帯の間に重力へ掛ける倍率
        // 既定の固定ステップ (1/60 秒) の 4 フレームぶん
        float m_hitStopBaseSeconds = 4.0f / 60.0f; // 質量 1 へ通常速度で当てた時に止める秒
        float m_centerHitStopScale =
            2.0f; // 威力の伸び (最大 2 倍) と掛けて、素と中心近くの当たりの止まりを 4 倍差にする
        // 止める長さの上限。0.2 秒より長い停止は衝突の重さではなく処理落ちに見える
        float m_hitStopMaxSeconds = 12.0f / 60.0f;
        float m_pushInDistance = 0.06f;       // 凍結の頭で置かれていた相手を発射方向へ食い込ませる距離
        float m_shakeAmplitude = 0.05f;       // 凍結中の往復の振れ幅。質量 1 で半分になる
        float m_cameraShakeScale = 0.06f;     // 威力 1・質量因子 1 の当たりのカメラ揺れの最初の振れの大きさ (m)
        float m_centerHitShakeScale = 1.25f;  // 中心近くの当たりの最初の振れの大きさに掛ける倍率
        int m_wideShakeFrames = 16;           // 大きな外れの揺れを描くフレーム数。止めの頭を含む
        float m_wideShakeUpOverSide = 0.35f;  // 大きな外れの最初の振れの縦 ÷ 横
        int m_wideShakeLongestFlipFrames = 3; // 大きな外れの揺れの向きが入れ替わるまでの最長フレーム数
        float m_centerHitZoom = 1.15f;        // 中心近くの当たりで画面に写る大きさの倍率
        float m_centerHitRollDegrees = 3.0f;  // 中心近くの当たりの視線の軸まわりの傾き (度)
        int m_zoomRollReturnFrames = 6;       // 寄りと傾きを元へ戻すフレーム数
        float m_nearHitReturnRatio = 0.4f;    // 惜しい当たりの寄りの倍率の 1 を超えた分と傾きに掛ける割合
        float m_nearHitPullBackRatio = 0.5f;  // 惜しい当たりの寄りと傾きを保つフレーム数 ÷ 止めのフレーム数
        float m_centerHitPadStrength = 1.0f;  // 中心近くの当たりの低い周波数のモーターの始めの速さ。0〜1
        float m_widePadStrength = 0.6f;       // 大きな外れの当たりの高い周波数のモーターの始めの速さ。0〜1
        float m_squashThickness = 0.7f;       // 凍結中の自機と置かれていた相手の、進行方向の厚みの倍率
        float m_squashHeight = 1.1f;          // 凍結中の自機と置かれていた相手の、高さの倍率
        float m_stretchAlong = 1.2f;          // 解放のフレームの伸びの倍率。反発は縦、貫通は進行の軸
        // 止めの潰れ → 明けの伸び → 行き過ぎ → 元の玉を、続けて 1 つの弾む動きに見せる
        // 0.5 は縦 0.9 まで縮む。0.25 (縦 0.95) では揺れに見え、1.0 (縦 0.8) は止めの潰れに近く 2 回目の衝突に見える
        float m_stretchOvershoot = 0.5f; // 伸びの量に対する、戻る途中で縮む側へ行き過ぎる量の割合
        // 伸びから行き過ぎを経て元の形へ戻すフレーム数。前半で縮む側へ行き過ぎ、後半で戻る
        // 6 は溜めきり・質量 1 の反動の上り約 40 フレームの最初の 15%
        // 弾け出しの間だけ形を動かし、残りの上りは元の玉で浮かせる
        int m_stretchRecoverSteps = 6;
        // 中心近くで当てた時だけの白フラッシュ。端で当てた時と見間違えない強さにする
        // 0.5 は一瞬白と分かる濃さ。1.0 だと食い込みと潰れの絵が隠れる
        float m_centerHitFlashAlpha = 0.5f;
        // 白は潰れと同じ止めの頭から出る
        // 2 フレームなら白が重なるのは潰れの最初の 2 フレームだけで、2 フレーム目の濃さは半分
        // 8 フレームでは形が読めなかった
        int m_centerHitFlashSteps = 2;
        // 既定は壊さない。壊れて消えると重さが飛距離に出ず、押し飛ばしと反発だけを先に詰められない
        bool m_breakEnabled = false;
        float m_breakSpeedScale = 0.75f;         // 貫通した直後に速度へ掛ける倍率
        float m_breakStopSeconds = 4.0f / 60.0f; // 貫通の瞬間に止める秒。4 フレームぶん
        float m_markProbeDistance = 64.0f;       // 跡の床を真下へ探す上限。これより下に床が無ければ跡を出さない

        int m_freezePendingSteps = 0; // 次のフレームに掛ける凍結のフレーム数。0 は予約なし
        int m_hitStopRemaining = 0;   // 止まっている残りフレーム数。0 は止まっていない
        int m_hitStopTotal = 0;       // 止め始めのフレーム数。振動の減衰の分母
        // 明けたフレームに自機が持つ速度。反動の当たりは、明けに BeginRebound が同じ m_pendingReboundArc から出し直す
        NS::Core::Vector3 m_pendingSelfVelocity{0.0f, 0.0f, 0.0f};
        NS::Game::Player::ReboundArc m_pendingReboundArc{}; // 明けたフレームに自機を弾く反動の向きと高さと距離
        LaunchArc m_pendingLaunchArc{};                     // 明けたフレームに相手を飛ばす曲線
        // 検知のフレームの相手の位置。置かれていた相手は明けたフレームにここへ厳密に戻す
        NS::Core::Vector3 m_pendingTargetHome{0.0f, 0.0f, 0.0f};
        NS::Core::Vector3 m_pendingImpactDir{0.0f, 0.0f, 0.0f}; // 発射の水平方向。食い込みと振動の軸
        float m_pendingShakeAmplitude = 0.0f;                   // この衝突の往復の振れ幅
        int m_pendingFlashSteps = 0;                            // この衝突の白のフレーム数。白の無い段は 0
        NS::Obj::CameraShakeDesc m_pendingShake{};              // この衝突のカメラの揺れ
        NS::Obj::CameraZoomRollDesc m_pendingZoomRoll{};        // この衝突の寄りと傾き。寄りの無い段は倍率 1
        NS::Core::Vector3 m_scaleHome{1.0f, 1.0f, 1.0f};        // 配置で決めた元の描画スケールの控え
        NS::Core::Vector3 m_stretchScale{1.0f, 1.0f, 1.0f};     // 解放のフレームの伸びた形
        int m_recoverRemaining = 0;                             // 形を戻し切るまでの残りフレーム数
        bool m_scaleHeld = false;                               // 潰した形のまま凍結している最中か
        NS::Obj::ObjectRef m_pendingTarget{};                   // 発射する相手。凍結をまたぐので使うたびに引く
        // 検知のフレームに相手が置かれていたか。食い込み・振動・元位置へ戻すのはこの時だけ
        bool m_pendingTargetPlaced = false;
        bool m_targetShapeHeld = false; // 相手の描く形を縮めたまま止めている最中か

        // 1 回の当たりのパッドの振動。始めの値から直線に減らし、書くフレーム数で切る
        struct PadVibrationPlan
        {
            NS::Platform::GamepadVibration start{};
            int fadeFrames = 0; // 始めの値から 0 まで減るフレーム数
            int frames = 0;     // 書くフレーム数。fadeFrames 以下
        };
        PadVibrationPlan m_pendingPad{}; // この衝突の振動。振動の無い段は書くフレーム数 0
        PadVibrationPlan m_pad{};        // 書いている振動
        int m_padElapsed = 0;            // 振動を始めた止めの頭から数えたフレーム数
        bool m_padRunning = false;       // 振動を書いている最中か

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
        int m_centerHitFlashRemaining = 0;
        NS::Game::Player::PlayerComponent* m_movement = nullptr; // 同じ配置物の移動。非所有
        CollisionInput* m_collisionInput = nullptr;
    };
} // namespace NS::Game::Level
