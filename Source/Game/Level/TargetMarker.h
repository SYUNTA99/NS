#pragma once

#include "Game/Level/ImpactResolver.h"
#include "Game/Level/SlamAim.h"
#include "Game/Player/PlayerVisualParams.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/OverlayRenderer.h"
#include "NSlib/CoreObject/Reflection/Curve.h"

#include <vector>

namespace GL::Player
{
        class ChargeStatus;
} // namespace GL::Player

namespace GL::Level
{
        //! 画面へ重ねる四角 1 枚。座標は描画先の画素で、左上が原点
        struct MarkerRect
        {
            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
        };

        //! ロックオンの枠の出方を決めるフレーム数。フレームは固定ステップで数える
        struct LockOnFrames
        {
            int sinceCapture = 0; //!< 狙う相手なしからありへ移ったフレームを 0 として数えたフレーム数
            int sinceLost = -1; //!< 狙う相手ありからなしへ移ったフレームを 0 として数えたフレーム数。外れていなければ負
        };

        //! ロックオンの枠。明るい線の四角と、その下に先に描く暗い縁の四角
        struct LockOnFrameShape
        {
            //! 明るい線。左上・右上・左下・右下の隅の順に、隅ごとに横の 1 本と縦の 1 本
            std::vector<MarkerRect> corners;
            //! 暗い縁。同じ並びで、各四角を縁の幅ぶん広げる
            std::vector<MarkerRect> outline;
            NS::Color color{};        //!< 明るい線の色と不透明度
            NS::Color outlineColor{}; //!< 暗い縁の色と不透明度
        };

        //! 外接箱の中心と最大半径を画面へ投影し、捕捉・喪失の経過に応じた枠を組む
        //! 画素寸法は高さ 720 基準。viewProjection は行ベクトルに右から掛ける行列
        //! カメラの後ろや喪失演出の終了後は空の枠。不正な入力・調整値なら false で outFrame を変更しない
        [[nodiscard]] bool BuildLockOnFrame(const NS::Matrix& viewProjection,
                                            NS::Size2D targetSize,
                                            const NS::AABB& bounds,
                                            LockOnFrames frames,
                                            const TargetMarkerDesc& desc,
                                            LockOnFrameShape& outFrame);

        //! @brief 溜め中の相手へロックオン枠を描く部品
        //! @details 相手を見失うと直前の枠を縮めて残す。溜めを終えたら消す
        class TargetMarker : public NS::OverlayRenderer
        {
        public:
            //! 溜めている間、確定した狙う相手へ枠を控える。溜めを終えたら控えと経過フレーム数を戻す
            void Update(const GL::Player::ChargeStatus& charge);

            void OnRenderOverlay(const NS::Gfx::RenderContext& context) override;

            //! このフレームに示す相手。示さないフレームと、外れた後に枠だけを出すフレームは nullptr
            [[nodiscard]] NS::SceneActor* ShownTarget() const noexcept;

            //! 捕捉中か喪失演出中の枠を組む。targetSize は画素寸法
            //! 枠を組めた場合 true、それ以外の場合は false。失敗時は outFrame を変更しない
            [[nodiscard]] bool BuildShownShape(const NS::Matrix& viewProjection,
                                               NS::Size2D targetSize,
                                               LockOnFrameShape& outFrame) const;

            NS_REFLECT_BEGIN(TargetMarker, NS::OverlayRenderer)
            NS_GROUP("ロックオンの枠")
            NS_FIELD(m_desc.color, "ロックオンの枠の色")
            NS_FIELD(m_desc.lineThickness, "ロックオンの枠の太さ")
            NS_FIELD(m_desc.armRatio, "ロックオンの枠の腕の割合")
            NS_GROUP("枠")
            NS_FIELD(m_desc.frameGap, "枠と輪郭の間")
            NS_FIELD(m_desc.frameMinSide, "枠の一辺の下限")
            NS_FIELD(m_desc.outlineColor, "枠の縁の色")
            NS_FIELD(m_desc.outlineWidth, "枠の縁の幅")
            NS_GROUP("枠が出る時")
            NS_FIELD(m_desc.appearScale, "枠が出る時の倍率")
            NS_FIELD(m_desc.appearMaxSide, "枠が出る時の一辺の上限")
            NS_FIELD(m_desc.appearFrames, "枠が縮むフレーム数")
            NS_FIELD(m_desc.appearCurve, "枠が縮む進みの曲線")
            NS_FIELD(m_desc.appearColor, "枠が出る時の色")
            NS_GROUP("外れた時")
            NS_FIELD(m_desc.lostScale, "外れた時の枠の倍率")
            NS_FIELD(m_desc.lostFrames, "外れた時の枠のフレーム数")
            NS_FIELD(m_desc.referenceHeight, "画素寸法の基準の高さ")
            NS_REFLECT_END()

        private:
            TargetMarkerDesc m_desc{};
            SlamLineTarget m_shown{}; // 示す相手の予測。m_hasShown が偽の間は読まない
            bool m_hasShown = false;
            int m_framesSinceCapture = 0; // 外れた後は外れたフレームの値で止める
            NS::AABB m_lostBounds{};      // 外れた相手の外接箱。m_framesSinceLost が負の間は読まない
            int m_framesSinceLost = -1;
        };
} // namespace GL::Level
