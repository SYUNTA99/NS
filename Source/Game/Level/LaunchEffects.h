#pragma once

#include "NSlib/CoreObject/Asset/Effect.h"
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
        //! 飛行への追従と着地の時機を持ち、粒の寿命と粉の広がりは素材が決める
        class LaunchEffects : public NS::Object
        {
        public:
            // 絵は user (持ち主のアクター) のシーンの絵の管理へ出す

            //! 尾と粉の絵を先に読む。持ち主の OnAssetsLoaded が呼ぶ
            void Preload(const NS::IGetEffect* user);

            //! 出ている尾を置物の今の位置と向きへ動かし、曲線を離れたら消す。持ち主の見た目の段が呼ぶ
            void Update(const NS::IGetEffect* user, const PropFlight& flight);

            //! 押し飛ばされた所から尾を出す。前の尾が残っていれば消してから出す
            //! 飛ぶ水平の向きは、速さが0の時の尾の向きに使う
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

            NS_REFLECT_BEGIN(LaunchEffects, NS::Object)
            NS_FIELD(m_landDustLife, "着地の粉の寿命フレーム")
            NS_FIELD(m_dustRingLift, "着地の粉を浮かせる高さ")
            NS_FIELD(m_launchTrailAsset, "飛び出しの尾の資産")
            NS_FIELD(m_launchLandDustAsset, "着地の粉の資産")
            NS_REFLECT_END()

        private:
            int m_landDustLife = 24;
            //! 床の法線方向への離隔 (メートル)
            float m_dustRingLift = 0.3f;
            NS::Effect* m_launchTrailAsset = nullptr;
            NS::Effect* m_launchLandDustAsset = nullptr;

            void EndTrail(const NS::IGetEffect* user);

            NS::uint32 m_trail = 0; // 飛び出しの尾。消したら 0
            float m_trailScale = 1.0f; // 尾の再生の大きさ。自分の直径 (m)
            NS::Vector3 m_launchDir{1.0f, 0.0f, 0.0f};
            float m_landPower = 0.0f;
            float m_landRootMass = 0.0f;
        };
    } // namespace Level
} // namespace GL
