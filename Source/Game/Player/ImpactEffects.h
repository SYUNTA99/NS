#pragma once

#include "Game/Level/HitTier.h"
#include "Game/Player/ImpactEffectSettings.h"
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
    } // namespace Level
} // namespace GL

namespace GL
{
    namespace Player
    {
        class BodySlamStatus;
        struct PlayerMotion;

        //! 核が最大近くに留まる間の大きさの動かし方
        enum class CoreHoldMotion
        {
            Pulse,  // 偶数のフレームだけ縮める
            Settle, // 1 フレーム目の最大から、落ち着く割合へ少しずつ縮める
        };

        //! 火花を飛ばす向き
        enum class SparkHeading
        {
            Launch, // 相手の飛ぶ向き
            Scrape, // 横ずれの側と飛ぶ向きと上の間
        };

        //! 当たり 1 回の演出の大きさと量
        //! ShapeFor が段と調整値から決める。大きさや数が 0 の層は出さない
        struct ImpactShape
        {
            GL::Level::HitTier tier = GL::Level::HitTier::Center;
            std::size_t coreInput = 0;                       // 核の絵 impact.core の、段の色の節を出す動的入力の番号
            CoreHoldMotion coreHold = CoreHoldMotion::Pulse; // 核が最大近くに留まる間の大きさの動かし方
            SparkHeading sparkHeading = SparkHeading::Launch;
            std::size_t sparkCountInput = 0;  // 火花の絵 impact.sparks の、粒の数を入れる動的入力の番号
            std::size_t recoilCountInput = 0; // 弾かれ線の絵 impact.recoil の、本数を入れる動的入力の番号
            int holdLastFrame = 1;            // 核が最大近くに留まる最後のフレーム。当たりの絵の頭を 0 と数える
            //! 核を留まりの次のフレームで消すか。偽なら留まりの最後のフレームに親を止め、絵の定義の落ちるフレーム数で薄れる
            bool coreCut = false;
            int sparkStartFrame = 0; // 火花を出すフレーム。当たりの絵の頭を 0 と数え、中心近くだけ 3
            int emberStartFrame =
                0; // 火の粉を出すフレーム。当たりの絵の頭を 0 と数え、核が落ちるフレーム。中心近くだけ
            float coreDiameter = 0.0f; // 核の直径。単位は m
            float streakLength = 0.0f; // 光条の長さ。単位は m。中心近くだけ
            float ringRadius = 0.0f;   // 輪が広がりきった半径。単位は m。大きな外れは 0
            int sparkCount = 0;
            float sparkSpeed =
                0.0f; // 火花の速さ。単位は m/s。大きさ 1 の時の速さで、絵の上では大きさを掛けた速さになる
            float sparkScale = 1.0f;   // 火花の絵の全体に掛ける大きさ。粒の大きさ・散る範囲・速さが一緒に伸びる
            float sparkAmount = 0.0f;  // 火花の量。粒の数 × 速さ (本・m/s)。記録の量に使う
            int emberCount = 0;        // 火の粉の粒の数。中心近くだけで、他の段は 0
            float glowDiameter = 0.0f; // 照りの直径。単位は m。中心近くで相手が置かれていた時だけ
            int recoilCount = 0;
            float recoilLength = 0.0f; // 弾かれ線の長さ。単位は m
            int dustCount = 0;         // 当たりの粉の塊の数。飛んでいた相手は 0
            float dustScale = 0.0f;    // 当たりの粉の塊の大きさ。単位は m
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
            NS::Vector3 dustOrigin;   // 当たりの粉の輪の真ん中。相手が居た床から、自機と逆の側へずらした点
            NS::Vector3 ringNormal;   // 輪の面の法線。相手の飛ぶ向きをカメラへ起こした向き
        };

        //! 自機の命中・反動・着地のエフェクトを出す部品
        //! 持ち主が束の項目を始めた時の頼みを、移動後の VisualStep で読む。頼みの無い命中では出さない
        //! 相手の飛行・着地の演出は相手の LaunchEffects に任せる
        class ImpactEffects : public NS::Object
        {
        public:
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
            //! @param[in] impact 最後の当たりの内訳。絵の置き所と大きさを決める
            void Update(const NS::IGetEffect* effect,
                        const NS::IGetCamera* camera,
                        const NS::Vector3& position,
                        const BodySlamStatus& slam,
                        const PlayerMotion& motion,
                        const NS::Body* body,
                        const NS::Collider* collider,
                        const GL::Level::ImpactRecord& impact);

            //! 当たりの内訳から、演出の大きさと量を決める
            [[nodiscard]] ImpactShape ShapeFor(const GL::Level::ImpactRecord& impact) const noexcept;

            //! 反動の着地の粉が広がりきる半径を、着地の前のフレームの落ちる速さから決める
            //! @param[in] fallSpeed 落ちる速さ。単位は m/s。負は 0 と見なす
            //! @return 広がりきった半径。単位は m
            [[nodiscard]] float LandDustRadiusFor(float fallSpeed) const noexcept;

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

            void Configure(const ImpactEffectSettings* settings) noexcept
            {
                NS_ASSERT(Game, settings != nullptr, "演出の設定が空");
                m_settings = settings;
            }
            const ImpactEffectSettings& Settings() const noexcept { return *m_settings; }

            NS_REFLECT_NONE(ImpactEffects, NS::Object)

        private:
            inline static const ImpactEffectSettings m_defaultSettings{};
            const ImpactEffectSettings* m_settings = &m_defaultSettings;
            // 段で変わる層の形を段ごとの 1 行から埋める。段を足したら行を足す
            void ApplyTierRow(ImpactShape& shape, const GL::Level::ImpactRecord& impact, float power) const noexcept;
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
        };
    } // namespace Player
} // namespace GL
