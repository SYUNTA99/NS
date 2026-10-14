#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Object/SubObject.h"

namespace NS::Obj
{
    //! シーンへ向き・色・環境光を渡す平行光。複数置いても有効な一つだけを使う
    class DirectionalLight : public SubObject
    {
    public:
        //! 平行光の向き。正規化前で良く、シェーダ側で正規化する
        [[nodiscard]] const NS::Vector3& Direction() const noexcept { return m_direction; }
        //! 平行光の色。1.0 超の HDR 値も許容する
        [[nodiscard]] const NS::Vector3& Color() const noexcept { return m_color; }
        //! 空側の環境光。上を向いた面へ回る
        [[nodiscard]] const NS::Vector3& Ambient() const noexcept { return m_ambient; }
        //! 地面側の環境光。下を向いた面へ回る
        [[nodiscard]] const NS::Vector3& Ground() const noexcept { return m_ground; }
        //! 画面へ出す前の露出。上げると暗部が持ち上がり、明部は S 字が受け止めるので飛ばない
        [[nodiscard]] float Exposure() const noexcept { return m_exposure; }

        //! 所属 scene の平行光の登録簿へ自分を入れる
        void OnAppear() override { DirectionalLight::OnStart(); }
        void OnDisappear() noexcept override { DirectionalLight::OnEndPlay(); }
        void OnStart() override;
        //! 所属 scene の平行光の登録簿から自分を外す
        void OnEndPlay() override;

        NS_REFLECT_BEGIN(DirectionalLight, SubObject)
        NS_REFLECT_FIELD(m_direction, "方向")
        NS_REFLECT_FIELD(m_color, "色")
        NS_REFLECT_FIELD(m_ambient, "環境光")
        NS_REFLECT_FIELD(m_ground, "地面環境光")
        NS_REFLECT_FIELD(m_exposure, "露出")
        NS_REFLECT_END()

    private:
        NS::Vector3 m_direction{-0.3f, -1.0f, -0.2f};
        NS::Vector3 m_color{1.0f, 1.0f, 1.0f};
        NS::Vector3 m_ambient{0.30f, 0.34f, 0.40f};
        NS::Vector3 m_ground{0.24f, 0.21f, 0.18f};
        float m_exposure = 1.35f;
    };
} // namespace NS::Obj
