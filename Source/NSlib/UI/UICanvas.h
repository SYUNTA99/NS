#pragma once

#include "NSlib/UI/Widget.h"

namespace NS
{
    namespace UI
    {
        //! @brief 基準解像度と実ピクセルの対応。画面全体で 1 つ
        //! @details 座標は高さ 1080 の基準解像度で書き、描画先の高さ比で一様に拡縮する
        //! 横幅は比率に応じて伸び縮みし、端に貼る物は anchor で追従させる
        struct UICanvas
        {
            static constexpr float k_ReferenceHeight = 1080.0f; //!< 基準解像度の高さ

            float scale = 1.0f; //!< 基準解像度から実ピクセルへの拡縮率
            WidgetRect rect{};  //!< 基準解像度単位の全画面の矩形
        };

        //! @brief 描画先の大きさから拡縮と全画面の矩形を作る
        //! @details 幅か高さが 0 以下なら拡縮 1 と空の矩形を返す
        [[nodiscard]] UICanvas MakeUICanvas(float viewportWidth, float viewportHeight) noexcept;
    } // namespace UI
} // namespace NS
