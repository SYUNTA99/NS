#pragma once

#include <string>

#include "Game/Level/HitTier.h"
#include "Game/Player/EffectLayerList.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Object/SubObject.h"

#include <cstdint>
#include <vector>

namespace NS::Game::Level
{
    class MapObj;
    class MapObjParams;

    //! @brief 押し飛ばされた物が自分で出す飛び出しの尾と、床に落ちた所の粉。置物に載せる
    //! @details 受け持つ層の名前は launch. で始まる。飛んでいる自分の後ろへ尾を付けていき、曲線を離れたフレームで消す
    //! 尾の色は当たりの段、残る長さは飛ばしの比、粉の大きさは威力と自分の重さで決める
    //! 描画の無い世界でも記録は残し、試しは Layers を読む
    //! MapObj の観測の段 (ObserveStep) が発光の歩を始め、見た目の段 (VisualStep) が進める。
    //! 尾を出す BeginTrail は自機の段に届く放しの知らせの中で走り、置物が自分を動かす Triggers の段より前なので、
    //! 尾の頭は放した時の速度で 1 フレーム先へ置く
    class LaunchEffects : public NS::Obj::SubObject
    {
    public:
        LaunchEffects() noexcept;

        //! 同じ置物の飛び方を控え、描画のある世界なら層の絵を読み込む
        void OnStart() override;

        //! 記録のフレームを 1 つ進め、止めると決めたフレームに来た層を止める
        void BeginStep();
        //! 曲線を飛んでいる間は尾の頭を自分の位置へ付けていき、曲線を離れたら尾を消す
        void OnUpdate() override;

        //! @brief 押し飛ばされた所から尾を出す。前の尾が残っていれば消してから出す
        //! @param[in] tier 当たりの段。尾の色を段ごとの表で引き、中心近くは橙、大きな外れは灰
        //! @param[in] power 最終威力。落ちた所の粉を大きくする
        //! @param[in] launchScale 飛ばしの比。尾を長く残す
        //! @param[in] launchDir 飛ぶ水平の向き。速さが 0 の時の尾の向き
        void BeginTrail(HitTier tier, float power, float launchScale, const NS::Vector3& launchDir);

        //! @brief 尾を消し、落ちた所へ粉を出す
        //! @param[in] position 床に触れた点。世界座標
        //! @param[in] normal 触れた床の法線。粉はこの向きへ少し浮かせて置く
        void NotifyLanding(const NS::Vector3& position, const NS::Vector3& normal);
        //! 出ている尾を消す。粉は出さない
        void CancelTrail();

        //! 出すと決めた層の記録
        [[nodiscard]] const NS::Game::Player::EffectLayerList& Layers() const noexcept { return m_layers; }

        //! 出ている尾が残るフレーム数。尾が無ければ 0
        [[nodiscard]] int TrailFrames() const noexcept { return m_trailFrames; }

        //! 落ちた所の粉の大きさ (m)
        [[nodiscard]] float LandDustScale() const noexcept { return m_landDustScale; }

        // 飛んでいく物の尾と落ちた所の粉は、飛ばした手応えそのもの。Inspector で触って詰められるよう公開する
        NS_REFLECT_BEGIN(LaunchEffects, NS::Obj::SubObject)
        NS_REFLECT_FIELD(m_landDustLife, "着地の粉の寿命フレーム")
        NS_REFLECT_FIELD(m_dustRingLift, "着地の粉を浮かせる高さ")
        NS_REFLECT_FIELD(m_launchTrailAsset, "飛び出しの尾の資産")
        NS_REFLECT_FIELD(m_launchLandDustAsset, "着地の粉の資産")
        NS_REFLECT_FIELD(m_landDustAssetRadius, "着地の粉の資産半径")
        NS_REFLECT_END()

    private:
        std::string m_launchTrailAsset = "launch.trail";
        std::string m_launchLandDustAsset = "launch.landDust";
        float m_landDustAssetRadius = 1.2f;
        int m_landDustLife = 24;
        //! 床の法線の向きへ浮かせる。単位はメートル
        float m_dustRingLift = 0.3f;
        [[nodiscard]] const MapObjParams& Tuning() const noexcept;
        // 尾を消し、床に落ちていればその場へ粉を出す
        void EndTrail(NS::Gfx::EffectScene* effects);

        NS::Game::Player::EffectLayerList m_layers;
        MapObj* m_body = nullptr;
        std::uint32_t m_trail = 0;                 // 飛び出しの尾。消したら 0
        int m_trailStartStep = 0;                  // 尾を出したフレーム。このフレームは出した姿のまま
        int m_trailFrames = 0;                     // 尾が残るフレーム数
        float m_trailScale = 1.0f;                 // 尾の再生の大きさ。自分の直径 (m)
        NS::Vector3 m_launchDir{1.0f, 0.0f, 0.0f}; // 飛ぶ水平の向き
        float m_landDustScale = 0.0f;              // 落ちた所の粉の大きさ (m)
    };
} // namespace NS::Game::Level
