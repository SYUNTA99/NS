#pragma once

#include "NSlib/Core/Interpolated.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Object.h"

#include <cstddef>
#include <vector>

namespace NS
{
    //! 世界の座標の線 1 本
    struct WorldLine
    {
        Vector3 start{};
        Vector3 end{};
        float thickness = 0.0f; //!< 描画先の高さに対する太さの割合
        Color color{};
    };

    //! 画素へ投げた線 1 本。座標は描画先の左上を原点にしたピクセル
    struct PixelLine
    {
        Vector2 start{};
        Vector2 end{};
        float thickness = 0.0f; //!< ピクセル
        Color color{};
    };

    //! 線を画素へ投げる。端の点がカメラの後ろにある線と、画面での長さが太さより短い線 (カメラへまっすぐ向く物) は投げない
    //! @param[in] viewProjection 行ベクトルに右から掛けるビュー × 射影
    //! @param[out] outLine 投げた線。失敗した場合は書き換えない
    //! @return 投げた場合は true、投げない線と描画先の大きさが 0 以下の場合は false
    [[nodiscard]] bool TryProjectWorldLine(const WorldLine& line,
                                           const Matrix& viewProjection,
                                           Size2D targetSize,
                                           PixelLine& outLine) noexcept;

    //! 世界の座標の線を、固定ステップの前と今の 2 つで持つ部品。書くのは持ち主のアクター、描くのは SceneRenderer
    //! 基底の SceneActor が OnInit の直後に入れ、出入りで切り替え、OnEndPlay で外し、固定ステップの頭で Snapshot を呼ぶ
    class WorldLines : public Object
    {
    public:
        NS_REFLECT_NONE(WorldLines, Object)

        //! この歩の線を全部書き直す。前の歩の線から補間して描かれる
        void SetLines(const std::vector<WorldLine>& lines) { m_lines.Set(lines); }
        //! 前の歩の線を今の線にそろえる
        void Snapshot() { m_lines.Snapshot(); }

        //! 今の線の数
        [[nodiscard]] std::size_t Count() const noexcept { return m_lines.Current().size(); }
        //! index 番目の今の線の端の点を、前の歩の同じ番目の線と割合 alpha (0 で前、1 で今) で混ぜた線
        //! 前と今の線の数が違う場合は、別の線どうしを混ぜずに今の線を返す。index は Count より小さいこと
        [[nodiscard]] WorldLine LineAt(std::size_t index, float alpha) const noexcept;

    private:
        Interpolated<std::vector<WorldLine>> m_lines;
    };
} // namespace NS
