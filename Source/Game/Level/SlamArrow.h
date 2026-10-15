#pragma once

#include "Game/Level/SlamAim.h"
#include "Game/Player/LaunchPitch.h"
#include "Game/Player/PlayerVisualParams.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/SubObjects/OverlayRenderer.h"
#include "NSlib/Graphics/DrawItem.h"
#include "NSlib/Graphics/FrameConstants.h"

#include <functional>
#include <string>
#include <vector>

namespace GL
{
    namespace Player
    {
        class ChargeStatus;
        class PlayerParams;
    } // namespace Player
} // namespace GL

namespace NS
{
    namespace Gfx
    {
        class Material;
    }
    namespace Gfx
    {
        class StaticMesh;
    }
} // namespace NS

namespace NS
{
    class Collider;
}

namespace GL
{
    namespace Level
    {
        //! 地面の矢印を組む時点の、溜めと狙いの様子
        struct SlamArrowState
        {
            AimLine line{};          //!< 狙いの線。矢印はこの線の向きへ放った玉の道筋に描く
            float ballRadius = 0.0f; //!< 自機の玉の半径。単位は m。帯の幅はこの 2 倍
            bool hasTarget = false;  //!< 線の上に狙う相手がいる場合 true
            //! 放った玉の縁が狙う相手に触れる所までの、線に沿った距離 (SlamLineTarget::launchContact)。単位は m。
            //! hasTarget が偽の間は読まない
            float targetContact = 0.0f;
            int framesSinceShown = 0; //!< 矢印を出したフレームを 0 として数えたフレーム数
            float charge01 = 0.0f;
            bool chargeFull = false;   //!< 溜めきりの場合 true
            float overcharge01 = 0.0f; //!< 溜めすぎの深さ 0..1。溜めきりの間だけ色に効く
        };

        //! 床に貼る板 1 枚ぶんの、線に沿った範囲と高さ
        struct SlamArrowPiece
        {
            float alongNear = 0.0f; //!< 自機に近い端の、線に沿った距離。単位は m
            float alongFar = 0.0f;  //!< 自機から遠い端の、線に沿った距離。単位は m
            float height = 0.0f;    //!< 近い端の高さ (世界の y)。床か玉の一番下の点の高さに浮かせる高さを足した値
            float rise = 0.0f;      //!< 遠い端の高さ − 近い端の高さ (m)。床に貼る板は 0
        };

        //! 地面の矢印の形と色。距離は狙いの線の始まり (自機の位置) から線に沿って測る
        struct SlamArrowShape
        {
            NS::Vector3 origin; //!< 狙いの線の始まり。世界座標
            NS::Vector3 direction;
            float bandWidth = 0.0f;           //!< 帯の幅 (玉の通る幅)。単位は m
            float start = 0.0f;               //!< 帯の始まり (玉の縁) の距離。単位は m
            float tip = 0.0f;                 //!< このフレームの矢じりの先の距離。伸びている途中はその長さ。単位は m
            float fullTip = 0.0f;             //!< 伸びきった矢じりの先の距離。単位は m
            float headDepth = 0.0f;           //!< 矢じりの奥行き。単位は m
            float colorFront = 0.0f;          //!< 色の付いた部分の先の距離。単位は m
            bool fullyColored = false;        //!< 溜めきりで、矢じりの先まで全部に色を付ける場合 true
            NS::Vector3 stageColor;           //!< 色の付いた部分の色。RGB で各 0〜1
            std::vector<SlamArrowPiece> band; //!< 帯を貼る板。自機の側から並ぶ。床の無い所は入れない
            bool hasHead = false;             //!< 矢じりの下に床がある場合 true
            SlamArrowPiece head{};            //!< 矢じりを貼る板。hasHead が偽の間は読まない
        };

        //! @brief 床を探す関数。from から真下へ maxDepth までの間の床の高さを outGroundY に返す
        //! @details 床があれば true、無ければ false を返し、outGroundY を書き換えない
        using SlamArrowGroundProbe = std::function<bool(const NS::Vector3& from, float maxDepth, float& outGroundY)>;

        //! 溜めと狙いから矢印の形・色を組む。床への配置は別途行う
        //! 帯は玉の縁から始まり、相手への接触点か突進の終点まで growFrames で伸びる
        //! 不正な調整値・入力なら false で outShape を変更しない。成功時も帯と矢じりの板は空
        [[nodiscard]] bool BuildSlamArrow(const SlamArrowState& state,
                                          const SlamArrowDesc& desc,
                                          SlamArrowShape& outShape);

        //! BuildSlamArrow の形を床へ貼り、帯と矢じりの板を置き直す
        //! 起点の床が無ければ置かない。起点の床より深い所には貼らない
        void PlaceSlamArrowOnGround(const SlamArrowGroundProbe& probe,
                                    const SlamArrowDesc& desc,
                                    SlamArrowShape& shape);

