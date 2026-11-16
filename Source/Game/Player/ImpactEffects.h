#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Asset/Effect.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"

#include <initializer_list>
#include <optional>
#include <vector>

namespace NS
{
    class IGetCamera;
    class IGetCollision;
    class IGetEffect;
} // namespace NS

namespace GL
{
    namespace Player
    {
        struct PlayerMotion;

        //! 地形に当たった 1 回の、エフェクトを決める値。持ち主が当たった束を始めたフレームに渡す
        struct TerrainHit
        {
            NS::Vector3 contact;  // 衝突点。自機の玉の表面の、面の側の点
            NS::Vector3 normal;   // 当たった面の外向きの法線。正規化済み
            NS::Vector3 incoming; // 当たる直前に進もうとした向き。正規化済み
            NS::Vector3 rebound;  // 返る向き。正規化済み
            bool center = false;  // 中心の段 (核・光条・輪・火の粉・床の照り) を出すか。偽は外れの段
            float power = 0.0f;   // 素材へ渡す強さ
        };

        //! Update のフレームの持ち主の体
        struct ImpactSelf
        {
            NS::Vector3 position; // 玉の中心。世界座標
            NS::Vector3 foot;     // 当たりのカプセルの下端。世界座標
            float radius = 0.0f;  // 当たりのカプセルの半径
            bool grounded = false;
            int stopFrames = 0; // 当たりの止めの残りのフレーム数。当たりを始めるフレームに、核を留める長さを決める
        };

        //! 当たり 1 回の層を置いた所と向き
        struct ImpactAim
        {
            NS::Vector3 contact;      // 衝突点
            NS::Vector3 sparkDir;     // 火花の向き。中心の段は面の法線、外れの段は面に沿って擦る向き
            NS::Vector3 recoilDir;    // 弾かれ線の向き。飛び出すフレームの自機の速度の向き
            NS::Vector3 recoilOrigin; // 弾かれ線を出した所。自機の玉の表面の、飛ぶ向きの逆の点
            NS::Vector3 dustOrigin;   // 粉の根を置いた床の位置
            NS::Vector3 ringNormal;   // 輪の面の法線。面の法線をカメラへ起こした向き
        };

        //! 自機のエアダッシュ・地形の反動・着地のエフェクトを出す部品
        //! 持ち主が束の項目を始めた時の頼みを、移動後の VisualStep で読む。頼みの無い当たりでは出さない
        class ImpactEffects : public NS::Object
        {
        public:
            //! やり直しで、持ち主が出しているエフェクトと、未消化の頼みを消す。user は空でないこと
            void Reset(const NS::IGetEffect* user);

            //! 当たりのエフェクトを、次の Update で hit から始める
            //! 持ち主が当たった束の持ち主へ渡す項目 HitEffect を始めたフレームに呼ぶ
            void RequestHit(const TerrainHit& hit) noexcept;
            //! 飛び出すフレームのエフェクトを、次の Update で始める。当たりのエフェクトが出ていなければ始めない
            //! 持ち主が明けの束の持ち主へ渡す項目 FlightEffect を始めたフレームに呼ぶ
            void RequestFlight() noexcept { m_flightRequested = true; }

            //! 頼まれた物を始め、出ている物を進める。持ち主の VisualStep が毎フレーム 1 回呼ぶ
            //! 同じフレームに両方を頼まれた時は、当たりの後に飛び出しを始める
            //! @param[in] effect エフェクトを出す持ち主のアクター。空でないこと
            //! @param[in] camera 輪と反動の尾を視点へ向けるのに、効果を掛ける前の視点を読む。空でないこと
            //! @param[in] collision 照りと粉を置く床を、自機の真下へ調べる。空でないこと
            void Update(const NS::IGetEffect* effect,
                        const NS::IGetCamera* camera,
                        const NS::IGetCollision* collision,
                        const PlayerMotion& motion,
                        const ImpactSelf& self);

            //! 尾の +Y を速度方向へ向け、視線に近い時は輪郭の外へ見えるよう傾ける
            //! 座標は世界。速度 0 は真上、カメラ位置が無ければ傾けない。戻り値は単位方向
            [[nodiscard]] NS::Vector3 ReboundTrailHeading(
                const NS::Vector3& velocity,
                const NS::Vector3& ballCenter,
                const std::optional<NS::Vector3>& cameraPosition) const noexcept;

