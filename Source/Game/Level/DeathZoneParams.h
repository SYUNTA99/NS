#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/AssetField.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"

#include <algorithm>

namespace NS
{
    class HitSensor;
    class SceneActor;
} // namespace NS

namespace GL
{
    namespace Level
    {
        //! 自機を即死させる物 (落下死の範囲・水面) が、その時に出す音とエフェクトと、やり直すまでの長さ
        //! 死ぬ時の音とエフェクトを決めるのはここだけで、自機は持たない
        class DeathZoneParams : public NS::Object
        {
        public:
            //! 入った時の音。空の欄は nullptr
            [[nodiscard]] const NS::Sound* EnterSound() const noexcept { return m_enterSound; }
            [[nodiscard]] float EnterVolume() const noexcept { return m_enterVolume; }
            //! 入った時のエフェクト。空の欄は nullptr
            [[nodiscard]] const NS::Effect* EnterEffect() const noexcept { return m_enterEffect; }
            //! 死んだ所を見せてからやり直すまでのフレーム数。負の値は 0 として読む
            [[nodiscard]] int RestartDelayFrames() const noexcept { return std::max(m_restartDelayFrames, 0); }

            //! other の持ち主へ即死を知らせ、応じた時だけ、入った時の音を鳴らして position にエフェクトを出す
            //! 落下死の範囲と水面が同じ手順で使う
            //! @param[in] owner 音とエフェクトを出すアクター。消えない側 (範囲か水面) を渡す。消える自機が出すと、
            //!                  粒が出る前にエフェクトの親が止まる
            //! @param[in] self 知らせの送り手のセンサー
            //! @param[in] other 知らせの受け手 (自機の体) のセンサー
            //! @param[in] position エフェクトを出すワールド座標の位置。受け手が消える前に読んだ値を渡す
            //! @return 受け手が応じたか
            bool KillAndPlayEntry(const NS::SceneActor* owner,
                                  NS::HitSensor* self,
                                  NS::HitSensor* other,
                                  const NS::Vector3& position) const;

            NS_REFLECT_BEGIN(DeathZoneParams, NS::Object)
            // 範囲ごとに違う物を出すので、シーンに置いた範囲でも替えられる欄にする
            NS_GLOBAL_FIELD(m_enterSound, "入った時の音")
            NS_GLOBAL_FIELD(m_enterVolume, "入った時の音の音量", NS::FieldRange::Unit())
            NS_GLOBAL_FIELD(m_enterEffect, "入った時のエフェクト")
            NS_GLOBAL_FIELD(m_restartDelayFrames, "やり直すまでのフレーム数")
            NS_REFLECT_END()

        private:
            NS::Sound* m_enterSound = nullptr;
            float m_enterVolume = 0.8f;
            NS::Effect* m_enterEffect = nullptr;
            int m_restartDelayFrames = 0; // 0 は死んだフレームにやり直す。海の範囲は、しぶきを見せる間だけ待つ
        };
    } // namespace Level
} // namespace GL