        //! BuildSlamArrow の形を玉の底の道筋へ置き、板を置き直す。ballCenterHeight は放つ時の世界 y
        //! 道筋に床があれば床上へ置く。床の探索は玉の中心の終点までとし、その先は終点直下の床を使う
        //! probe が空なら床を探さない
        void PlaceSlamArrowOnPath(const SlamArrowGroundProbe& probe,
                                  const GL::Player::LaunchPath& path,
                                  float ballCenterHeight,
                                  const SlamArrowDesc& desc,
                                  SlamArrowShape& shape);

        //! 矢印の板 1 枚ぶんの、描く単位へ写す値。Shaders/ground_arrow.ps.hlsl の cbuffer と同じ並び
        //! @details 頂点シェーダは standard.vs.hlsl をそのまま使うので FrameCB と同じ大きさにし、world と viewProj を
        //! 同じ位置に置く。残りは照明の欄の場所に矢印の値を置く。板の v は 0 が遠い端、1 が近い端
        struct alignas(16) SlamArrowConstants
        {
            NS::Matrix world{};         //!< 上向きの板を線に沿った範囲と幅へ伸ばして置く行列
            NS::Matrix viewProj{};      //!< ビュー × 射影
            NS::Vector4 chargedColor{}; //!< rgb は色の付いた部分の色、a は明るい縁の不透明度
            NS::Vector4 plainColor{};   //!< rgb は色の付いていない部分の色、a は明るい縁の不透明度
            NS::Vector4 darkColor{};    //!< rgb は暗い縁の色、a はその不透明度
            //! xy は始まりのぼかし、zw は色の付いた部分の重み。どちらも板の v の 1 次式 (v = 0 の値と v あたりの変化)
            NS::Vector4 fadeAndFront{};
            //! xy は帯の切れ目を測る、矢じりの先からの距離 ÷ 矢じりの奥行き (板の v の 1 次式)。
            //! z と w は色の付いた部分と付いていない部分の塗りの平均の不透明度
            NS::Vector4 rearAndFill{};
            NS::Gfx::TremorCB tremor{}; //!< standard.vs.hlsl が読む震えの欄。振れ幅 0 のまま送り、矢印は震わせない
            NS::Vector4 opacity{};      //!< FrameCB の不透明度の欄の場所。矢印のシェーダーは読まない
        };

        //! 矢印を描く資材。どれも非所有
        struct SlamArrowDrawAssets
        {
            NS::Gfx::StaticMesh* mesh = nullptr; //!< 共有の上向きの板
            NS::Gfx::Material* bandMaterial = nullptr;
            NS::Gfx::Material* headMaterial = nullptr;
        };

        //! 配置済みの帯・矢じりと、隠れた部分用の矢じりを out へ追加する。資材が空でも追加する
        //! 板は両面を描く。低い視点では指定の上限まで矢じりを起こす
        //! viewProjection はビュー × 射影、cameraPosition は世界座標
        void AppendSlamArrowDrawItems(const SlamArrowShape& shape,
                                      const SlamArrowDesc& desc,
                                      const NS::Matrix& viewProjection,
                                      const NS::Vector3& cameraPosition,
                                      const SlamArrowDrawAssets& assets,
                                      std::vector<NS::Gfx::DrawItem>& out);

        //! @brief 溜め中に、放つ玉の道筋へ矢印を描く部品
        //! @details 接地した水平突進は床へ貼り、それ以外は LaunchPitch と同じ道筋へ置く
        //! にじみの後に奥行きを読み、隠れた矢じりも薄く描く
        class SlamArrow : public NS::OverlayRenderer
        {
        public:
            SlamArrow() noexcept;

            void ResolveAssets(NS::AssetManager& assets) override;

            //! @brief 溜め中の矢印を控える。狙いが無いか溜めを終えたら、控えと経過フレーム数を戻す
            //! @details 道筋は params の突進の速さと重力、玉の半径と床は collider (持ち主の移動の当たり) から出す
            void Update(const GL::Player::ChargeStatus& charge,
                        const GL::Player::PlayerParams* params,
                        const NS::Collider* collider);

            //! ロックオンの枠 (TargetMarker の 0) より先に描き、枠を矢印の上に重ねる
            [[nodiscard]] int OverlayOrder() const noexcept override { return -1; }

            void OnRenderOverlay(const NS::Gfx::RenderContext& context) override;

            //! @brief このフレームに控えた矢印を読む
            //! @param[out] outShape 控えた形。控えが無い場合は書き換えない
            //! @return 控えがある場合 true、それ以外の場合は false
            [[nodiscard]] bool TryGetShownArrow(SlamArrowShape& outShape) const;

