#pragma once

#include "Game/Level/HitTier.h"
#include "Game/Player/EffectLayerList.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Reflection/Reflection.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

class Player;

namespace NS::Game::Level
{
    class ImpactResolver;
    struct ImpactRecord;
} // namespace NS::Game::Level

namespace NS::Game::Player
{
    //! @brief 核が最大近くに留まる間の大きさの動かし方
    enum class CoreHoldMotion
    {
        Pulse,  //!< 偶数のフレームだけ縮める
        Settle, //!< 1 フレーム目の最大から、落ち着く割合へ少しずつ縮める
    };

    //! @brief 火花を飛ばす向き
    enum class SparkHeading
    {
        Launch, //!< 相手の飛ぶ向き
        Scrape, //!< 横ずれの側と飛ぶ向きと上の間
    };

    //! @brief 当たり 1 回の層の大きさと量
    //! @details ImpactEffects::ShapeFor が当たりの内訳と欄から決める。0 の層は出さない
    //! 段で変わる物は ShapeFor が段ごとの 1 行から埋め、層を出す側は段を比べずにこの欄を読む
    struct ImpactShape
    {
        NS::Game::Level::HitTier tier = NS::Game::Level::HitTier::Center; //!< 当たりの段
        std::size_t coreInput = 0;                        //!< 核の絵 impact.core の、段の色の節を出す動的入力の番号
        CoreHoldMotion coreHold = CoreHoldMotion::Pulse;  //!< 核が最大近くに留まる間の大きさの動かし方
        SparkHeading sparkHeading = SparkHeading::Launch; //!< 火花の向き
        std::size_t sparkCountInput = 0;                  //!< 火花の絵 impact.sparks の、粒の数を入れる動的入力の番号
        std::size_t recoilCountInput = 0;                 //!< 弾かれ線の絵 impact.recoil の、本数を入れる動的入力の番号
        int holdLastFrame = 1; //!< 核が最大近くに留まる最後のフレーム。当たりの絵の頭を 0 と数える
        //! 核を留まりの次のフレームで消すか。偽なら留まりの最後のフレームに親を止め、絵の定義の落ちるフレーム数で薄れる
        bool coreCut = false;
        int sparkStartFrame = 0; //!< 火花を出すフレーム。当たりの絵の頭を 0 と数え、中心近くだけ 3
        int emberStartFrame = 0; //!< 火の粉を出すフレーム。当たりの絵の頭を 0 と数え、核が落ちるフレーム。中心近くだけ
        float coreDiameter = 0.0f; //!< 核の直径。単位は m
        float streakLength = 0.0f; //!< 光条の長さ。単位は m。中心近くだけ
        float ringRadius = 0.0f;   //!< 輪が広がりきった半径。単位は m。大きな外れは 0
        int sparkCount = 0;        //!< 火花の粒の数
        float sparkSpeed = 0.0f;  //!< 火花の速さ。単位は m/s。大きさ 1 の時の速さで、絵の上では大きさを掛けた速さになる
        float sparkScale = 1.0f;  //!< 火花の絵の全体に掛ける大きさ。粒の大きさ・散る範囲・速さが一緒に伸びる
        float sparkAmount = 0.0f; //!< 火花の量。粒の数 × 速さ (本・m/s)。記録の量に使う
        int emberCount = 0;       //!< 火の粉の粒の数。中心近くだけで、他の段は 0
        float glowDiameter = 0.0f; //!< 照りの直径。単位は m。中心近くで相手が置かれていた時だけ
        int recoilCount = 0;       //!< 弾かれ線の本数
        float recoilLength = 0.0f; //!< 弾かれ線の長さ。単位は m
        int dustCount = 0;         //!< 当たりの粉の塊の数。飛んでいた相手は 0
        float dustScale = 0.0f;    //!< 当たりの粉の塊の大きさ。単位は m
    };

    //! @brief 当たり 1 回の層を置く所と向き
    struct ImpactAim
    {
        NS::Core::Vector3 contact; //!< 接触点。自機の玉の縁の、相手へ向いた点
        //! 火花の向き。大きな外れは MissSparkHeading (面の上の位置が無い時は横ずれの側と飛ぶ向きと上の間)、
        //! 他は相手の飛ぶ向き
        NS::Core::Vector3 sparkDir;
        NS::Core::Vector3 recoilDir;    //!< 弾かれ線の向き。自機の反動の初速の向き
        NS::Core::Vector3 recoilOrigin; //!< 弾かれ線を出した所。自機の玉の縁の、反動の向きの逆の点
        NS::Core::Vector3 dustOrigin;   //!< 当たりの粉の輪の真ん中。相手が居た床から、自機と逆の側へずらした点
        NS::Core::Vector3 ringNormal;   //!< 輪の面の法線。相手の飛ぶ向きをカメラへ起こした向き
    };

