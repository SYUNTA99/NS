#pragma once

#include "Runtime/Math/Math.h"
#include "Runtime/Object/Component.h"

namespace NS::Object
{
    /// @brief シーンの平行光 (太陽) を 1 本供給する Component
    /// @details 配置物に載せ、Scene が有効な平行光を選んで描画のシーン段照明の出所にする
    /// 位置は無関係で向き / 色 / 環境光だけを持つ。複数置いた場合の多灯合成は非対応で、有効な 1 本のみが効く
    class DirectionalLightComponent : public Component
    {
    public:
        /// 平行光の向き。正規化前で良く、シェーダ側で正規化する
        [[nodiscard]] const NS::Math::Vector3& Direction() const noexcept { return m_direction; }
        /// 平行光の色。1.0 超の HDR 値も許容する
        [[nodiscard]] const NS::Math::Vector3& Color() const noexcept { return m_color; }
        /// 空側の環境光。上を向いた面へ回る
        [[nodiscard]] const NS::Math::Vector3& Ambient() const noexcept { return m_ambient; }
        /// 地面側の環境光。下を向いた面へ回る
        [[nodiscard]] const NS::Math::Vector3& Ground() const noexcept { return m_ground; }
        /// 画面へ出す前の露出。上げると暗部が持ち上がり、明部は S 字が受け止めるので飛ばない
        [[nodiscard]] float Exposure() const noexcept { return m_exposure; }

        NS_REFLECT_BEGIN(DirectionalLightComponent, Component)
        NS_REFLECT_FIELD(m_direction, "方向")
        NS_REFLECT_FIELD(m_color, "色")
        NS_REFLECT_FIELD(m_ambient, "環境光")
        NS_REFLECT_FIELD(m_ground, "地面環境光")
        NS_REFLECT_FIELD(m_exposure, "露出")
        NS_REFLECT_END()

    private:
        NS::Math::Vector3 m_direction{-0.3f, -1.0f, -0.2f}; // 平行光の向き
        NS::Math::Vector3 m_color{1.0f, 1.0f, 1.0f};        // 平行光の色
        NS::Math::Vector3 m_ambient{0.30f, 0.34f, 0.40f};   // 空側の環境光
        NS::Math::Vector3 m_ground{0.24f, 0.21f, 0.18f};    // 地面側の環境光
        float m_exposure = 1.35f;                           // 画面へ出す前の露出
    };
} // namespace NS::Object
