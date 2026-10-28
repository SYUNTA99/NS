#pragma once

#include "Game/Level/LaunchEffectSettings.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/MapObjParams.h"
#include "Game/Level/PropFlight.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Sphere.h"
#include "NSlib/CoreObject/Object.h"

#include "NSlib/Core/Types.h"
#include <vector>

namespace NS
{
    class IGetEffect;
}

namespace GL
{
    namespace Level
    {
        //! 押し飛ばされたアクターの尾と、着地の粉を出す部品
        //! 尾は曲線を離れた時に消す。段で色、飛ばしの比で長さ、威力と質量で粉の大きさを決める
        class LaunchEffects : public NS::Object
        {
        public:
            // 絵は user (持ち主のアクター) のシーンの絵の管理へ出す

            //! 尾と粉の絵を先に読む。持ち主の OnAssetsLoaded が呼ぶ
            void Preload(const NS::IGetEffect* user);

            //! 出ている尾を置物の今の位置と向きへ動かし、曲線を離れたら消す。持ち主の見た目の段が呼ぶ
            void Update(const NS::IGetEffect* user, const PropFlight& flight);

            //! 押し飛ばされた所から尾を出す。前の尾が残っていれば消してから出す
            //! 段で尾の色を引き (中心近くは橙、大きな外れは灰)、飛ばしの比で尾を長く残し、
            //!          威力と質量で落ちた所の粉を大きくする。飛ぶ水平の向きは速さが 0 の時の尾の向き
            //! @param[in] flight 放した直後の飛び。尾の頭を今の位置に置く。曲線の上でなければ尾を出さない
            //! @param[in] sphere 置物の当たりの世界座標の球。尾の太さを直径に合わせる
            void BeginTrail(const NS::IGetEffect* user,
                            const TackleReleaseDesc& release,
                            const PropFlight& flight,
                            const MapObjParams* params,
                            const NS::Sphere& sphere);

            //! 尾を消し、落ちた所へ粉を出す
            //! @param[in] position 床に触れた点。世界座標
            //! @param[in] normal 触れた床の法線。粉はこの向きへ少し浮かせて置く
            void NotifyLanding(const NS::IGetEffect* user, const NS::Vector3& position, const NS::Vector3& normal);
            //! 出ている尾を消す。粉は出さない
            void CancelTrail(const NS::IGetEffect* user);

            //! 出ている尾が残るフレーム数。尾が無ければ 0
            [[nodiscard]] int TrailFrames() const noexcept { return m_trailFrames; }

            //! 落ちた所の粉の大きさ (m)
            [[nodiscard]] float LandDustScale() const noexcept { return m_landDustScale; }

            void Configure(const LaunchEffectSettings* settings) noexcept
            {
                NS_ASSERT(Game, settings != nullptr, "演出の設定が空");
                m_settings = settings;
            }
            const LaunchEffectSettings& Settings() const noexcept { return *m_settings; }

            NS_REFLECT_NONE(LaunchEffects, NS::Object)

        private:
            inline static const LaunchEffectSettings m_defaultSettings{};
            const LaunchEffectSettings* m_settings = &m_defaultSettings;
            // 尾を消し、床に落ちていればその場へ粉を出す
            void EndTrail(const NS::IGetEffect* user);

            NS::uint32 m_trail = 0; // 飛び出しの尾。消したら 0
            int m_trailFrames = 0;
            float m_trailScale = 1.0f; // 尾の再生の大きさ。自分の直径 (m)
            NS::Vector3 m_launchDir{1.0f, 0.0f, 0.0f};
            float m_landDustScale = 0.0f; // 落ちた所の粉の大きさ (m)
        };
    } // namespace Level
} // namespace GL
