#pragma once

#include "Game/Level/HitTier.h"
#include "NSlib/CoreObject/Asset/Effect.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"

#include "NSlib/Core/Types.h"
#include <cstddef>
#include <optional>
#include <vector>

namespace NS
{
    class Body;
    class Collider;
    class IGetCamera;
    class IGetEffect;
} // namespace NS

namespace GL
{
    namespace Level
    {
        struct ImpactRecord;
        struct ImpactAnswer;
    } // namespace Level
} // namespace GL

namespace GL
{
    namespace Player
    {
        class BodySlamStatus;
        struct PlayerMotion;

        //! 火花を飛ばす向き
        enum class SparkHeading
        {
            Launch, // 相手の飛ぶ向き
            Scrape, // 横ずれの側と飛ぶ向きと上の間
        };

        //! 当たり1回の素材へ渡すゲーム情報と、層を出す時機
        struct ImpactShape
        {
            GL::Level::HitTier tier = GL::Level::HitTier::Center;
            std::size_t coreInput = 0;                       // 核の絵 impact.core の、段の色の節を出す動的入力の番号
            SparkHeading sparkHeading = SparkHeading::Launch;
            int holdLastFrame = 1;            // 核が最大近くに留まる最後のフレーム。当たりの絵の頭を 0 と数える
            //! 核を留まりの次のフレームで消すか。偽なら留まりの最後のフレームに親を止め、絵の定義の落ちるフレーム数で薄れる
            bool coreCut = false;
            int sparkStartFrame = 0; // 火花を出すフレーム。当たりの絵の頭を 0 と数え、中心近くだけ 3
            int emberStartFrame =
                0; // 火の粉を出すフレーム。当たりの絵の頭を 0 と数え、核が落ちるフレーム。中心近くだけ
            float power = 0.0f;
            float launchScale = 0.0f;
            float reboundScale = 0.0f;
            bool centerLayers = false;
            bool groundLight = false;
            bool groundDust = false;
            float rootMass = 0.0f;
        };

        //! 当たり 1 回の層を置く所と向き
        struct ImpactAim
        {
            NS::Vector3 contact; // 接触点。自機の玉の縁の、相手へ向いた点
            //! 火花の向き。大きな外れは MissSparkHeading (面の上の位置が無い時は横ずれの側と飛ぶ向きと上の間)、
            //! 他は相手の飛ぶ向き
            NS::Vector3 sparkDir;
            NS::Vector3 recoilDir;    // 弾かれ線の向き。自機の反動の初速の向き
            NS::Vector3 recoilOrigin; // 弾かれ線を出した所。自機の玉の縁の、反動の向きの逆の点
            NS::Vector3 dustOrigin;   // 粉の根を置く床の位置。輪の離隔は素材が持つ
            NS::Vector3 ringNormal;   // 輪の面の法線。相手の飛ぶ向きをカメラへ起こした向き
        };

        //! 自機の命中・反動・着地のエフェクトを出す部品
        //! 持ち主が束の項目を始めた時の頼みを、移動後の VisualStep で読む。頼みの無い命中では出さない
        //! 相手の飛行・着地の演出は相手の LaunchEffects に任せる
        class ImpactEffects : public NS::Object
        {
        public:
            //! 突進の開始と接触から、弾けと尾を出す。保持中の溜め演出は出さない
            //! userは空でないこと。位置はワールド座標
            void UpdateSlam(const NS::IGetEffect* user,
                            const NS::Vector3& center,
                            const BodySlamStatus& slam,
                            const PlayerMotion& motion,
                            const GL::Level::ImpactAnswer& impact);
            //! やり直しでエフェクトと未消化の命中・飛行の頼みを消す。userは空でないこと
            void Reset(const NS::IGetEffect* user);
            //! 当たり・反動・着地の絵を先に読む。絵は effect (持ち主のアクター) のシーンへ出す。持ち主の OnAssetsLoaded
            //! が呼ぶ
            void Preload(const NS::IGetEffect* effect);

