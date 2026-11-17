#pragma once

#include "NSlib/Core/Math.h"

#include "NSlib/Core/Types.h"

//! エディタのビューの矩形・画面から世界への光線・画面の上で大きさを保つ倍率を補助する数学ユーティリティ

namespace NS
{
    namespace Ed
    {
        //! ゲーム画面を映すパネルの矩形。スクリーン座標で、編集入力の変換と内外の判定に使う
        struct ViewRect
        {
            int x = 0;      // 左上のスクリーン X 座標
            int y = 0;      // 左上のスクリーン Y 座標
            int width = 0;  // 幅 (ピクセル)
            int height = 0; // 高さ (ピクセル)
        };

        //! スクリーン座標が矩形内かを返す。右端 (x+width) と下端 (y+height) は外側扱い
        [[nodiscard]] bool ViewRectContains(const ViewRect& rect, int screenX, int screenY) noexcept;

        [[nodiscard]] Size2D ViewRectSize(const ViewRect& rect) noexcept;

        //! スクリーン座標を矩形原点基準のローカル座標へ変換する
        //! @note 矩形外でも変換自体は返す。範囲判定は ViewRectContains が担う
        void ViewRectToLocal(const ViewRect& rect, int screenX, int screenY, int& outX, int& outY) noexcept;

        //! ウィンドウ内のマウス座標を、別窓にも対応するパネルのスクリーン座標へ変換する
        void WindowMouseToViewSpace(int mouseX, int mouseY, int& outX, int& outY) noexcept;

        //! スクリーンの2D座標からワールド空間へ向かう Ray を生成する
        [[nodiscard]] Ray ScreenToWorldRay(const Matrix& viewProjection,
                                               Size2D viewport,
                                               int mouseX,
                                               int mouseY) noexcept;

        //! 画面上の見かけの大きさを一定に近づける、世界の長さの倍率を返す
        //! 深度 10 までは 1、遠いほど深度に比例して伸ばす。clip.w が 0.001 以下なら 1
        [[nodiscard]] float ScreenConstantScale(const Vector3& point, const Matrix& viewProjection) noexcept;
    } // namespace Ed
} // namespace NS
