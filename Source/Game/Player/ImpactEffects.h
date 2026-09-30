#pragma once

#include "Game/Level/HitTier.h"
#include "Game/Player/EffectLayerList.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Reflection/Reflection.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace NS::Game::Level
{
    class ImpactResolver;
    struct ImpactRecord;
} // namespace NS::Game::Level

namespace NS::Game::Player
{
    class PlayerComponent;

    //! @brief 当たり 1 回の層の大きさと量
    //! @details ImpactEffects::ShapeFor が当たりの内訳と欄から決める。0 の層は出さない
    struct ImpactShape
    {
        NS::Game::Level::HitTier tier = NS::Game::Level::HitTier::Center; //!< 当たりの段
        int holdLastFrame = 1;     //!< 核が最大近くに留まる最後のフレーム。止めの頭を 0 と数える
        int nearPullFrame = 0;     //!< 惜しいの輪が広がり止むフレーム。止めの頭を 0 と数え、他の段は 0
        int sparkStartFrame = 0;   //!< 火花を出すフレーム。止めの頭を 0 と数え、中心近くだけ 3
        int emberStartFrame = 0;   //!< 火の粉を出すフレーム。止めの頭を 0 と数え、核が落ちるフレーム。中心近くだけ
        float coreDiameter = 0.0f; //!< 核の直径。単位は m
        float streakLength = 0.0f; //!< 光条の長さ。単位は m。中心近くだけ
        float ringRadius = 0.0f;   //!< 輪が広がりきった半径。単位は m。大きな外れは 0
        int sparkCount = 0;        //!< 火花の粒の数
        float sparkSpeed = 0.0f;   //!< 火花の速さ。単位は m/s
        float sparkAmount = 0.0f;  //!< 火花の量。粒の数 × 速さ (本・m/s)。記録の量に使う
        int emberCount = 0;        //!< 火の粉の粒の数。中心近くだけで、他の段は 0
        float glowDiameter = 0.0f; //!< 照りの直径。単位は m。中心近くで相手が置かれていた時だけ
        int recoilCount = 0;       //!< 弾かれ線の本数
        float recoilLength = 0.0f; //!< 弾かれ線の長さ。単位は m
        int dustCount = 0;         //!< 当たりの粉の塊の数。飛んでいた相手は 0
        float dustScale = 0.0f;    //!< 当たりの粉の塊の大きさ。単位は m
    };

    //! @brief 当たり 1 回の層を置く所と向き
    struct ImpactAim
    {
        NS::Core::Vector3 contact;    //!< 接触点。自機の玉の縁の、相手へ向いた点
        NS::Core::Vector3 sparkDir;   //!< 火花の向き。大きな外れは横ずれの側と飛ぶ向きと上の間、他は相手の飛ぶ向き
        NS::Core::Vector3 recoilDir;  //!< 弾かれ線の向き。自機の反動の初速の向き
        NS::Core::Vector3 recoilOrigin; //!< 弾かれ線を出した所。自機の玉の縁の、反動の向きの逆の点
        NS::Core::Vector3 dustOrigin;   //!< 当たりの粉の輪の真ん中。相手が居た床から、自機と逆の側へずらした点
        NS::Core::Vector3 ringNormal; //!< 輪の面の法線。相手の飛ぶ向きをカメラへ起こした向き
    };

    //! @brief 自機が当ててから着地するまでのエフェクトの層を出し、出すと決めた記録を持つ
    //! @details 受け持つ層の名前は impact.・rebound.・land. で始まる
    //! 同居する ImpactResolver の止めの頭と明けを読み、止めの頭に核と照り、次のフレームから光条、
    //! 3 フレーム目から輪、明けに粉、明けの 4 フレーム後に弾かれ線を出す。火花は中心近くが 3 フレーム目、他は止めの頭
    //! 中心近くは核が落ちるフレームに火の粉を出す
    //! 明けに、反動に入った自機へ反動の尾を出し、毎フレーム付いていかせる。反動の尾は頂点で親を止めて 8 フレーム後に消す
    //! 反動の着地 (着地の潰れと同じフレーム) には足元へ粉を出す
    //! 飛ばした相手の飛び出しの尾と落ちた所の粉は、相手が自分で出す (LaunchEffects)。相手の部品は読まない
    //! 止めが 0 の当たりでは何も出さない
    //! 層の時間 (留まり・広がり・消えるフレーム) はここが持ち、形と色は絵が持つ
    //! 描画の無い世界でも記録は残し、試しと Replay は Layers を読む
    //! 優先度は Update 帯の +60。同じフレームの ImpactResolver (-100) が決めた止めの頭と明けと、
    //! 自機の移動 (Update) の後に走る。物理と飛ばした相手の段階の切り替え (LateUpdate) よりは前に走る
    //! 依存: EffectLayerList, NS::Game::Level::ImpactResolver, PlayerComponent, カメラの窓口
    class ImpactEffects : public NS::Obj::Component
    {
    public:
        ImpactEffects() noexcept;