    //! @brief 自機が当ててから着地するまでのエフェクトの層を出し、出すと決めた記録を持つ
    //! @details 受け持つ層の名前は impact.・rebound.・land. で始まる
    //! 当たりのタイムラインの事象が置いた頼みを読み、当たりの絵の頭に核と照り、次のフレームから光条、
    //! 3 フレーム目から輪、飛びの絵の頭に粉、その 4 フレーム後に弾かれ線を出す。火花は中心近くが 3 フレーム目、
    //! 他は当たりの絵の頭。中心近くは核が落ちるフレームに火の粉を出す
    //! 飛びの絵の頭に、反動に入った自機へ反動の尾を出し、毎フレーム付いていかせる。反動の尾は頂点で親を止めて 8
    //! フレーム後に消す 反動の着地 (着地の潰れと同じフレーム) には足元へ粉を出す
    //! 飛ばした相手の飛び出しの尾と落ちた所の粉は、相手が自分で出す (LaunchEffects)。相手の部品は読まない
    //! 当たりの絵の事象を置いていないタイムラインの当たりと、タイムラインの引けない当たりでは何も出さない
    //! 層の時間 (留まり・広がり・消えるフレーム) はここが持ち、形と色は絵が持つ
    //! 描画の無い世界でも記録は残し、試しと Replay は Layers を読む
    //! Player の見た目の段 (VisualStep) が最後に呼ぶ。
    //! 同じフレームの ImpactResolver が事象から頼みを置いた後と、自機の移動の段の後に走る。
    //! 物理の段と、飛ばした相手が自分の段階を切り替える Triggers の段よりは前に走る
    //! 依存: EffectLayerList, NS::Game::Level::ImpactResolver, Player, カメラの窓口
    class ImpactEffects : public NS::Obj::Component
    {
    public:
        ImpactEffects() noexcept;

        //! 同居する ImpactResolver を控え、描画のある世界なら層の絵を読み込む
        void OnStart() override;

        //! 記録のフレームを 1 つ進め、置かれた頼みを読んで層を出し、出ている層の大きさを置き直す
        void OnUpdate() override;

        //! @brief 当たりの絵を始める頼みを置く。同じフレームの OnUpdate が、同居する ImpactResolver
        //! の直近の当たりから層を出す
        //! @details 決定の段から直に層を出すと EffectLayerList::BeginStep より前に出て、始まりのフレームが 1 つずれる
        void RequestHitEffect() noexcept { m_hitRequested = true; }

        //! @brief 飛びの絵を始める頼みを置く。同じフレームの OnUpdate が反動の尾を出す
        //! @details 当たりの絵の段取りが終わった後の頼みは何もしない
        void RequestFlightEffect() noexcept { m_flightRequested = true; }

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
        [[nodiscard]] static NS::Core::Vector3 ReboundTrailHeading(
            const NS::Core::Vector3& velocity,
            const NS::Core::Vector3& ballCenter,
            const std::optional<NS::Core::Vector3>& cameraPosition) noexcept;

        //! 直近の当たりの層を置いた所と向き。まだ当たっていなければ全部 0
        [[nodiscard]] const ImpactAim& LastAim() const noexcept { return m_aim; }

        //! @brief 外れの火花の向きを、当てた面の上の位置と突進の向きから決める
        //! @details 外した側 (面の右 × u + 上 × v の向き) へ 7 割、相手の表面に沿って滑る向き
        //! (突進の向きから表面の向きへ 押し込む成分を除いた向き) へ 3
        //! 割を足して正規化する。表面の向きは相手を丸として読む (MissSurfaceNormal の鋭さ 2)。 面の右は JudgeHitFace
        //! と同じく、上から見て進む向きの右
        //! @param[in] u 面の上の左右の位置。自機から見て右が正
        //! @param[in] v 面の上の上下の位置。上が正
        //! @param[in] slamDirection 突進の向き。縦の成分は捨てる
        //! @return 長さ 1 の向き。位置が面の真ん中で外した側が決まらない時と、突進の水平の向きが決まらない時は (0, 0,
        //! 0)
        [[nodiscard]] static NS::Core::Vector3 MissSparkHeading(float u,
                                                                float v,
                                                                const NS::Core::Vector3& slamDirection) noexcept;

