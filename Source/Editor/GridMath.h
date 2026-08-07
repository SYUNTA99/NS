#pragma once

#include "Runtime/Core/Math.h"

#include <cstdint>

//! @brief エディタでのグリッド計算やレイキャスト判定を補助する数学ユーティリティ

namespace NS::Editor
{
    //! グリッドの基本サイズ
    inline constexpr float k_GridSize = 1.0f;

    //! @brief ゲーム画面を映すパネルの矩形（スクリーン座標）。編集入力の座標変換と入りの判定に使う
    struct ViewRect
    {
        int x = 0;      //!< 左上のスクリーン X 座標
        int y = 0;      //!< 左上のスクリーン Y 座標
        int width = 0;  //!< 幅（ピクセル）
        int height = 0; //!< 高さ（ピクセル）
    };

    //! スクリーン座標が矩形内かを返す。右端 (x+width) と下端 (y+height) は外側扱い
    [[nodiscard]] bool ViewRectContains(const ViewRect& rect, int screenX, int screenY) noexcept;

    //! 矩形の幅と高さを返す
    [[nodiscard]] NS::Core::Size2D ViewRectSize(const ViewRect& rect) noexcept;

    //! @brief スクリーン座標を矩形原点基準のローカル座標へ変換する
    //! @note 矩形外でも変換自体は返す。範囲判定は ViewRectContains が担う
    void ViewRectToLocal(const ViewRect& rect, int screenX, int screenY, int& outX, int& outY) noexcept;

    //! @brief ウィンドウ内座標のマウスを、パネル矩形と同じ空間へ移す
    //! @details パネルを別窓へ出せる設定では矩形が OS の画面座標で来る。ウィンドウ内座標のまま
    //! 引き算するとタイトルバーと枠のぶんずれるので、ここで足して揃える
    void WindowMouseToViewSpace(int mouseX, int mouseY, int& outX, int& outY) noexcept;

    //! @brief スクリーンの2D座標からワールド空間へ向かう Ray を生成する。
    [[nodiscard]] NS::Core::Ray ScreenToWorldRay(const NS::Core::Matrix& viewProjection,
                                                 NS::Core::Size2D viewport,
                                                 int mouseX,
                                                 int mouseY) noexcept;

    //! ワールド座標を最も近いグリッドセルの中心座標に丸める
    [[nodiscard]] NS::Core::Vector3 SnapWorldPointToGrid(NS::Core::Vector3 worldPoint,
                                                         float gridSize = k_GridSize) noexcept;

    //! @brief 衝突点の座標と法線ベクトルから、隣接する配置先セルの中心座標を算出する
    [[nodiscard]] NS::Core::Vector3 SnapHitToPlacementCell(NS::Core::Vector3 hitPoint,
                                                           NS::Core::Vector3 hitNormal,
                                                           float gridSize = k_GridSize) noexcept;

    //! @brief レイと水平な地面との交点を算出し、グリッドにスナップした座標を取得する
    //! @return 交差しない場合はfalseを返す
    [[nodiscard]] bool TryGroundPlaneFallback(const NS::Core::Ray& ray,
                                              NS::Core::Vector3& outCellCenter,
                                              float gridSize = k_GridSize) noexcept;

    //! 0〜3の段階的な回転値を、Y軸周りの90度刻みのクォータニオンに変換する
    [[nodiscard]] NS::Core::Quaternion RotationToQuaternion(std::uint8_t rotation) noexcept;
} // namespace NS::Editor