#pragma once

#include "Game/Player/ChargeEffectSettings.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/Gfx/Render/EffectTypes.h"

#include "NSlib/Core/Types.h"
#include <string_view>
#include <vector>

namespace NS
{
    class IGetEffect;
}

namespace GL
{
    namespace Level
    {
        struct ImpactAnswer;
    } // namespace Level
} // namespace GL

namespace GL
{
    namespace Player
    {
        class BodySlamStatus;
        class ChargeStatus;
        class PlayerAppearance;
        struct PlayerMotion;

        //! 溜めと放しの演出を出し、寿命を管理する部品
        //! PlayerAppearance の後に更新する
        class ChargeEffects : public NS::Object
        {
        public:
            // 絵は user (持ち主のアクター) のシーンの絵の管理へ出す

            //! 溜めと放しの絵を先に読む。持ち主の OnAssetsLoaded が呼ぶ
            void Preload(const NS::IGetEffect* user);

            //! 押し・溜め・放しに合わせて演出を出し、玉へ付いていかせる。持ち主の見た目の段が呼ぶ
            //! @param[in] center 玉の中心。持ち主のルートの位置。世界座標
            //! @param[in] aimDirection 今放せば出る水平の向き (PlayerActor::ReleaseDirection)
            //! @param[in] appearance 玉の回転の正。このフレームに玉が回った角度を読む。空でないこと
            //! @param[in] impact 当たりの裁定の答え。当たりの状態へ移るフレームと接触の見込みを読む
            void Update(const NS::IGetEffect* user,
                        const NS::Vector3& center,
                        const ChargeStatus& charge,
                        const BodySlamStatus& slam,
                        const PlayerMotion& motion,
                        const NS::Vector3& aimDirection,
                        const PlayerAppearance* appearance,
                        const GL::Level::ImpactAnswer& impact);

            //! 番号を控えている溜めと放しのエフェクトを消す。持ち主の ResetState が呼ぶ
            void Clear(const NS::IGetEffect* user);

            //! 溜まる光へ動的入力 1 番で渡す、溜めきってからの数
            //! @return 押し続けている間、溜めきったフレームを 1 とした数。溜めきる前と押していない間は 0
            [[nodiscard]] int FramesSinceFullCharge() const noexcept { return m_framesSinceFull; }

            //! 放しの弾けの倍率を返す。charge01 は 0〜1 に丸め、非有限は 0 とする
            [[nodiscard]] float ReleaseBurstScale(float charge01) const noexcept;

            //! +Z を direction の水平成分へ向ける。水平の向きが無効なら無回転
            [[nodiscard]] static NS::Quaternion YawToward(const NS::Vector3& direction) noexcept;

            void Configure(const ChargeEffectSettings* settings) noexcept
            {
                NS_ASSERT(Game, settings != nullptr, "演出の設定が空");
                m_settings = settings;
            }
            const ChargeEffectSettings& Settings() const noexcept { return *m_settings; }

            NS_REFLECT_NONE(ChargeEffects, NS::Object)

        private:
            inline static const ChargeEffectSettings m_defaultSettings{};
            const ChargeEffectSettings* m_settings = &m_defaultSettings;
            int m_chargeInput = 0;
            int m_burstScaleInput = 1;
            int m_fullFramesInput = 1;
            NS::Gfx::EffectPlayDesc PlayDesc(const NS::Vector3& position,
                                             const NS::Quaternion& rotation,
                                             float charge01) const noexcept;
            void ForgetEndedEffects(const NS::IGetEffect* user) noexcept;
            void StartPress(const NS::IGetEffect* user, const NS::Vector3& center, const NS::Vector3& aimDirection);
            void StartCharging(const NS::IGetEffect* user,
                               const NS::Vector3& center,
                               const NS::Vector3& aimDirection,
                               float charge01);
            void StartFullFlash(const NS::IGetEffect* user, const NS::Vector3& center);
            // 紫の揺れが端へ来たフレームに、その側へ火花を散らす
            void PlaySwaySparks(const NS::IGetEffect* user, const NS::Vector3& center, const ChargeStatus& charge);
            // 押している間の層を玉へ付いていかせ、溜め量を渡す
            void FollowHeldEffects(const NS::IGetEffect* user,
                                   const NS::Vector3& center,
                                   const NS::Vector3& aimDirection,
                                   float charge01,
                                   float spinDegreesThisFrame) noexcept;
            void ClearHeldEffects(const NS::IGetEffect* user) noexcept;
            void StartRelease(const NS::IGetEffect* user, const NS::Vector3& center, const BodySlamStatus& slam);
            void UpdateTrail(const NS::IGetEffect* user,
                             const NS::Vector3& center,
                             const BodySlamStatus& slam,
                             bool slamming,
                             const GL::Level::ImpactAnswer& impact) noexcept;
            void StopTrailRoot(const NS::IGetEffect* user) noexcept;
            // 層を今の位置・向き・大きさへ置き直し、向きを記録に書く。描画の無い世界では記録だけ書く
            void Place(const NS::IGetEffect* user,
                       NS::uint32 id,
                       const NS::Vector3& position,
                       const NS::Quaternion& rotation) noexcept;
            void SetCharge(const NS::IGetEffect* user, NS::uint32 id, float charge01) const noexcept;
            void SetInput(const NS::IGetEffect* user, NS::uint32 id, int index, float value) const noexcept;
            void StopAndClear(const NS::IGetEffect* user, NS::uint32& id) noexcept;
            // 跳ね返りか破壊が起きたか、触れる見込みが m_burstClearFrames 以内か
            [[nodiscard]] bool IsContactNear(const GL::Level::ImpactAnswer& impact) const noexcept;

            // 出ているエフェクトの記録の番号。0 は出ていない
            NS::uint32 m_curl = 0;
            NS::uint32 m_spin = 0;
            NS::uint32 m_grind = 0;
            NS::uint32 m_gather = 0;
            NS::uint32 m_full = 0;
            NS::uint32 m_trail = 0;
            NS::uint32 m_burst = 0;

            int m_fullStep = 0;        // 溜めきりの閃きを出したフレームの記録の番号
            int m_framesSinceFull = 0; // FramesSinceFullCharge の値。Update が毎フレーム決める
            bool m_fullShown = false;  // この押しで溜めきりの閃きを出したか。押し直すまで 2 回目を出さない
            bool m_wasHeld = false;
            bool m_wasSlamming = false;
            float m_spinDegrees = 0.0f; // 押してから玉が回った角度の累計。回転の弧の板の回りの角度
            NS::Vector3 m_slamDirection;
        };
    } // namespace Player
} // namespace GL
