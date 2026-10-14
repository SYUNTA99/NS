#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Object/Reflection/Reflection.h"
#include "NSlib/Object/SubObject.h"

#include "NSlib/Core/Types.h"
#include <string>

class Player;

namespace NS::Gfx
{
    class Mesh;
}

namespace NS::Obj
{
    class Body;
}

namespace GL::Level
{
    class ImpactResolver;
} // namespace GL::Level

namespace GL::Player
{
    struct MissTumble;

    //! @brief 自機の立ち姿・玉と、描画時の回転・変形を管理する部品
    //! @details 空の参照は Collider の寸法で仮の形を作る。Model の後に資産を解決する
    // TODO: 部分的な JSON 適用では Model の undo や Collider の寸法変更を再反映できない。ResetFromJson で直る
    class PlayerAppearance : public NS::Obj::SubObject
    {
    public:
        PlayerAppearance() noexcept;

        //! @brief 玉の見た目へ持ち替える。既に玉なら何もしない
        //! @details 持ち主の Player があれば、次の OnUpdate でその丸まりに合わせ直す
        void Curl() noexcept;
        //! @brief 立ち姿へ戻す。既に立ち姿なら何もしない
        //! @details 持ち主の Player があれば、次の OnUpdate でその丸まりに合わせ直す
        void Uncurl() noexcept;
        [[nodiscard]] bool IsCurled() const noexcept { return m_curled; }

        //! 2 つの見た目を引き当て、今の姿の mesh を同居する Model へ差す
        void ResolveAssets(NS::Obj::AssetManager& assets) override;

        //! 同居する身体の部品と ImpactResolver を控える。身体の部品が無ければ以後は丸まりを写さない
        void OnStart() override;
        void OnUpdate() override;
        //! 描く形の倍率を 1 へ戻す。プレイを終えた後に潰れた形が残らない
        void OnEndPlay() override;

        //! @brief 描く形の倍率を補間なしで (1, 1, 1) へ戻し、着地の潰れを捨てる
        //! @details やり直しとプレイの終わりで呼ぶ。次の OnUpdate からはその時の構えと当たりの形で組み直す
        void ResetDrawScale() noexcept;

        //! 直前の OnUpdate で玉が回った角度を度で返す。立ち姿と止めの間は 0
        [[nodiscard]] float SpinDegreesThisFrame() const noexcept { return m_spinDegreesThisFrame; }
        //! @brief 玉を回している軸を返す
        //! @details 根の空間の単位ベクトル。正の角度で、玉の上面が 軸 × 上 の向きへ倒れる前転になる。
        //! 外れの反動の間は水平とは限らない
        //! @return 直前の OnUpdate で回した軸。止めの間は止まる前の軸、立ち姿では (1, 0, 0)
        [[nodiscard]] NS::Vector3 SpinAxis() const noexcept { return m_spinAxis; }
        [[nodiscard]] float ChargeSquashScale() const noexcept { return m_chargeSquashScale; }
        [[nodiscard]] float PressSquashScale() const noexcept { return m_pressSquashScale; }

        NS_REFLECT_BEGIN(PlayerAppearance, NS::Obj::SubObject)
        NS_REFLECT_GROUP("姿")
        NS_REFLECT_FIELD(m_standingMeshRef, "立ち姿のメッシュ")
        NS_REFLECT_FIELD(m_ballMeshRef, "玉のメッシュ")
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
        void ShowCurrentLook() noexcept;
        void AdvanceSpin() noexcept;
        void AdvanceMissTumble(const MissTumble& tumble) noexcept;
        // 反動の着地で縦の潰れを始めるか、潰れを 1 フレーム戻す。書くのは WriteDrawScale
        void AdvanceLandingSquash() noexcept;
        // 当たりの形か構えの縮みに着地の潰れを掛け、同居する Model の描く時だけの倍率へ書く
        void WriteDrawScale() noexcept;

        // 保存・編集される参照文字列。空は仮の形。ResolveAssets が実体を当てる
        std::string m_standingMeshRef{};
        std::string m_ballMeshRef{};
        NS::Gfx::Mesh* m_standingMesh = nullptr; // AssetManager 所有
        NS::Gfx::Mesh* m_ballMesh = nullptr;     // AssetManager 所有
        bool m_curled = false;
        const NS::Obj::Body* m_body = nullptr; // 接地の問い先。非所有
        const ::Player* m_actor = nullptr;     // 突進の速度と狙いの向きと溜めと構えの縮みの問い先。非所有
        const GL::Level::ImpactResolver* m_resolver = nullptr; // 当たりの潰れと伸びの問い先。非所有

        // 回る速さは 3 つとも 1 フレーム 180 度未満 (1/60 秒のフレームで 10800 度/秒未満) で使う。超えると描く時の
        // 補間が短い側を通り、逆回りに見える

        // 立ち姿の間の軸。丸まった直後に狙いが決まらなければこの軸で回る

        NS::Quaternion m_spin = NS::Quaternion::Identity;
        NS::Vector3 m_spinAxis = NS::Vector3::UnitX; // 直前のフレームに回した軸
        float m_spinSpeed = 0.0f;                    // 直前のフレームに回した速さ。度/秒
        float m_spinDegreesThisFrame = 0.0f;         // 直前の OnUpdate で回った角度。度
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
    };
} // namespace GL::Player
