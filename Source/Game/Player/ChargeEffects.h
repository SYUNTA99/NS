#pragma once

#include "Game/Player/EffectLayerList.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Reflection/Reflection.h"

#include <cstdint>
#include <string_view>
#include <vector>

class Player;

namespace NS::Game::Level
{
    class ImpactResolver;
} // namespace NS::Game::Level

namespace NS::Game::Player
{
    class PlayerAppearance;
    class PlayerParams;

    //! @brief 自機の溜めと放しのエフェクトの層を出し、出すと決めた記録を持つ
    //! @details 押した・溜めに入った・溜めきった・放した・突進が終わった・当たりの止めが始まった、を同居する部品から
    //! 読み、層を出す・付いていかせる・消す。層と出るフレーム (p = 押した、q = 溜めに入った、F = 溜めきった、
    //! u = 突進が始まった、c = 止めの頭):
    //! - charge.curl 丸まりの殻: p に出し p + k_CurlSteps に消す。それより前に放したら放したフレームに消す
    //! - charge.spin 回転の弧: p に出し、放したフレームに消す。板の回りの角度に玉が回った角度の累計を渡す
    //! - charge.gather 溜まる光: p に出し、放したフレームに消す。溜めきってからの数 (FramesSinceFullCharge) を
    //!   動的入力 1 番で渡す。根は毎フレーム狙いの線の水平の向きへ回し、光の点を手前は低く・奥は高く生ませる
    //! - charge.grind 削る粉: q に出し、放したフレームに消す
    //! - charge.full 溜めきりの閃き: 1 回の押しで 1 回だけ F に出し、F + k_FullFlashSteps に消す。
    //!   それより前に放したら放したフレームに消す。消すまで玉へ付いていく
    //! - release.burst 放しの弾け: u に出し u + k_BurstSteps に消す。輪・丸屋根・筋の大きさは ReleaseBurstScale を
    //!   動的入力 1 番で渡す。はじけの光と散って残る筋は大きさを変えない。放した所に置いたまま。
    //!   触れる前の時計が走り始めたら (ImpactResolver::IsBeforeContact) そのフレームに、線の先の相手に触れる見込みが
    //!   k_BurstClearFrames 以内になったらそのフレームに、どちらも無いまま触れたら触れたフレームに消す
    //! - slam.trail 突進の尾: u に出す。当たったら c で、当たらずに突進が終わったら終わったフレームで親を止め、
    //!   止めた k_TrailFadeSteps フレーム後に消す
    //! 溜めている間の層には溜め量 (Player::ChargeJudge) を動的入力 0 番で毎フレーム渡す。
    //! 描画の無い世界でも記録は残し、試しと Replay は Layers を読む
    //! Player の見た目の段 (VisualStep) が PlayerAppearance の後に呼ぶ。
    //! 同じフレームに PlayerAppearance が回した玉の向きより後に走る
    //! 依存: EffectLayerList, PlayerAppearance, Player, NS::Game::Level::ImpactResolver
    // TODO: エフェクトは固定ステップで進み、付いていく層は固定ステップの位置へ置く。60 を超える画面で付いていく層が
    // 段々に見えたら、描画フレームごとに描く時の補間の位置で渡す形へ移す
    class ChargeEffects : public NS::Obj::Component
    {
    public:
        //! 丸まりの殻を出してから消すまでのフレーム数
        static constexpr int k_CurlSteps = 6;
        //! 溜めきりの閃きを出してから消すまでのフレーム数。横の閃き 4 フレームと、続く縦の柱 2 フレーム
        static constexpr int k_FullFlashSteps = 6;
        //! 放しの弾けを出してから消すまでのフレーム数。散って残る粒の寿命 21 フレームと同じ
        static constexpr int k_BurstSteps = 21;
        //! 線の先の相手に触れる見込みがこのフレーム数以内になったら、放しの弾けを消す。触れる直前の
        //! 3 フレームは二人が近づくのを見せ、外れでも光で手応えを期待させない
        static constexpr int k_BurstClearFrames = 3;
        //! 突進の尾の親を止めてから消すまでのフレーム数。尾の点の寿命と同じ
        static constexpr int k_TrailFadeSteps = 6;

        static constexpr std::string_view k_Curl = "charge.curl";
        static constexpr std::string_view k_Spin = "charge.spin";
        static constexpr std::string_view k_Grind = "charge.grind";
        static constexpr std::string_view k_Gather = "charge.gather";
        static constexpr std::string_view k_Full = "charge.full";
        static constexpr std::string_view k_Burst = "release.burst";
        static constexpr std::string_view k_Trail = "slam.trail";
        // 紫の揺れの端で散らす火花。当たりの火花の絵の、擦れて飛ぶ節 (動的入力 1 番) を使う
        static constexpr std::string_view k_SwaySparks = "impact.sparks";

        ChargeEffects() noexcept;

        //! 同居する部品を控え、描画のある世界なら受け持つ層の絵を読み込む
        void OnStart() override;
        //! 記録のフレームを 1 つ進め、このフレームの結末を読んで層を出す・付いていかせる・消す
        void OnUpdate() override;
        //! 残っている層を全部消す
        void OnEndPlay() override;

        //! 出すと決めた層の記録
        [[nodiscard]] const EffectLayerList& Layers() const noexcept { return m_layers; }

        //! @brief 溜まる光へ動的入力 1 番で渡す、溜めきってからの数
        //! @return 押し続けている間、溜めきったフレームを 1 とした数。溜めきる前と押していない間は 0
        [[nodiscard]] int FramesSinceFullCharge() const noexcept { return m_framesSinceFull; }

