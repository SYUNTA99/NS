#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/AssetField.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"

#include "NSlib/Core/Types.h"
#include <string>

namespace GL
{
    namespace Player
    {
        struct PlayerMotion;

        //! 自機の立ち姿・玉と、描画時の回転・変形を管理する部品
        //! 姿のメッシュは素材の姿のポインタの欄 (NS::Mesh*)
        //! に持ち、姿ごとの倍率で大きさを合わせる。当たりの寸法には付いて行かない
        // TODO: 部分的な JSON 適用では Model の undo を再反映できない。ResetFromJson で直る
        class PlayerAppearance : public NS::Object
        {
        public:
            //! 姿のメッシュの欄は、作る時に既定の名前の素材を AssetManager::Get() から引く
            PlayerAppearance();

            //! 玉のメッシュを renderer (持ち主の Model) へ差し、玉の倍率を局所の倍率へ書く
            //! 丸まっているかは持たない。持ち主が当たりの形を変えた時と OnAssetsLoaded で、形に合う方を呼ぶ
            void Curl(NS::Model* renderer) noexcept;
            //! 立ち姿のメッシュを renderer へ差し、立ち姿の倍率を局所の倍率へ書く
            //! 呼ぶ時は Curl と同じ
            void Uncurl(NS::Model* renderer) noexcept;

            //! 丸まりに合わせて玉を回し、回転を renderer へ書く。持ち主の見た目の段が呼ぶ
            //! @param[in] dt 持ち主の 1 フレームの秒。玉を回す角度に掛ける
            void Update(NS::Model* renderer, float dt, const PlayerMotion& motion);

            //! renderer (持ち主の Model) の描く倍率を補間なしで (1, 1, 1) へ戻す
            //! やり直しとプレイの終わりで持ち主が呼ぶ
            void ResetDrawScale(NS::Model* renderer) noexcept;

            //! 直前の Update で玉が回った角度を度で返す。立ち姿と止めの間は 0
            [[nodiscard]] float SpinDegreesThisFrame() const noexcept { return m_spinDegreesThisFrame; }
            //! 玉を回している軸を返す
            //! 根の空間の単位ベクトル。正の角度で、玉の上面が 軸 × 上 の向きへ倒れる前転になる
            //! @return 直前の Update で回した軸。止めの間は止まる前の軸、立ち姿では (1, 0, 0)
            [[nodiscard]] NS::Vector3 SpinAxis() const noexcept { return m_spinAxis; }

            //! 水平の速さが最大の速さのこの比以上なら、歩きでなく走りの動作を流す
            [[nodiscard]] float RunBlendRatio() const noexcept { return m_runBlendRatio; }
            //! 歩きと走りの動作の再生速度の下限
            [[nodiscard]] float MinPlaybackSpeed() const noexcept { return m_minPlaybackSpeed; }

            NS_REFLECT_BEGIN(PlayerAppearance, NS::Object)
            NS_GROUP("姿")
            NS_FIELD(m_standingMesh, "立ち姿のメッシュ")
            NS_FIELD(m_ballMesh, "玉のメッシュ")
            NS_FIELD(m_standingScale, "立ち姿の倍率")
            NS_FIELD(m_ballScale, "玉の倍率")
            NS_GROUP("エアダッシュの転がり")
            NS_FIELD(m_dashRollDegreesPerMeter, "エアダッシュの 1 m で回る角度")
            NS_GROUP("歩きと走り")
            NS_FIELD(m_runBlendRatio, "走りへ移る速さの比")
            NS_FIELD(m_minPlaybackSpeed, "再生速度の下限")
            NS_REFLECT_END()

        private:
            // 姿のメッシュを Model へ差し、姿の倍率を Model の局所の倍率へ書く
            // 読めていない姿は、Model の今のメッシュを残す
            void ShowLook(NS::Model* renderer, NS::Mesh* mesh, const NS::Vector3& scale) noexcept;

            // 素材は AssetManager が持つ。欄は nullptr にならない
            NS::Mesh* m_standingMesh = nullptr;
            NS::Mesh* m_ballMesh = nullptr;
            NS::Vector3 m_standingScale{1.3f, 1.15f, 1.3f};
            NS::Vector3 m_ballScale{1.3f, 1.3f, 1.3f};

            // 玉の回る速さは 1 フレーム 180 度未満 (1/60 秒のフレームで 10800 度/秒未満) で使う。超えると描く時の
            // 補間が短い側を通り、逆回りに見える

            NS::Quaternion m_spin = NS::Quaternion::Identity;
            NS::Vector3 m_spinAxis = NS::Vector3::UnitX; // 直前のフレームに回した軸
            float m_spinSpeed = 0.0f;                    // 直前のフレームに回した速さ。度/秒
            float m_spinDegreesThisFrame = 0.0f;         // 直前の Update で回った角度。度

            // エアダッシュと地形の反動の間、1 m 進むごとに玉が回る角度 (度/m)。回る速さは今の速さに掛ける
            float m_dashRollDegreesPerMeter = 88.0f;

            // 歩きと走りの動作は、再生速度を水平の速さ ÷
            // 最大の速さにする。止まりかけで足が止まって見えないよう下限で止める
            float m_runBlendRatio = 0.4f;
            float m_minPlaybackSpeed = 0.5f;
        };
    } // namespace Player
} // namespace GL
