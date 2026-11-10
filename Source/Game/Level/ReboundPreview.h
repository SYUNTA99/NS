#pragma once

#include "Game/Player/PlayerVisualParams.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/WorldLines.h"
#include "NSlib/CoreObject/Object.h"

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

        //! 矢印ごとに、軸と矢じりの 2 本を世界の座標の線で組む。曲げられる先を先に並べ、割り当てた向きを最後に並べて上に重ねる
        //! 矢じりは、矢印の向きと世界の上向きに垂直な向きへ開く。矢印が上か下を向く時は世界の X 軸へ開く
        //! 太さは描画先の高さに対する割合にする (調整値の太さ ÷ 基準の高さ)
        //! @param[out] outLines 組んだ線。前の中身は捨てる。失敗した場合は書き換えない
        //! @return 調整値が不正な場合は false、それ以外の場合は true
        [[nodiscard]] bool BuildReboundArrowLines(const ReboundPreviewArrows& arrows,
                                                  const ReboundPreviewDesc& desc,
                                                  std::vector<NS::WorldLine>& outLines);

        //! エアダッシュで当たる前に、反動で飛ぶ向きと曲げられる先を矢印にして、持ち主の線の部品へ書く部品
        //! 自機が毎フレーム Show か Hide を呼び、続けて WriteLines を呼ぶ。描くのは描く仕組み
        class ReboundPreview : public NS::Object
        {
        public:
            //! このフレームに描く矢印を控える
            void Show(const ReboundPreviewArrows& arrows);

            //! 控えを捨て、描かない
            void Hide() noexcept;

            //! 控えた矢印。控えが無い場合は nullptr
            [[nodiscard]] const ReboundPreviewArrows* ShownArrows() const noexcept;

            //! 控えた矢印を世界の座標の線に組んで lines に書く。控えが無い時と調整値が不正な時は線を空にする
            //! @param[in] lines 書く先。持ち主のアクターの線の部品
            void WriteLines(NS::WorldLines* lines);

            NS_REFLECT_BEGIN(ReboundPreview, NS::Object)
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
            std::vector<NS::WorldLine> m_lineScratch; // 線を毎フレーム組み直す置き場。確保を使い回す
        };
    } // namespace Level
} // namespace GL