        //! @brief 放しの弾けの大きさの倍率
        //! @param[in] charge01 突進を出した時の溜め量 0..1。範囲の外は丸める。有限でなければ 0 とみなす
        //! @return 欄「タップの弾けの大きさ」+ 欄「溜めきりで足す弾けの大きさ」× 溜め量。既定でタップ 0.75、溜めきり 1
        [[nodiscard]] float ReleaseBurstScale(float charge01) const noexcept;

        //! @brief +Z を水平の向き direction へ向ける Y 軸まわりの回転
        //! @param[in] direction 向ける先。縦の成分は使わない
        //! @return 回転。水平の長さが 0 か有限でなければ回さない
        [[nodiscard]] static NS::Core::Quaternion YawToward(const NS::Core::Vector3& direction) noexcept;

        NS_REFLECT_NONE(ChargeEffects, NS::Obj::Component)

    private:
        [[nodiscard]] const PlayerParams& Tuning() const noexcept;
        // 決めたフレームに消す層
        struct ScheduledStop
        {
            std::uint32_t id = 0;
            int step = 0;
        };

        void StopDueLayers(NS::Gfx::EffectScene* effects) noexcept;
        void StartPress(NS::Gfx::EffectScene* effects, const NS::Core::Vector3& center);
        void StartCharging(NS::Gfx::EffectScene* effects, const NS::Core::Vector3& center);
        void StartFullFlash(NS::Gfx::EffectScene* effects, const NS::Core::Vector3& center);
        // 紫の揺れが端へ来たフレームに、その側へ火花を散らす
        void PlaySwaySparks(NS::Gfx::EffectScene* effects, const NS::Core::Vector3& center);
        // 押している間の層を玉へ付いていかせ、溜め量を渡す
        void FollowHeldLayers(NS::Gfx::EffectScene* effects, const NS::Core::Vector3& center, float charge01) noexcept;
        void ClearHeldLayers(NS::Gfx::EffectScene* effects) noexcept;
        void StartRelease(NS::Gfx::EffectScene* effects, const NS::Core::Vector3& center);
        void UpdateTrail(NS::Gfx::EffectScene* effects, const NS::Core::Vector3& center, bool slamming) noexcept;
        void StopTrailRoot(NS::Gfx::EffectScene* effects) noexcept;
        // 層を今の位置・向き・大きさへ置き直し、向きを記録に書く。描画の無い世界では記録だけ書く
        void Place(NS::Gfx::EffectScene* effects,
                   std::uint32_t id,
                   const NS::Core::Vector3& position,
                   const NS::Core::Quaternion& rotation) noexcept;
        void SetCharge(NS::Gfx::EffectScene* effects, std::uint32_t id, float charge01) const noexcept;
        // 動的入力 index 番に value を入れる。描画の無い世界か、再生できなかった層なら何もしない
        void SetInput(NS::Gfx::EffectScene* effects, std::uint32_t id, int index, float value) const noexcept;
        // 押している間に狙っている水平の向き。狙いの線があればその向き、無ければ Player::AimDirection
        [[nodiscard]] NS::Core::Vector3 HeldAimDirection() const noexcept;
        void StopLayer(NS::Gfx::EffectScene* effects, std::uint32_t& id) noexcept;
        // このフレームに触れたか、線の先の相手に触れる見込みが k_BurstClearFrames 以内か
        [[nodiscard]] bool IsContactNear() const noexcept;

        // 放しの弾けの輪・丸屋根・筋の大きさ。溜めきりで輪が半径 3.2 m まで広がる
        // タップの 0.75 は輪が半径 2.4 m で、押した瞬間の丸まりの殻 (半径 1.2 m) の倍。0.4 (半径 1.3 m) は
        // 放した次のフレームの輪が殻と同じ大きさに見えた

        EffectLayerList m_layers;
        std::vector<ScheduledStop> m_scheduledStops;

        // 出ている層の記録の番号。0 は出ていない
        std::uint32_t m_curl = 0;
        std::uint32_t m_spin = 0;
        std::uint32_t m_grind = 0;
        std::uint32_t m_gather = 0;
        std::uint32_t m_full = 0;
        std::uint32_t m_trail = 0;
        std::uint32_t m_burst = 0;

        int m_fullStep = 0;                // 溜めきりの閃きを出したフレームの記録の番号
        int m_framesSinceFull = 0;         // FramesSinceFullCharge の値。OnUpdate が毎フレーム決める
        bool m_fullShown = false;          // この押しで溜めきりの閃きを出したか。押し直すまで 2 回目を出さない
        bool m_wasHeld = false;            // 前のフレームに押していたか
        bool m_wasSlamming = false;        // 前のフレームに突進していたか
        bool m_trailAwaitsFreeze = false;  // 当たりを検知して突進が終わった。次のフレームの止めの頭で尾の親を止める
        float m_spinDegrees = 0.0f;        // 押してから玉が回った角度の累計。回転の弧の板の回りの角度
        NS::Core::Vector3 m_slamDirection; // 突進の尾を向ける水平の向き

        const ::Player* m_actor = nullptr;              // 押し・溜め量・突進の速度・狙いの向きを答える自機。非所有
        const PlayerAppearance* m_appearance = nullptr; // 玉の回転の正。非所有
        const NS::Game::Level::ImpactResolver* m_resolver = nullptr; // 止めの頭の正。非所有
    };
} // namespace NS::Game::Player
