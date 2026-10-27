#pragma once

#include "NSlib/Core/Math.h"

#include "NSlib/Core/Types.h"

//! @brief エディタでのグリッド計算やレイキャスト判定を補助する数学ユーティリティ

namespace NS::Editor
{
        //! グリッドの基本サイズ
        inline constexpr float k_GridSize = 1.0f;

        //! @brief ゲーム画面を映すパネルの矩形。スクリーン座標で、編集入力の変換と内外の判定に使う
        struct ViewRect
        {
            int x = 0;      //!< 左上のスクリーン X 座標
            int y = 0;      //!< 左上のスクリーン Y 座標
            int width = 0;  //!< 幅 (ピクセル)
            int height = 0; //!< 高さ (ピクセル)
        };

        //! スクリーン座標が矩形内かを返す。右端 (x+width) と下端 (y+height) は外側扱い
        [[nodiscard]] bool ViewRectContains(const ViewRect& rect, int screenX, int screenY) noexcept;

        [[nodiscard]] Size2D ViewRectSize(const ViewRect& rect) noexcept;

        //! @brief スクリーン座標を矩形原点基準のローカル座標へ変換する
        //! @note 矩形外でも変換自体は返す。範囲判定は ViewRectContains が担う
        void ViewRectToLocal(const ViewRect& rect, int screenX, int screenY, int& outX, int& outY) noexcept;

        //! ウィンドウ内のマウス座標を、別窓にも対応するパネルのスクリーン座標へ変換する
        void WindowMouseToViewSpace(int mouseX, int mouseY, int& outX, int& outY) noexcept;

        //! @brief スクリーンの2D座標からワールド空間へ向かう Ray を生成する
        [[nodiscard]] Ray ScreenToWorldRay(const Matrix& viewProjection,
                                               Size2D viewport,
                                               int mouseX,
                                               int mouseY) noexcept;

        //! ワールド座標を最も近いグリッドセルの中心座標に丸める
        [[nodiscard]] Vector3 SnapWorldPointToGrid(Vector3 worldPoint, float gridSize = k_GridSize) noexcept;

        //! @brief レイと水平な地面との交点を算出し、グリッドにスナップした座標を取得する
        //! @param[out] outCellCenter スナップ後のセル中心。交差した時だけ書く
        //! @return 交差した場合 true、それ以外の場合は false
        [[nodiscard]] bool TryGroundPlaneFallback(const Ray& ray,
                                                  Vector3& outCellCenter,
                                                  float gridSize = k_GridSize) noexcept;

        //! 0〜3の段階的な回転値を、Y軸周りの90度刻みのクォータニオンに変換する
        [[nodiscard]] Quaternion RotationToQuaternion(uint8 rotation) noexcept;

        //! @brief 画面上の見かけの大きさを一定に近づける、世界の長さの倍率を返す
        //! @details 深度 10 までは 1、遠いほど深度に比例して伸ばす。clip.w が 0.001 以下なら 1
        [[nodiscard]] float ScreenConstantScale(const Vector3& point, const Matrix& viewProjection) noexcept;
} // namespace NS::Editor