            //! 当たりの絵を、次の Update で最後の当たりの内訳から始める
            //! 持ち主が段の束の持ち主へ渡す項目 HitEffect を始めたフレームに呼ぶ
            void RequestHit() noexcept { m_hitRequested = true; }
            //! 飛びの絵を、次の Update で始める。当たりの絵が出ていなければ始めない
            //! 持ち主が明けの束の持ち主へ渡す項目 FlightEffect を始めたフレームに呼ぶ
            void RequestFlight() noexcept { m_flightRequested = true; }

            //! 頼まれた当たりと飛びの絵を始め、出ている絵を進める。持ち主の見た目の段が呼ぶ
            //! 同じフレームに両方を頼まれた時は、当たりの絵の後に飛びの絵を始める
            //! @param[in] effect 絵を出す持ち主のアクター。空でないこと
            //! @param[in] camera 反動の尾を視点へ向けるのに、効果を掛ける前の視点を読む。空でないこと
            //! @param[in] position 持ち主のルートの位置。世界座標
            //! @param[in] slam 外れの火花の向きに突進の向きを読む
            //! @param[in] body 反動の尾の向きと、頂点と着地の判定に速度と接地を読む。空でないこと
            //! @param[in] collider 接触点と足元を、当たりのカプセルから作る。空でないこと
            //! @param[in] impact 最後の当たりの内訳。絵の置き所と素材へ渡す強さを決める
            void Update(const NS::IGetEffect* effect,
                        const NS::IGetCamera* camera,
                        const NS::Vector3& position,
                        const BodySlamStatus& slam,
                        const PlayerMotion& motion,
                        const NS::Body* body,
                        const NS::Collider* collider,
                        const GL::Level::ImpactRecord& impact);

            //! 当たりの内訳から、素材の入力と発生・停止の時機を決める
            [[nodiscard]] ImpactShape ShapeFor(const GL::Level::ImpactRecord& impact) const noexcept;

            //! 尾の +Y を速度方向へ向け、視線に近い時は輪郭の外へ見えるよう傾ける
            //! 座標は世界。速度 0 は真上、カメラ位置が無ければ傾けない。戻り値は単位方向
            [[nodiscard]] NS::Vector3 ReboundTrailHeading(
                const NS::Vector3& velocity,
                const NS::Vector3& ballCenter,
                const std::optional<NS::Vector3>& cameraPosition) const noexcept;

            //! 直近の当たりの層を置いた所と向き。まだ当たっていなければ全部 0
            [[nodiscard]] const ImpactAim& LastAim() const noexcept { return m_aim; }

            //! 外した側と面に沿う滑りを混ぜ、火花の単位方向を返す。u は右、v は上が正
            //! surfaceNormal は反動の配分が使った面の向き (ImpactRecord::faceNormal)。滑りはこの面に沿う
            //! slamDirection は水平成分を使う。面の原点か水平の向きが決まらない時は (0, 0, 0)
            [[nodiscard]] NS::Vector3 MissSparkHeading(float u,
                                                       float v,
                                                       const NS::Vector3& surfaceNormal,
                                                       const NS::Vector3& slamDirection) const noexcept;

