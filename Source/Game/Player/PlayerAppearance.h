#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Reflection/AssetRef.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Object.h"

#include "NSlib/Core/Types.h"
#include <string>

namespace NS
{
    namespace Gfx
    {
        class D3DMesh;
    }
} // namespace NS

namespace NS
{
    class Body;
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
        class ReboundStatus;
        class SkidStatus;
        struct MissTumble;
        struct PlayerMotion;

        //! @brief 自機の立ち姿・玉と、描画時の回転・変形を管理する部品
        //! @details 姿のメッシュは参照の欄から読み、姿ごとの倍率で大きさを合わせる。当たりの寸法には付いて行かない
        // TODO: 部分的な JSON 適用では Model の undo を再反映できない。ResetFromJson で直る
        class PlayerAppearance : public NS::Object
        {
        public:
            explicit PlayerAppearance(NS::SceneActor* owner) noexcept;

            //! @brief 玉の見た目へ持ち替える。既に玉なら何もしない
            //! @details 次の Update で、持ち主の丸まりに合わせ直す
            void Curl() noexcept;
            //! @brief 立ち姿へ戻す。既に立ち姿なら何もしない
            //! @details 次の Update で、持ち主の丸まりに合わせ直す
            void Uncurl() noexcept;
            [[nodiscard]] bool IsCurled() const noexcept { return m_curled; }

            //! @brief 今の姿のメッシュを同居する Model へ差し、姿の倍率を Model の局所の倍率へ書く
            //! @details 持ち主の OnAssetsLoaded と、丸まる・戻るが呼ぶ。読めていない姿は Model の今のメッシュを残す
            void ShowCurrentLook() noexcept;

            //! @brief 丸まりに合わせて姿を持ち替え、玉を回し、描く形の倍率を書く。持ち主の見た目の段が呼ぶ
            //! @param[in] aimDirection 狙いの線が無い時に、放せば出る水平の向き
            //! @param[in] body 反動の着地の判定に接地と縦の速さを読む。空でないこと
            //! @param[in] impact 当たりの裁定の答え。当たりの潰れと伸びと、何回目の当たりかを読む
            void Update(const PlayerMotion& motion,
                        const ChargeStatus& charge,
                        const BodySlamStatus& slam,
                        const ReboundStatus& rebound,
                        const SkidStatus& skid,
                        const NS::Vector3& aimDirection,
                        const NS::Body* body,
                        const GL::Level::ImpactAnswer& impact);

            //! @brief 描く形の倍率を補間なしで (1, 1, 1) へ戻し、着地の潰れを捨てる
            //! @details やり直しとプレイの終わりで持ち主が呼ぶ。次の Update からはその時の構えと当たりの形で組み直す
            void ResetDrawScale() noexcept;

            //! 押している間の構えの縦の倍率。押していない間は 1、溜めている間は構えの縮み、その前は押しの構えの縮み
            [[nodiscard]] float StanceHeight(const ChargeStatus& charge) const noexcept;

            //! 直前の Update で玉が回った角度を度で返す。立ち姿と止めの間は 0
            [[nodiscard]] float SpinDegreesThisFrame() const noexcept { return m_spinDegreesThisFrame; }
            //! @brief 玉を回している軸を返す
            //! @details 根の空間の単位ベクトル。正の角度で、玉の上面が 軸 × 上 の向きへ倒れる前転になる。
            //! 外れの反動の間は水平とは限らない
            //! @return 直前の Update で回した軸。止めの間は止まる前の軸、立ち姿では (1, 0, 0)
            [[nodiscard]] NS::Vector3 SpinAxis() const noexcept { return m_spinAxis; }

            NS_REFLECT_BEGIN(PlayerAppearance, NS::Object)
            NS_REFLECT_GROUP("姿")
            NS_REFLECT_FIELD(m_standingMesh, "立ち姿のメッシュ")
            NS_REFLECT_FIELD(m_ballMesh, "玉のメッシュ")
            NS_REFLECT_FIELD(m_standingScale, "立ち姿の倍率")
            NS_REFLECT_FIELD(m_ballScale, "玉の倍率")
            NS_REFLECT_GROUP("溜めの回転")
            NS_REFLECT_FIELD(m_emptyChargeSpinSpeed, "溜め 0 の回る速さ")
            NS_REFLECT_FIELD(m_fullChargeSpinSpeed, "溜めきりの回る速さ")
            NS_REFLECT_GROUP("外れの回り方")
            NS_REFLECT_FIELD(m_missTwistTurnsPerSecond, "外れの縁でのねじれの回転数")
            NS_REFLECT_FIELD(m_missSpinCarryRatio, "外れで当たる前の回転を引き継ぐ割合")
            NS_REFLECT_FIELD(m_missSpinBlendSteps, "外れの回転を寄せるフレーム数")
            NS_REFLECT_FIELD(m_missWobbleDegrees, "外れの軸のぶれの角度")
            NS_REFLECT_FIELD(m_missWobbleTurnsPerSecond, "外れの軸のぶれの速さ")
            NS_REFLECT_GROUP("着地の潰れ")
            NS_REFLECT_FIELD(m_landingSquash, "着地の潰れ")
            NS_REFLECT_FIELD(m_landingSquashRecoverSteps, "着地の潰れを戻すフレーム数")
            NS_REFLECT_GROUP("構えの縮み")
            NS_REFLECT_FIELD(m_chargeSquashScale, "構えの縮み")
            NS_REFLECT_FIELD(m_pressSquashScale, "押しの構えの縮み")
            NS_REFLECT_END()