            //! 直近の当たりの層を置いた所と向き。まだ当たっていなければ全部 0
            [[nodiscard]] const ImpactAim& LastAim() const noexcept { return m_aim; }

            NS_REFLECT_BEGIN(ImpactEffects, NS::Object)
            NS_GROUP("時間と向き")
            NS_FIELD(m_ringFaceCamera, "輪をカメラへ起こす割合")
            NS_FIELD(m_coreHoldMax, "核の留まりの上限フレーム")
            NS_FIELD(m_wideCoreHoldLast, "外れの核の留まりフレーム")
            NS_FIELD(m_centerSparkStart, "中心の火花の開始フレーム")
            NS_FIELD(m_scrapeLaunchWeight, "擦る火花の飛ぶ向きの重み")
            NS_FIELD(m_scrapeLiftWeight, "擦る火花の上向きの重み")
            NS_FIELD(m_ringStart, "輪の開始フレーム")
            NS_FIELD(m_ringEnd, "輪の終了フレーム")
            NS_FIELD(m_streakTailFrames, "光条の尾のフレーム")
            NS_FIELD(m_streakFadeFrames, "光条の薄れるフレーム")
            NS_FIELD(m_recoilDelay, "弾かれ線の遅れフレーム")
            NS_FIELD(m_ringLaunchWeight, "輪の飛ぶ向きの重み")
            NS_FIELD(m_reboundTrailFadeFrames, "反動の尾の薄れるフレーム")
            NS_FIELD(m_reboundStreakMinAcross, "反動の尾の最小横成分")
            NS_FIELD(m_dashTrailFadeFrames, "ダッシュの尾の薄れるフレーム")
            NS_GROUP("床")
            NS_FIELD(m_floorProbeDrop, "床を探す深さ")
            NS_FIELD(m_floorLift, "照りを浮かせる高さ")
            NS_FIELD(m_landDustLife, "着地の粉の寿命フレーム")
            NS_FIELD(m_dustRingLift, "着地の粉を浮かせる高さ")
            NS_GROUP("素材")
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
            NS_FIELD(m_dashTrailAsset, "ダッシュの尾の資産")
            NS_REFLECT_END()

        private:
            float m_ringFaceCamera = 1.0f;
            int m_coreHoldMax = 10;
            int m_wideCoreHoldLast = 1;
            int m_centerSparkStart = 3;
            float m_scrapeLaunchWeight = 1.0f;
            float m_scrapeLiftWeight = 1.0f;
            int m_ringStart = 3;
            int m_ringEnd = 10;
            int m_streakTailFrames = 3;
            int m_streakFadeFrames = 3;
            int m_recoilDelay = 4;
            float m_ringLaunchWeight = 0.6f;
            int m_reboundTrailFadeFrames = 8;
            //! 筋が玉の陰に入らない傾き。玉の半径と筋の半幅の和を、筋の中心までの長さで割る
            float m_reboundStreakMinAcross = 0.8f / 1.3f;
            int m_dashTrailFadeFrames = 6;
            //! 照りと粉を置く床を、自機の中心から真下へ探す長さ。単位はメートル
            float m_floorProbeDrop = 3.0f;
            //! 同じ面で描画がちらつかないための離隔。単位はメートル
            float m_floorLift = 0.03f;
            int m_landDustLife = 30;
            //! 粉が床へ埋まらないよう浮かせる高さ。単位はメートル
            float m_dustRingLift = 0.3f;
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
            NS::Effect* m_dashTrailAsset = nullptr;

            // 当たったフレームから数えた当たり 1 回の段取り。再生の番号 0 は出ていない事を表す
            struct HitPlan
            {
                bool active = false;
                TerrainHit hit;
                int startFrame = 0;    // 当たりのエフェクトを始めた Update の番号
                int releaseFrame = -1; // 飛び出しを始めた Update の番号。まだ始めていなければ -1
                int holdLastFrame = 1; // 核が最大近くに留まる最後のフレーム。当たったフレームを 0 と数える
                int sparkStartFrame = 0;
                bool hasFloor = false;
                NS::Vector3 floor;      // 照りと粉を置く床の上の点
                NS::Vector3 sparkDir;   // 火花と火の粉の向き
                NS::Vector3 ringNormal;
                NS::Vector3 flightDir;  // 飛び出すフレームの自機の速度の向き
                NS::uint32 core = 0;
                NS::uint32 streak = 0;
                NS::uint32 ring = 0;
                NS::uint32 glow = 0;
                NS::uint32 sparks = 0;      // 止めの間に束を始め直した時に、前の段の火花を消すために控える
                bool streakFading = false;  // 光条の親をもう止めたか
                bool sparksPlayed = false;
                bool embersPlayed = false;
                bool recoilPlayed = false;
                bool dustPlayed = false;
            };