        //! 同居する ImpactResolver を控え、描画のある世界なら層の絵を読み込む
        void OnStart() override;

        //! 記録のフレームを 1 つ進め、止めの頭と明けを読んで層を出し、出ている層の大きさを置き直す
        void OnUpdate() override;

        //! 出すと決めた層の記録
        [[nodiscard]] const EffectLayerList& Layers() const noexcept { return m_layers; }

        //! @brief 当たりの内訳から、層の大きさと量を決める
        //! @param[in] impact 当たりの内訳
        //! @return 層の大きさと量
        [[nodiscard]] ImpactShape ShapeFor(const NS::Game::Level::ImpactRecord& impact) const noexcept;

        //! @brief 反動の着地の粉が広がりきる半径を、着地の前のフレームの落ちる速さから決める
        //! @param[in] fallSpeed 落ちる速さ。単位は m/s。負は 0 と見なす
        //! @return 広がりきった半径。単位は m
        [[nodiscard]] float LandDustRadiusFor(float fallSpeed) const noexcept;

        //! @brief 反動の尾の再生の向き (+Y) を、自機の速度とカメラの位置から決める
        //! @details 速度の向きが視線に近い時だけ、画面の上の向きを保ったまま視線から起こす。起こす量は、筋の一番太い
        //! 真ん中が下塗りの幅ごと玉の輪郭の外に出るまで。視線に斜めに飛ぶ時とカメラが無い時は速度の向きのまま
        //! @param[in] velocity 自機の速度 (m/s)。長さが 0 なら真上へ飛ぶと見なす
        //! @param[in] ballCenter 玉の中心
        //! @param[in] cameraPosition カメラの位置。無ければ起こさない
        //! @return 長さ 1 の向き
        [[nodiscard]] static NS::Core::Vector3
        ReboundTrailHeading(const NS::Core::Vector3& velocity,
                            const NS::Core::Vector3& ballCenter,
                            const std::optional<NS::Core::Vector3>& cameraPosition) noexcept;

        //! 直近の当たりの層を置いた所と向き。まだ当たっていなければ全部 0
        [[nodiscard]] const ImpactAim& LastAim() const noexcept { return m_aim; }

        // 当たりの層の大きさと量は当てた瞬間の手触りそのもの。Inspector で触って詰められるよう公開する
        NS_REFLECT_BEGIN(ImpactEffects, NS::Obj::Component)
        NS_REFLECT_FIELD(m_coreDiameterBase, "核の直径の基準")
        NS_REFLECT_FIELD(m_coreDiameterPerPower, "核の直径の威力あたり")
        NS_REFLECT_FIELD(m_coreDiameterMax, "核の直径の上限")
        NS_REFLECT_FIELD(m_coreBirthScale, "核の出始めの大きさ")
        NS_REFLECT_FIELD(m_streakLengthBase, "光条の長さの基準")
        NS_REFLECT_FIELD(m_streakLengthPerPower, "光条の長さの威力あたり")
        NS_REFLECT_FIELD(m_streakEndThickness, "光条の細りきった太さ")
        NS_REFLECT_FIELD(m_ringRadiusBase, "輪の半径の基準")
        NS_REFLECT_FIELD(m_ringRadiusPerPower, "輪の半径の威力あたり")
        NS_REFLECT_FIELD(m_ringStartRadius, "輪の出始めの半径")
        NS_REFLECT_FIELD(m_nearRingReach, "惜しいの輪が届く割合")
        NS_REFLECT_FIELD(m_ringFaceCamera, "輪をカメラへ起こす割合")
        NS_REFLECT_FIELD(m_sparkCountMin, "火花の数の下限")
        NS_REFLECT_FIELD(m_sparkCountMax, "火花の数の上限")
        NS_REFLECT_FIELD(m_sparkSpeedBase, "火花の速さの基準")
        NS_REFLECT_FIELD(m_sparkSpeedPerLaunch, "火花の速さの飛ばしの比あたり")
        NS_REFLECT_FIELD(m_nearSparkShare, "惜しいの火花の数の割合")
        NS_REFLECT_FIELD(m_wideSparkCount, "大きな外れの火花の数")
        NS_REFLECT_FIELD(m_wideSparkSpeed, "大きな外れの火花の速さ")
        NS_REFLECT_FIELD(m_emberShare, "火の粉の数の火花あたり")
        NS_REFLECT_FIELD(m_glowDiameterBase, "照りの直径の基準")
        NS_REFLECT_FIELD(m_glowDiameterPerPower, "照りの直径の威力あたり")
        NS_REFLECT_FIELD(m_recoilCount, "弾かれ線の本数")
        NS_REFLECT_FIELD(m_wideRecoilCount, "大きな外れの弾かれ線の本数")
        NS_REFLECT_FIELD(m_recoilLengthBase, "弾かれ線の長さの基準")
        NS_REFLECT_FIELD(m_recoilLengthPerRebound, "弾かれ線の長さの反動の比あたり")
        NS_REFLECT_FIELD(m_dustCountBase, "当たりの粉の数の基準")
        NS_REFLECT_FIELD(m_dustCountMassLimit, "当たりの粉の数を増やす質量の上限")
        NS_REFLECT_FIELD(m_dustScaleBase, "当たりの粉の大きさの基準")
        NS_REFLECT_FIELD(m_dustScalePerRootMass, "当たりの粉の大きさの質量の平方根あたり")
        NS_REFLECT_FIELD(m_dustScalePerPower, "当たりの粉の大きさの威力あたりの伸び")
        NS_REFLECT_FIELD(m_landDustRadiusBase, "着地の粉の半径の基準")
        NS_REFLECT_FIELD(m_landDustRadiusPerFallSpeed, "着地の粉の半径の落ちる速さあたり")
        NS_REFLECT_END()