            NS_REFLECT_BEGIN(SlamArrow, NS::OverlayRenderer)
            NS_REFLECT_GROUP("形")
            NS_REFLECT_FIELD(m_desc.growFrames, "矢印が伸びるフレーム数")
            NS_REFLECT_FIELD(m_desc.groundLift, "矢印を浮かせる高さ")
            NS_REFLECT_FIELD(m_desc.headWidth, "矢じりの幅")
            NS_REFLECT_FIELD(m_desc.headDepthRatio, "矢じりの奥行きの割合")
            NS_REFLECT_FIELD(m_desc.headDepthMin, "矢じりの奥行きの下限")
            NS_REFLECT_FIELD(m_desc.headDepthMax, "矢じりの奥行きの上限")
            NS_REFLECT_FIELD(m_desc.headMinViewDegrees, "矢じりを見せる最小の角度")
            NS_REFLECT_FIELD(m_desc.startFade, "帯の始まりのぼかし")
            NS_REFLECT_FIELD(m_desc.frontSoftness, "色の境目のぼかし")
            NS_REFLECT_GROUP("色")
            NS_REFLECT_FIELD(m_desc.lateStageFrom, "後半の色へ変わる溜め量")
            NS_REFLECT_FIELD(m_desc.earlyColor, "溜めの前半の色")
            NS_REFLECT_FIELD(m_desc.lateColor, "溜めの後半の色")
            NS_REFLECT_FIELD(m_desc.fullColor, "溜めきりの色")
            NS_REFLECT_FIELD(m_desc.overchargeColor, "溜めすぎの色")
            NS_REFLECT_FIELD(m_desc.plainColor, "色の付いていない部分の色")
            NS_REFLECT_FIELD(m_desc.darkColor, "矢印の暗い縁の色")
            NS_REFLECT_GROUP("不透明度")
            NS_REFLECT_FIELD(m_desc.darkAlpha, "矢印の暗い縁の不透明度")
            NS_REFLECT_FIELD(m_desc.bandEdgeAlpha, "帯の明るい縁の不透明度")
            NS_REFLECT_FIELD(m_desc.bandFillAlpha, "帯の塗りの不透明度")
            NS_REFLECT_FIELD(m_desc.headEdgeAlpha, "矢じりの明るい縁の不透明度")
            NS_REFLECT_FIELD(m_desc.headFillAlpha, "矢じりの塗りの不透明度")
            NS_REFLECT_FIELD(m_desc.plainBandEdgeAlpha, "色の無い帯の明るい縁の不透明度")
            NS_REFLECT_FIELD(m_desc.plainBandFillAlpha, "色の無い帯の塗りの不透明度")
            NS_REFLECT_FIELD(m_desc.occludedHeadAlpha, "隠れた矢じりの不透明度")
            NS_REFLECT_FIELD(m_desc.groundProbeSpacing, "床探索の刻み")
            NS_REFLECT_FIELD(m_desc.ballGroundSearchDepth, "床探索の深さ")
            NS_REFLECT_FIELD(m_desc.sameHeightTolerance, "同じ床高さの誤差")
            NS_REFLECT_FIELD(m_desc.maxBandPieces, "帯の分割上限")
            NS_REFLECT_FIELD(m_desc.bandTextureSpan, "帯の画像内の幅比")
            NS_REFLECT_FIELD(m_desc.headTextureSpan, "矢じりの画像内の幅比")
            NS_REFLECT_FIELD(m_desc.maxHeadTiltDegrees, "矢じりを起こす角度の上限")
            NS_REFLECT_FIELD(m_meshAsset, "板の資産")
            NS_REFLECT_FIELD(m_bandMaterialAsset, "帯の材質の資産")
            NS_REFLECT_FIELD(m_headMaterialAsset, "矢じりの材質の資産")
            NS_REFLECT_END()

        private:
            std::string m_meshAsset = "shadowQuad";
            std::string m_bandMaterialAsset = "Assets/Materials/ground_arrow_band.mat";
            std::string m_headMaterialAsset = "Assets/Materials/ground_arrow_head.mat";
            SlamArrowDesc m_desc{};
            SlamArrowShape m_shown{}; // 控えた矢印。m_hasShown が偽の間は読まない
            bool m_hasShown = false;
            int m_framesSinceShown = -1;                  // 矢印を出していない間は負
            NS::Gfx::StaticMesh* m_mesh = nullptr;        // 共有の上向きの板 (非所有)
            NS::Gfx::Material* m_bandMaterial = nullptr;  // 帯のマテリアル (非所有)
            NS::Gfx::Material* m_headMaterial = nullptr;  // 矢じりのマテリアル (非所有)
            std::vector<NS::Gfx::DrawItem> m_drawScratch; // 描く単位を毎フレーム積み直す置き場。確保を使い回す
        };
    } // namespace Level
} // namespace GL