        // 当たりの層の大きさと量は当てた瞬間の手触りそのもの。Inspector で触って詰められるよう公開する
        NS_REFLECT_BEGIN(ImpactEffects, NS::Obj::Component)
        NS_REFLECT_GROUP("核")
        NS_REFLECT_FIELD(m_coreDiameterBase, "核の直径の基準")
        NS_REFLECT_FIELD(m_coreDiameterPerPower, "核の直径の威力あたり")
        NS_REFLECT_FIELD(m_coreDiameterMax, "核の直径の上限")
        NS_REFLECT_FIELD(m_coreBirthScale, "核の出始めの大きさ")
        NS_REFLECT_GROUP("光条")
        NS_REFLECT_FIELD(m_streakLengthBase, "光条の長さの基準")
        NS_REFLECT_FIELD(m_streakLengthPerPower, "光条の長さの威力あたり")
        NS_REFLECT_FIELD(m_streakEndThickness, "光条の細りきった太さ")
        NS_REFLECT_GROUP("輪")
        NS_REFLECT_FIELD(m_ringRadiusBase, "輪の半径の基準")
        NS_REFLECT_FIELD(m_ringRadiusPerPower, "輪の半径の威力あたり")
        NS_REFLECT_FIELD(m_ringStartRadius, "輪の出始めの半径")
        NS_REFLECT_FIELD(m_ringFaceCamera, "輪をカメラへ起こす割合")
        NS_REFLECT_GROUP("火花と火の粉")
        NS_REFLECT_FIELD(m_sparkCountMin, "火花の数の下限")
        NS_REFLECT_FIELD(m_sparkCountMax, "火花の数の上限")
        NS_REFLECT_FIELD(m_sparkSpeedBase, "火花の速さの基準")
        NS_REFLECT_FIELD(m_sparkSpeedPerLaunch, "火花の速さの飛ばしの比あたり")
        NS_REFLECT_FIELD(m_wideSparkCount, "大きな外れの火花の数")
        NS_REFLECT_FIELD(m_wideSparkSpeed, "大きな外れの火花の速さ")
        NS_REFLECT_FIELD(m_wideSparkScale, "大きな外れの火花の大きさ")
        NS_REFLECT_FIELD(m_emberShare, "火の粉の数の火花あたり")
        NS_REFLECT_GROUP("照り")
        NS_REFLECT_FIELD(m_glowDiameterBase, "照りの直径の基準")
        NS_REFLECT_FIELD(m_glowDiameterPerPower, "照りの直径の威力あたり")
        NS_REFLECT_GROUP("弾かれ線")
        NS_REFLECT_FIELD(m_recoilCount, "弾かれ線の本数")
        NS_REFLECT_FIELD(m_wideRecoilCount, "大きな外れの弾かれ線の本数")
        NS_REFLECT_FIELD(m_recoilLengthBase, "弾かれ線の長さの基準")
        NS_REFLECT_FIELD(m_recoilLengthPerRebound, "弾かれ線の長さの反動の比あたり")
        NS_REFLECT_GROUP("当たりの粉")
        NS_REFLECT_FIELD(m_dustCountBase, "当たりの粉の数の基準")
        NS_REFLECT_FIELD(m_dustCountMassLimit, "当たりの粉の数を増やす質量の上限")
        NS_REFLECT_FIELD(m_dustScaleBase, "当たりの粉の大きさの基準")
        NS_REFLECT_FIELD(m_dustScalePerRootMass, "当たりの粉の大きさの質量の平方根あたり")
        NS_REFLECT_FIELD(m_dustScalePerPower, "当たりの粉の大きさの威力あたりの伸び")
        NS_REFLECT_GROUP("着地の粉")
        NS_REFLECT_FIELD(m_landDustRadiusBase, "着地の粉の半径の基準")
        NS_REFLECT_FIELD(m_landDustRadiusPerFallSpeed, "着地の粉の半径の落ちる速さあたり")
        NS_REFLECT_END()