    private:
        // 止めの頭から数えた当たり 1 回の段取り。層の番号 0 はまだ出していない印
        struct HitPlan
        {
            bool active = false;
            int freezeStep = 0;   // 止めの頭の EffectLayerList のフレーム
            int releaseStep = -1; // 明けのフレーム。まだ明けていなければ -1
            ImpactShape shape;
            NS::Core::Vector3 contact;    // 接触点。自機の玉の縁の、相手へ向いた点
            NS::Core::Vector3 launchDir;  // 相手の飛ぶ水平の向き
            NS::Core::Vector3 sideDir;    // 相手の面に沿った、横ずれの側の水平の向き
            NS::Core::Vector3 scrapeDir;  // 大きな外れの火花の向き。横ずれの側と相手の飛ぶ向きと上の間
            NS::Core::Vector3 selfDir;    // 自機の反動の初速の向き
            NS::Core::Vector3 ringNormal; // 輪の面の法線
            NS::Core::Vector3 floor;      // 相手が置かれていた床の上の点
            NS::Core::Vector3 awayDir;    // 自機から相手への水平の向き。当たりの粉を自機から離す側
            std::uint32_t targetId = 0;   // 当たった相手の配置物の id
            std::uint32_t core = 0;
            std::uint32_t streak = 0;
            std::uint32_t ring = 0;
            std::uint32_t glow = 0;
            bool sparksPlayed = false;
            bool embersPlayed = false;
            bool recoilPlayed = false;
            bool dustPlayed = false;
        };

        // 明けから、自機の反動が終わるまでの尾。当たりの段取りより長く残る。層の番号 0 は無い印
        struct Flight
        {
            std::uint32_t reboundTrail = 0; // 反動の尾。親を止めたか消したら 0
            int reboundStartStep = 0;       // 反動の尾を出したフレーム。このフレームは出した姿のまま置き直さない
        };

        // 出した層を、決めたフレームに子ごと消す控え
        struct ScheduledStop
        {
            std::uint32_t id = 0;
            int step = 0;
        };

        void BeginHit(NS::Gfx::EffectScene* effects, const NS::Game::Level::ImpactRecord& impact);
        void AdvanceHit(NS::Gfx::EffectScene* effects);
        void PlaySparks(NS::Gfx::EffectScene* effects);
        void PlayEmbers(NS::Gfx::EffectScene* effects);
        // 前の当たりの層のうち、ここが消える時を持っている物を今のフレームで畳む
        void FinishHeldLayers(NS::Gfx::EffectScene* effects);
        void SetAmount(std::uint32_t id, float amount) noexcept;
        // 明けに反動の尾を出す
        void BeginFlight(NS::Gfx::EffectScene* effects);
        // 反動の尾を自機へ付いていかせる。頂点で親を止める
        void AdvanceFlight(NS::Gfx::EffectScene* effects);
        // 反動の着地のフレームに足元へ粉を出し、次のフレームのために縦の速さを控える
        void AdvanceLanding(NS::Gfx::EffectScene* effects);
        // 出した層を lifeSteps フレーム後に消す
        void StopLater(std::uint32_t id, int lifeSteps);
        void RunScheduledStops(NS::Gfx::EffectScene* effects);
        // 場面のカメラの位置。カメラが無い・姿が決まらない世界では空
        [[nodiscard]] std::optional<NS::Core::Vector3> CameraPosition() const;

