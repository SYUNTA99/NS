#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Object.h"

namespace NS
{
    //! シーンへ向き・色・環境光を渡す平行光。複数置いても有効な一つだけを使う
    class DirectionalLight : public Object
    {
    public:
        //! 平行光の向き。持ち主の根の世界行列 root で +Z を回した、長さ 1 の向き
        //! 向きの欄は持たない。エディタのギズモでアクターを回すと光も回る
        [[nodiscard]] Vector3 Direction(const Matrix& root) const noexcept;
        //! 平行光の色。1.0 超の HDR 値も許容する
        [[nodiscard]] const NS::Color& Color() const noexcept { return m_color; }
        //! 空側の環境光。上を向いた面へ回る
        [[nodiscard]] const NS::Color& Ambient() const noexcept { return m_ambient; }
        //! 地面側の環境光。下を向いた面へ回る
        [[nodiscard]] const NS::Color& Ground() const noexcept { return m_ground; }
        //! 画面へ出す前の露出。上げると暗部が持ち上がり、明部は S 字が受け止めるので飛ばない
        [[nodiscard]] float Exposure() const noexcept { return m_exposure; }

        NS_REFLECT_BEGIN(DirectionalLight, Object)
        NS_FIELD(m_color, "色")
        NS_FIELD(m_ambient, "環境光")
        NS_FIELD(m_ground, "地面環境光")
        NS_FIELD(m_exposure, "露出")
        NS_REFLECT_END()

    private:
        // 光の 3 つの色は透明度を使わない。描く所へは rgb だけを渡す
        NS::Color m_color{1.0f, 1.0f, 1.0f};
        NS::Color m_ambient{0.30f, 0.34f, 0.40f};
        NS::Color m_ground{0.24f, 0.21f, 0.18f};
        float m_exposure = 1.35f;
    };
} // namespace NS