            NS_REFLECT_BEGIN(ImpactEffects, NS::Object)
            NS_GROUP("輪")
            NS_FIELD(m_ringFaceCamera, "輪をカメラへ起こす割合")
            NS_GROUP("当たりの粉")
            NS_GROUP("着地の粉")
            NS_GROUP("時間と向き")
            NS_FIELD(m_coreHoldMax, "核の留まりの上限フレーム")
            NS_FIELD(m_wideCoreHoldLast, "外れの核の留まりフレーム")
            NS_FIELD(m_missSparkSideShare, "外れの火花の横の割合")
            NS_FIELD(m_centerSparkStart, "中心の火花の開始フレーム")
            NS_FIELD(m_scrapeLaunchWeight, "擦る火花の飛ぶ向きの重み")
            NS_FIELD(m_scrapeLiftWeight, "擦る火花の上向きの重み")
            NS_FIELD(m_ringStart, "輪の開始フレーム")
            NS_FIELD(m_ringEnd, "輪の終了フレーム")
            NS_FIELD(m_streakTailFrames, "光条の尾のフレーム")
            NS_FIELD(m_streakFadeFrames, "光条の薄れるフレーム")
            NS_FIELD(m_recoilDelay, "弾かれ線の遅れフレーム")
            NS_FIELD(m_ringLaunchWeight, "輪の飛ぶ向きの重み")
            NS_FIELD(m_reboundTrailFadeSteps, "反動の尾の薄れるフレーム")
            NS_FIELD(m_landDustLife, "着地の粉の寿命フレーム")
            NS_FIELD(m_dustRingLift, "着地の粉を浮かせる高さ")
            NS_FIELD(m_reboundStreakMinAcross, "反動の尾の最小横成分")
            NS_FIELD(m_coreAsset, "核の資産")
            NS_FIELD(m_streakAsset, "光条の資産")
            NS_FIELD(m_ringAsset, "輪の資産")
            NS_FIELD(m_sparksAsset, "火花の資産")
            NS_FIELD(m_embersAsset, "火の粉の資産")
            NS_FIELD(m_glowAsset, "照りの資産")
            NS_FIELD(m_recoilAsset, "弾かれ線の資産")
            NS_FIELD(m_dustAsset, "当たりの粉の資産")
            NS_FIELD(m_reboundTrailAsset, "反動の尾の資産")
            NS_FIELD(m_landDustAsset, "着地の粉の資産")
            NS_FIELD(m_floorLift, "照りを浮かせる高さ")
            NS_GROUP("突進")
            NS_FIELD(m_burstAsset, "放しの弾けの資産")
            NS_FIELD(m_slamTrailAsset, "突進の尾の資産")
            NS_FIELD(m_burstSteps, "放しの弾けの寿命フレーム")
            NS_FIELD(m_burstClearFrames, "放しの弾けを消す接触前フレーム")
            NS_FIELD(m_slamTrailFadeSteps, "突進の尾の薄れるフレーム")
            NS_REFLECT_END()

        private:
            void ClearSlam(const NS::IGetEffect* user);
            float m_ringFaceCamera = 1.0f;
            int m_coreHoldMax = 10;
            int m_wideCoreHoldLast = 1;
            float m_missSparkSideShare = 0.7f;
            int m_centerSparkStart = 3;
            float m_scrapeLaunchWeight = 1.0f;
            float m_scrapeLiftWeight = 1.0f;
            int m_ringStart = 3;
            int m_ringEnd = 10;
            int m_streakTailFrames = 3;
            int m_streakFadeFrames = 3;
            int m_recoilDelay = 4;
            float m_ringLaunchWeight = 0.6f;
            int m_reboundTrailFadeSteps = 8;
            int m_landDustLife = 30;
            //! 粉が床へ埋まらないよう浮かせる高さ。単位はメートル
            float m_dustRingLift = 0.3f;
            //! 筋が玉の陰に入らない傾き。玉の半径と筋の半幅の和を、筋の中心までの長さで割る
            float m_reboundStreakMinAcross = 0.8f / 1.3f;
            NS::Effect* m_coreAsset = nullptr;
            NS::Effect* m_streakAsset = nullptr;
            NS::Effect* m_ringAsset = nullptr;
            NS::Effect* m_sparksAsset = nullptr;
            NS::Effect* m_embersAsset = nullptr;
            NS::Effect* m_glowAsset = nullptr;
            NS::Effect* m_recoilAsset = nullptr;
            NS::Effect* m_dustAsset = nullptr;
            NS::Effect* m_reboundTrailAsset = nullptr;
            NS::Effect* m_landDustAsset = nullptr;
            //! 同じ面で描画がちらつかないための離隔
            float m_floorLift = 0.03f;
            NS::Effect* m_burstAsset = nullptr;
            NS::Effect* m_slamTrailAsset = nullptr;
            int m_burstSteps = 21;
            int m_burstClearFrames = 3;
            int m_slamTrailFadeSteps = 6;