        EffectLayerList m_layers;
        NS::Game::Level::ImpactResolver* m_resolver = nullptr;
        PlayerComponent* m_player = nullptr;
        HitPlan m_plan;
        Flight m_flight;
        std::vector<ScheduledStop> m_scheduledStops;
        float m_lastVerticalVelocity = 0.0f; // 前のフレームの自機の縦の速さ (m/s)。着地のフレームは既に 0
        bool m_landingDustPlayed = false;    // この反動の着地の粉を出した。反動を抜けたら戻す
        ImpactAim m_aim;
        std::vector<std::uint32_t> m_dusts; // 出した粉。次の当たりの止めの頭で親を止める

        float m_coreDiameterBase = 0.3f;       // 威力 0 の核の直径 (m)。威力 0.7 の外れで 0.44 m、玉の直径の 3 分の 1
        float m_coreDiameterPerPower = 0.2f;   // 威力 1 あたり足す直径 (m)。溜めきり中心近く (威力 2) で 0.7 m
        float m_coreDiameterMax = 0.7f;        // 核の直径の上限 (m)。玉の直径 1.3 m の 54% で、玉の中心を覆わない
        float m_coreBirthScale = 0.5f;         // 止めの頭の星形と芯の大きさの割合。次のフレームで最大へ広がる
        float m_streakLengthBase = 4.0f;       // 威力 0 の光条の長さ (m)
        float m_streakLengthPerPower = 2.0f;   // 威力 1 あたり足す長さ (m)。溜めきりで 8 m、画面を横切る
        float m_streakEndThickness = 0.3f;     // 光条が細りきった後に残る太さの割合。1 から細っていく
        float m_ringRadiusBase = 0.5f;         // 威力 0 の輪の広がりきった半径 (m)
        float m_ringRadiusPerPower = 0.6f;     // 威力 1 あたり足す半径 (m)。溜めきりで 1.7 m、相手の玉の 2 倍強
        float m_ringStartRadius = 0.3f;        // 輪の出始めの半径 (m)。接触点の核より少し外
        float m_nearRingReach = 0.5f;          // 惜しいの輪が届く、広がりきった半径への割合
        float m_ringFaceCamera = 1.0f;         // 輪の法線をカメラへ起こす重み。相手の飛ぶ向きの重みは 0.6
        int m_sparkCountMin = 10;              // 威力 0.7 の火花の数
        int m_sparkCountMax = 30;              // 威力 2 の火花の数。完璧の火花は不完全の数倍
        float m_sparkSpeedBase = 6.0f;         // 飛ばしの比 0 の火花の速さ (m/s)
        float m_sparkSpeedPerLaunch = 3.0f;    // 飛ばしの比 1 あたり足す速さ (m/s)。重い相手ほど遅い
        float m_nearSparkShare = 0.5f;         // 惜しいの火花の数の、同じ威力の中心近くへの割合
        int m_wideSparkCount = 16;             // 大きな外れの火花の数。威力に依らない。手本のガードの 7〜10 の筋の数
        float m_wideSparkSpeed = 4.0f;         // 大きな外れの火花の速さ (m/s)。面に沿って擦れる
        float m_emberShare = 7.0f;             // 火の粉の粒の数の、同じ当たりの火花の数への倍率。威力 2 で 210 粒
        float m_glowDiameterBase = 2.0f;       // 威力 0 の照りの直径 (m)
        float m_glowDiameterPerPower = 1.6f;   // 威力 1 あたり足す直径 (m)。溜めきりで 5.2 m
        int m_recoilCount = 8;                 // 弾かれ線の本数
        int m_wideRecoilCount = 4;             // 大きな外れの弾かれ線の本数
        float m_recoilLengthBase = 0.6f;       // 反動の比 0 の弾かれ線の長さ (m)
        float m_recoilLengthPerRebound = 0.5f; // 反動の比 1 あたり足す長さ (m)。重い相手ほど長い
        int m_dustCountBase = 4;               // 質量 0 の粉の塊の数
        float m_dustCountMassLimit = 4.0f;     // 塊の数を増やす質量の上限。質量 1 あたり 1 つ足す
        float m_dustScaleBase = 0.8f;          // 質量 0 の塊の大きさ (m)
        float m_dustScalePerRootMass = 0.3f;   // 質量の平方根 1 あたり足す大きさ (m)
        float m_dustScalePerPower = 0.5f;      // 威力 1 からの 1 あたりで大きさに掛ける伸び。威力 2 で 1.5 倍
        float m_landDustRadiusBase = 1.2f;          // 落ちる速さ 0 の着地の粉が広がりきる半径 (m)
        float m_landDustRadiusPerFallSpeed = 0.04f; // 落ちる速さ 1 m/s あたり足す半径 (m)。溜めきりの反動で 1.67 m
    };
} // namespace NS::Game::Player