        private:
            void AdvanceSpin(const PlayerMotion& motion,
                             const ChargeStatus& charge,
                             const BodySlamStatus& slam,
                             const ReboundStatus& rebound,
                             const SkidStatus& skid,
                             const NS::Vector3& aimDirection,
                             const GL::Level::ImpactAnswer& impact) noexcept;
            // skidScale はこすって止まる間の身体の速さの倍率。こすっていない間は 1
            void AdvanceMissTumble(const MissTumble& tumble,
                                   const ReboundStatus& rebound,
                                   float skidScale,
                                   const GL::Level::ImpactAnswer& impact) noexcept;
            // 反動の着地で縦の潰れを始めるか、潰れを 1 フレーム戻す。書くのは WriteDrawScale
            void AdvanceLandingSquash(const PlayerMotion& motion, const NS::Body* body) noexcept;
            // 当たりの形か構えの縮みに着地の潰れを掛け、同居する Model の描く時だけの倍率へ書く
            void WriteDrawScale(const ChargeStatus& charge, const GL::Level::ImpactAnswer& impact) noexcept;

            NS::AssetRef<NS::Gfx::D3DMesh> m_standingMesh{"capsule"};
            NS::AssetRef<NS::Gfx::D3DMesh> m_ballMesh{"sphere"};
            NS::Vector3 m_standingScale{1.3f, 1.15f, 1.3f};
            NS::Vector3 m_ballScale{1.3f, 1.3f, 1.3f};
            bool m_curled = false;

            // 回る速さは 3 つとも 1 フレーム 180 度未満 (1/60 秒のフレームで 10800 度/秒未満) で使う。超えると描く時の
            // 補間が短い側を通り、逆回りに見える

            // 立ち姿の間の軸。丸まった直後に狙いが決まらなければこの軸で回る

            NS::Quaternion m_spin = NS::Quaternion::Identity;
            NS::Vector3 m_spinAxis = NS::Vector3::UnitX; // 直前のフレームに回した軸
            float m_spinSpeed = 0.0f;                    // 直前のフレームに回した速さ。度/秒
            float m_spinDegreesThisFrame = 0.0f;         // 直前の Update で回った角度。度
            NS::uint32 m_tumbleReboundCount = 0;         // 外れの回り方を始めた反動の回数。変わったら新しい反動
            NS::Vector3 m_tumbleStartSpin{};             // 外れの反動の始まりの回転。軸 × 度/秒
            int m_tumbleSteps = 0;                       // 外れの反動を始めてからのフレーム数
            float m_tumbleWobblePhase = 0.0f;            // 軸のぶれの始まりの向き。ラジアン
            int m_landingSquashRemaining = 0;            // 着地の潰れを戻し切るまでの残りフレーム数。0 は潰れていない
            float m_landingSquashVertical = 1.0f;        // 着地の潰れの今の縦の倍率。潰れていない間は 1
            bool m_drawScaleRejected = false;            // 直前に組んだ倍率を Model が断ったか。知らせを 1 回に絞る

            float m_emptyChargeSpinSpeed = 360.0f; // 溜め 0 の玉が回る速さ。度/秒
            float m_fullChargeSpinSpeed = 1440.0f; // 溜めきりの玉が回る速さ。度/秒
            // 縁で威力 1 の時のねじれの回転数 (回/秒)。威力と縁への近さを掛ける
            float m_missTwistTurnsPerSecond = 2.0f;
            // 溜めて外したほど大きく振り回される。突進の回転をこの割合だけ残してねじれに足す
            float m_missSpinCarryRatio = 0.4f;
            // 突進の回転からこのフレーム数で寄せる。急に変えると絵が飛ぶ
            int m_missSpinBlendSteps = 6;
            // ねじれの軸を傾ける角度 (度)
            float m_missWobbleDegrees = 10.0f;
            float m_missWobbleTurnsPerSecond = 1.5f;
            float m_landingSquash = 0.8f;        // 反動のまま着地したフレームの縦の倍率
            int m_landingSquashRecoverSteps = 6; // 着地の潰れを 1 へ戻すまでのフレーム数
            float m_chargeSquashScale = 0.95f;
            float m_pressSquashScale = 0.97f;
            NS::SceneActor* m_owner = nullptr; // 持ち主 (非所有)
        };
    } // namespace Player
} // namespace GL