            void ApplyTierRow(ImpactShape& shape, const GL::Level::ImpactRecord& impact) const noexcept;
            // 当たりの絵の頭から数えた当たり 1 回の段取り。層の番号 0 はまだ出していない事を表す
            struct HitPlan
            {
                bool active = false;
                int freezeStep = 0;   // 当たりの絵の頭の Effect のフレーム
                int releaseStep = -1; // 飛びの絵の頭のフレーム。まだ始めていなければ -1
                ImpactShape shape;
                NS::Vector3 contact;   // 接触点。自機の玉の縁の、相手へ向いた点
                NS::Vector3 launchDir; // 相手の飛ぶ水平の向き
                NS::Vector3 sideDir;   // 相手の面に沿った、横ずれの側の水平の向き
                NS::Vector3 scrapeDir; // 大きな外れの火花の向き。ImpactAim::sparkDir と同じ決め方
                NS::Vector3 selfDir;   // 自機の反動の初速の向き
                NS::Vector3 ringNormal;
                NS::Vector3 floor;       // 相手が置かれていた床の上の点
                NS::Vector3 awayDir;     // 自機から相手への水平の向き。当たりの粉を自機から離す側
                NS::uint32 targetId = 0; // 当たった相手のアクターの id
                NS::uint32 core = 0;
                NS::uint32 streak = 0;
                NS::uint32 ring = 0;
                NS::uint32 glow = 0;
                bool sparksPlayed = false;
                bool embersPlayed = false;
                bool recoilPlayed = false;
                bool dustPlayed = false;
            };

            // 飛びの絵の頭から、自機の反動が終わるまでの尾。当たりの段取りより長く残る。層の番号 0 は無い事を表す
            struct Flight
            {
                NS::uint32 reboundTrail = 0; // 反動の尾。親を止めたか消したら 0
                int reboundStartStep = 0;    // 反動の尾を出したフレーム。このフレームは出した姿のまま置き直さない
            };

            // Update が受けた持ち主の物。Update の中で呼ぶ関数へそのまま渡し、控えない
            struct Owner
            {
                const NS::IGetEffect* effect = nullptr;
                const NS::IGetCamera* camera = nullptr;
                NS::Vector3 position; // 持ち主のルートの位置。世界座標
            };

            void BeginHit(const Owner& owner,
                          const GL::Level::ImpactRecord& impact,
                          const BodySlamStatus& slam,
                          const NS::Collider* collider);
            void AdvanceHit(const Owner& owner, const NS::Collider* collider);
            void AdvanceHitCore(const Owner& owner, int frame);
            void AdvanceHitStreak(const Owner& owner, int frame);
            void AdvanceHitGlow(const Owner& owner, int frame);
            void AdvanceHitRing(const Owner& owner, int frame);
            void AdvanceHitRelease(const Owner& owner, const NS::Collider* collider, int step);
            void PlaySparks(const Owner& owner);
            void PlayEmbers(const Owner& owner);
            // 前の当たりの層のうち、ここが消える時を持っている物を今のフレームで畳む
            void FinishHeldEffects(const Owner& owner);
            void BeginFlight(const Owner& owner,
                             const PlayerMotion& motion,
                             const NS::Body* body,
                             const GL::Level::ImpactRecord& impact);
            // 反動の尾を自機へ付いていかせる。頂点で親を止める
            void AdvanceFlight(const Owner& owner, const PlayerMotion& motion, const NS::Body* body);
            // 反動の着地のフレームに足元へ粉を出し、次のフレームのために縦の速さを控える
            void AdvanceLanding(const Owner& owner,
                                const PlayerMotion& motion,
                                const NS::Body* body,
                                const NS::Collider* collider);
            // 効果を掛ける前の視点の位置。カメラが無い・姿が決まらない世界では空
            [[nodiscard]] static std::optional<NS::Vector3> CameraPosition(const Owner& owner);

            HitPlan m_plan;
            Flight m_flight;
            // RequestHit・RequestFlight の頼み。次の Update が読んで空にする
            bool m_hitRequested = false;
            bool m_flightRequested = false;
            float m_lastVerticalVelocity = 0.0f; // 前のフレームの自機の縦の速さ (m/s)。着地のフレームは既に 0
            bool m_landingDustPlayed = false;    // この反動の着地の粉を出した。反動を抜けたら戻す
            ImpactAim m_aim;
            std::vector<NS::uint32> m_dusts; // 出した粉。次の当たりの絵の頭で親を止める
            NS::uint32 m_slamTrail = 0;
            NS::uint32 m_burst = 0;
            bool m_wasSlamming = false;
            NS::Vector3 m_slamDirection;
        };
    } // namespace Player
} // namespace GL
