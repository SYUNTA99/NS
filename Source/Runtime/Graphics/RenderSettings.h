#pragma once

#include "Runtime/Math/Math.h"

#include <optional>

namespace NS::Graphics
{
    //! @brief プロジェクト全体の標準となる描画設定
    //! @details 描画システムが保持し、各シーンは必要に応じてこの設定をオーバーライドして使用する
    struct RenderSettings
    {
        NS::Math::Color clearColor{0.10f, 0.10f, 0.15f, 1.0f}; //!< 画面のクリア色
        NS::Math::Vector3 lightDir{-0.3f, -1.0f, -0.2f};       //!< 平行光源の方向
        NS::Math::Vector3 lightColor{1.0f, 1.0f, 1.0f};        //!< 平行光源の色と明るさ
        //! 空側の環境光。 上を向いた面へ回る
        NS::Math::Vector3 ambientColor{0.30f, 0.34f, 0.40f};
        //! 地面側の環境光。 下を向いた面へ回る。 空側と分けると影の中でも面の向きが読める
        NS::Math::Vector3 groundColor{0.24f, 0.21f, 0.18f};
        //! 画面へ出す前の露出。 S 字で潰す前に掛けるので、 上げても明部は飛ばず暗部だけ持ち上がる
        float exposure = 1.35f;
    };

    //! @brief 描画設定のうち、特定の項目だけを上書きするための構造体
    //! @details 値が未設定の項目は、標準の設定値がそのまま引き継がれる
    struct RenderSettingsOverride
    {
        std::optional<NS::Math::Color> clearColor;
        std::optional<NS::Math::Vector3> lightDir;
        std::optional<NS::Math::Vector3> lightColor;
        std::optional<NS::Math::Vector3> ambientColor;
        std::optional<NS::Math::Vector3> groundColor;
        std::optional<float> exposure;
    };

    //! @brief 描画設定のうち、特定の項目だけを上書きするための構造体
    //! @details 値が未設定の項目は、標準の設定値がそのまま引き継がれる
    [[nodiscard]] RenderSettings Resolve(const RenderSettings& defaults, const RenderSettingsOverride& over) noexcept;
} // namespace NS::Graphics
