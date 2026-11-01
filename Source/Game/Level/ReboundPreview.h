#pragma once

#include "Game/Player/PlayerVisualParams.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/OverlayRenderer.h"

#include <vector>

namespace GL
{
    namespace Level
    {
        //! 反動の矢印を描く元の、当たる点と向き。向きは全部が単位の長さ
        struct ReboundPreviewArrows
        {
            NS::Vector3 origin{};              //!< 当たると予測した点。世界座標
            NS::Vector3 assigned{};            //!< 当て方で決まる返る向き
            std::vector<NS::Vector3> choices;  //!< スティックで曲げられる先 (CanShiftReboundTo を満たす向き)
        };

        //! 画面に描く線 1 本。座標は描画先のピクセルで、左上が原点
        struct ReboundArrowLine
        {
            float startX = 0.0f;
            float startY = 0.0f;
            float endX = 0.0f;
            float endY = 0.0f;
            float thickness = 0.0f; //!< ピクセル
            NS::Color color{};
        };

        //! 矢印ごとに、軸と矢じりの 2 本の線を組む。曲げられる先を先に並べ、割り当てた向きを最後に並べて上に重ねる
        //! 当たる点か矢じりの先がカメラの後ろにある矢印と、画面でほぼ点になる矢印 (カメラへまっすぐ向く物) は組まない
        //! @param[in] viewProjection 行ベクトルに右から掛けるビュー × 射影
        //! @param[out] outLines 組んだ線。前の中身は捨てる。失敗した場合は書き換えない
        //! @return 調整値か描画先の大きさが不正な場合は false、それ以外の場合は true
        [[nodiscard]] bool BuildReboundArrowLines(const ReboundPreviewArrows& arrows,
                                                  const NS::Matrix& viewProjection,
                                                  NS::Size2D targetSize,
                                                  const ReboundPreviewDesc& desc,
                                                  std::vector<ReboundArrowLine>& outLines);

        //! エアダッシュで当たる前に、反動で飛ぶ向きと曲げられる先を矢印で宙に描く部品
        //! 自機が毎フレーム Show か Hide を呼ぶ
        class ReboundPreview : public NS::OverlayRenderer
        {
        public:
            //! このフレームに描く矢印を控える
            void Show(const ReboundPreviewArrows& arrows);

            //! 控えを捨て、描かない
            void Hide() noexcept;

            //! 控えた矢印。控えが無い場合は nullptr
            [[nodiscard]] const ReboundPreviewArrows* ShownArrows() const noexcept;

            void OnRenderOverlay(const NS::Gfx::RenderContext& context) override;

            NS_REFLECT_BEGIN(ReboundPreview, NS::OverlayRenderer)
            NS_GROUP("形")
            NS_FIELD(m_desc.assignedLength, "当て方の向きの矢印の長さ")
            NS_FIELD(m_desc.choiceLength, "曲げられる先の矢印の長さ")
            NS_FIELD(m_desc.assignedThickness, "当て方の向きの矢印の太さ")
            NS_FIELD(m_desc.choiceThickness, "曲げられる先の矢印の太さ")
            NS_FIELD(m_desc.headLengthRatio, "矢じりの長さの割合")
            NS_FIELD(m_desc.headAngleDegrees, "矢じりの開く角度")
            NS_FIELD(m_desc.referenceHeight, "ピクセルの寸法の基準の高さ")
            NS_GROUP("色")
            NS_FIELD(m_desc.assignedColor, "当て方の向きの矢印の色")
            NS_FIELD(m_desc.choiceColor, "曲げられる先の矢印の色")
            NS_REFLECT_END()

        private:
            ReboundPreviewDesc m_desc{};
            ReboundPreviewArrows m_shown{}; // m_hasShown が偽の間は読まない
            bool m_hasShown = false;
            std::vector<ReboundArrowLine> m_lineScratch; // 線を毎フレーム組み直す置き場。確保を使い回す
        };
    } // namespace Level
} // namespace GL
