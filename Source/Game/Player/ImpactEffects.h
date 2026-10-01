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
    class PlayerParams;

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
        NS::Core::Vector3 contact;      //!< 接触点。自機の玉の縁の、相手へ向いた点
        NS::Core::Vector3 sparkDir;     //!< 火花の向き。大きな外れは横ずれの側と飛ぶ向きと上の間、他は相手の飛ぶ向き
        NS::Core::Vector3 recoilDir;    //!< 弾かれ線の向き。自機の反動の初速の向き
        NS::Core::Vector3 recoilOrigin; //!< 弾かれ線を出した所。自機の玉の縁の、反動の向きの逆の点
        NS::Core::Vector3 dustOrigin;   //!< 当たりの粉の輪の真ん中。相手が居た床から、自機と逆の側へずらした点
        NS::Core::Vector3 ringNormal;   //!< 輪の面の法線。相手の飛ぶ向きをカメラへ起こした向き
    };

    //! @brief 自機が当ててから着地するまでのエフェクトの層を出し、出すと決めた記録を持つ
    //! @details 受け持つ層の名前は impact.・rebound.・land. で始まる
    //! 同居する ImpactResolver の止めの頭と明けを読み、止めの頭に核と照り、次のフレームから光条、
    //! 3 フレーム目から輪、明けに粉、明けの 4 フレーム後に弾かれ線を出す。火花は中心近くが 3 フレーム目、他は止めの頭
    //! 中心近くは核が落ちるフレームに火の粉を出す
    //! 明けに、反動に入った自機へ反動の尾を出し、毎フレーム付いていかせる。反動の尾は頂点で親を止めて 8
    //! フレーム後に消す 反動の着地 (着地の潰れと同じフレーム) には足元へ粉を出す
    //! 飛ばした相手の飛び出しの尾と落ちた所の粉は、相手が自分で出す (LaunchEffects)。相手の部品は読まない
    //! 止めが 0 の当たりでは何も出さない
    //! 層の時間 (留まり・広がり・消えるフレーム) はここが持ち、形と色は絵が持つ
    //! 描画の無い世界でも記録は残し、試しと Replay は Layers を読む
    //! Player::Update が最後に呼ぶ。同じフレームの ImpactResolver が決めた止めの頭と明けと、自機の移動の後に走る。
    //! 物理の段と、飛ばした相手が自分の段階を切り替える Triggers の段よりは前に走る
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
        [[nodiscard]] static NS::Core::Vector3 ReboundTrailHeading(
            const NS::Core::Vector3& velocity,
            const NS::Core::Vector3& ballCenter,
            const std::optional<NS::Core::Vector3>& cameraPosition) noexcept;

        //! 直近の当たりの層を置いた所と向き。まだ当たっていなければ全部 0
        [[nodiscard]] const ImpactAim& LastAim() const noexcept { return m_aim; }

        // 当たりの層の大きさと量は当てた瞬間の手触りそのもの。Inspector で触って詰められるよう公開する
        NS_REFLECT_NONE(ImpactEffects, NS::Obj::Component)

    private:
        [[nodiscard]] const PlayerParams& Tuning() const noexcept;
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
    };
} // namespace NS::Game::Player