    private:
        // 段で変わる層の形を段ごとの 1 行から埋める。段を足したら行を足す
        void ApplyTierRow(ImpactShape& shape, const NS::Game::Level::ImpactRecord& impact, float power) const noexcept;
        // 当たりの絵の頭から数えた当たり 1 回の段取り。層の番号 0 はまだ出していない印
        struct HitPlan
        {
            bool active = false;
            int freezeStep = 0;   // 当たりの絵の頭の EffectLayerList のフレーム
            int releaseStep = -1; // 飛びの絵の頭のフレーム。まだ始めていなければ -1
            ImpactShape shape;
            NS::Core::Vector3 contact;    // 接触点。自機の玉の縁の、相手へ向いた点
            NS::Core::Vector3 launchDir;  // 相手の飛ぶ水平の向き
            NS::Core::Vector3 sideDir;    // 相手の面に沿った、横ずれの側の水平の向き
            NS::Core::Vector3 scrapeDir;  // 大きな外れの火花の向き。ImpactAim::sparkDir と同じ決め方
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

        // 飛びの絵の頭から、自機の反動が終わるまでの尾。当たりの段取りより長く残る。層の番号 0 は無い印
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
        // 飛びの絵の頭に反動の尾を出す
        void BeginFlight(NS::Gfx::EffectScene* effects);
        // 反動の尾を自機へ付いていかせる。頂点で親を止める
        void AdvanceFlight(NS::Gfx::EffectScene* effects);
        // 反動の着地のフレームに足元へ粉を出し、次のフレームのために縦の速さを控える
        void AdvanceLanding(NS::Gfx::EffectScene* effects);
        // 出した層を lifeSteps フレーム後に消す
        void StopLater(std::uint32_t id, int lifeSteps);
        void RunScheduledStops(NS::Gfx::EffectScene* effects);
        // 効果を掛ける前の視点の位置。カメラが無い・姿が決まらない世界では空
        [[nodiscard]] std::optional<NS::Core::Vector3> CameraPosition() const;

        EffectLayerList m_layers;
        NS::Game::Level::ImpactResolver* m_resolver = nullptr;
        ::Player* m_player = nullptr;
        HitPlan m_plan;
        Flight m_flight;
        std::vector<ScheduledStop> m_scheduledStops;
        float m_lastVerticalVelocity = 0.0f; // 前のフレームの自機の縦の速さ (m/s)。着地のフレームは既に 0
        bool m_landingDustPlayed = false;    // この反動の着地の粉を出した。反動を抜けたら戻す
        ImpactAim m_aim;
        std::vector<std::uint32_t> m_dusts; // 出した粉。次の当たりの絵の頭で親を止める
        bool m_hitRequested = false;        // 事象が置いた、当たりの絵を始める頼み。OnUpdate が読んで消す
        bool m_flightRequested = false;     // 事象が置いた、飛びの絵を始める頼み。OnUpdate が読んで消す

        float m_coreDiameterBase = 0.3f;
        float m_coreDiameterPerPower = 0.2f;
        float m_coreDiameterMax = 0.7f;
        float m_coreBirthScale = 0.5f;
        float m_streakLengthBase = 4.0f;
        float m_streakLengthPerPower = 2.0f;
        float m_streakEndThickness = 0.3f;
        float m_ringRadiusBase = 0.5f;
        float m_ringRadiusPerPower = 0.6f;
        float m_ringStartRadius = 0.3f;
        float m_ringFaceCamera = 1.0f;
        int m_sparkCountMin = 10;
        int m_sparkCountMax = 30;
        float m_sparkSpeedBase = 6.0f;
        float m_sparkSpeedPerLaunch = 3.0f;
        int m_wideSparkCount = 16;
        float m_wideSparkSpeed = 4.0f;
        // 外れの火花の絵の全体に掛ける大きさ。粒の大きさ・散る範囲・速さが一緒に伸びる
        // 本人「HTML のやつの 3 倍ぐらい大きくしてほしい」から 3。1 では後ろからのカメラで自機の玉の陰に入った
        float m_wideSparkScale = 3.0f;
        float m_emberShare = 7.0f;
        float m_glowDiameterBase = 2.0f;
        float m_glowDiameterPerPower = 1.6f;
        int m_recoilCount = 8;
        // 外れは弾かれた手応えが来ない「すかし」。弾かれ線は弾き返された印に見えるので出さない
        int m_wideRecoilCount = 0;
        float m_recoilLengthBase = 0.6f;
        float m_recoilLengthPerRebound = 0.5f;
        int m_dustCountBase = 4;
        float m_dustCountMassLimit = 4.0f;
        float m_dustScaleBase = 0.8f;
        float m_dustScalePerRootMass = 0.3f;
        float m_dustScalePerPower = 0.5f;
        float m_landDustRadiusBase = 1.2f;
        float m_landDustRadiusPerFallSpeed = 0.04f;
    };
} // namespace NS::Game::Player