            // Update が受けた持ち主の物。Update の中で呼ぶ関数へそのまま渡し、覚えない
            struct Owner
            {
                const NS::IGetEffect* effect = nullptr;
                const NS::IGetCamera* camera = nullptr;
                const NS::IGetCollision* collision = nullptr;
                ImpactSelf self;
            };

            // 決まったフレームに子も含めて消す再生
            struct TimedStop
            {
                NS::uint32 id = 0;
                int frame = 0; // 消す Update の番号
            };

            // 位置と向きに 1 つ出し、動的入力を 0 番から順に書く
            NS::uint32 Play(const Owner& owner,
                            const NS::Effect* asset,
                            const NS::Vector3& position,
                            const NS::Quaternion& rotation,
                            std::initializer_list<float> inputs) const;
            // 親を止め、frames フレーム後に子も含めて消す
            void FadeOut(const Owner& owner, NS::uint32 id, int frames);
            void StopDueEffects(const Owner& owner);
            [[nodiscard]] bool FindFloor(const Owner& owner, NS::Vector3& outFloor) const;

            void BeginHit(const Owner& owner);
            void AimHit(const Owner& owner, HitPlan& plan) const;
            void AdvanceHit(const Owner& owner);
            void AdvanceHitCore(const Owner& owner, int frame);
            void AdvanceHitStreak(const Owner& owner, int frame);
            void AdvanceHitRing(const Owner& owner, int frame);
            void AdvanceHitRelease(const Owner& owner);
            // 前の当たりの層のうち、消える時をここが持っている物を今のフレームで消す
            void FinishHeldEffects(const Owner& owner);
            void BeginFlight(const Owner& owner, const PlayerMotion& motion);
            // 反動の尾を自機へ付いていかせる。反動の状態を出るか接地したら薄れさせる
            void AdvanceFlight(const Owner& owner, const PlayerMotion& motion);
            // ダッシュの尾を始め、ダッシュの間は自機へ付いていかせ、終わったら薄れさせる
            void AdvanceDash(const Owner& owner, const PlayerMotion& motion);
            // 当たってから宙に出た後の、最初に接地したフレームに足元へ粉を出す
            void AdvanceLanding(const Owner& owner, const PlayerMotion& motion);
            // 効果を掛ける前の視点の位置。カメラが無い・姿が決まらない世界では空
            [[nodiscard]] static std::optional<NS::Vector3> CameraPosition(const Owner& owner);

            HitPlan m_plan;
            ImpactAim m_aim;
            int m_frame = 0; // Update を呼ばれた回数。止めの間も進む
            // RequestHit・RequestFlight の頼み。次の Update が読んで空にする
            bool m_hitRequested = false;
            bool m_flightRequested = false;
            TerrainHit m_requestedHit;
            std::vector<TimedStop> m_timedStops;
            std::vector<NS::uint32> m_dusts; // 出した当たりの粉。次の当たりの頭で親を止める
            NS::uint32 m_reboundTrail = 0;
            int m_reboundTrailStartFrame = 0; // 反動の尾を出したフレーム。このフレームは出した姿のまま置き直さない
            NS::uint32 m_dashTrail = 0;
            bool m_wasDashing = false;
            NS::Vector3 m_dashDirection; // ダッシュの尾を向ける水平の向き。速度が 0 のフレームは前の向きを保つ
            bool m_landingArmed = false;   // 当たった後、まだ宙に出ていない
            bool m_awaitingLanding = false; // 当たって宙に出た後、まだ接地していない
            float m_lastVerticalVelocity = 0.0f; // 前のフレームの縦の速さ (m/s)。接地のフレームは既に 0
        };
    } // namespace Player
} // namespace GL
