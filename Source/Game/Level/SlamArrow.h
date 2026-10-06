#pragma once

#include "Game/Level/SlamAim.h"
#include "Game/Player/LaunchPitch.h"
#include "Game/Player/PlayerVisualParams.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Graphics/DrawItem.h"
#include "NSlib/Graphics/FrameConstants.h"
#include "NSlib/Object/Components/OverlayRenderer.h"

#include <functional>
#include <vector>

class Player;

namespace NS::Gfx
{
    class Material;
    class StaticMesh;
} // namespace NS::Gfx

namespace NS::Obj
{
    class Collider;
}

namespace NS::Game::Level
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
        int framesSinceShown = 0;  //!< 矢印を出したフレームを 0 として数えたフレーム数
        float charge01 = 0.0f;     //!< 溜め量 0..1
        bool chargeFull = false;   //!< 溜めきりの場合 true
        float overcharge01 = 0.0f; //!< 溜めすぎの深さ 0..1。溜めきりの間だけ色に効く
    };

    //! 地面の矢印の形と色を決める値

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
        NS::Vector3 origin;         //!< 狙いの線の始まり。世界座標
        NS::Vector3 direction;      //!< 狙いの水平の向き。正規化済みで y は 0
        float bandWidth = 0.0f;           //!< 帯の幅 (玉の通る幅)。単位は m
        float start = 0.0f;               //!< 帯の始まり (玉の縁) の距離。単位は m
        float tip = 0.0f;                 //!< このフレームの矢じりの先の距離。伸びている途中はその長さ。単位は m
        float fullTip = 0.0f;             //!< 伸びきった矢じりの先の距離。単位は m
        float headDepth = 0.0f;           //!< 矢じりの奥行き。単位は m
        float colorFront = 0.0f;          //!< 色の付いた部分の先の距離。単位は m
        bool fullyColored = false;        //!< 溜めきりで、矢じりの先まで全部に色を付ける場合 true
        NS::Vector3 stageColor;     //!< 色の付いた部分の色。RGB で各 0〜1
        std::vector<SlamArrowPiece> band; //!< 帯を貼る板。自機の側から並ぶ。床の無い所は入れない
        bool hasHead = false;             //!< 矢じりの下に床がある場合 true
        SlamArrowPiece head{};            //!< 矢じりを貼る板。hasHead が偽の間は読まない
    };

    //! @brief 床を探す関数。from から真下へ maxDepth までの間の床の高さを outGroundY に返す
    //! @details 床があれば true、無ければ false を返し、outGroundY を書き換えない
    using SlamArrowGroundProbe = std::function<bool(const NS::Vector3& from, float maxDepth, float& outGroundY)>;

    //! @brief 溜めと狙いの様子から、地面の矢印の長さ・色の付いた部分・色を組む。床の高さは置かない
    //! @details 帯は玉の縁 (state.ballRadius) から始まる。伸びきった先は、狙う相手がいれば state.targetContact +
    //! state.ballRadius で狙う相手の手前の面、いなければ線の長さ + state.ballRadius で突進が止まる所の玉の縁。
    //! このフレームの先は、矢印を出したフレームを 1 フレーム目として、desc.growFrames フレームで
    //! 伸びきった先まで等速に伸びる。矢じりの奥行きは、desc.headDepthRatio × 先の距離を desc.headDepthMin と
    //! desc.headDepthMax で抑えた値。色の付いた部分の先は、玉の縁 + (伸びきった先 − 玉の縁) × 溜め量。
    //! 溜めきりなら全部に色を付ける。
    //! 段の色は、溜めきりなら desc.fullColor、溜め量が desc.lateStageFrom 未満なら desc.earlyColor、
    //! それ以外は desc.lateColor。伸びきった先が玉の縁より手前なら、先を玉の縁に置き、描く物は無い
    //! @param[in] state 溜めと狙いの様子
    //! @param[in] desc 形と色を決める値
    //! @param[out] outShape 組んだ形。帯の板と矢じりの板は空。false の場合は書き換えない
    //! @return 組めた場合 true。desc の 0 以下にできない欄が 0 以下の場合、有限でない値がある場合、
    //! 狙いの線の向きの水平の長さが 0 の場合と、フレーム数が負の場合は false
    [[nodiscard]] bool BuildSlamArrow(const SlamArrowState& state, const SlamArrowDesc& desc, SlamArrowShape& outShape);

    //! @brief 組んだ矢印の真下の床を探し、帯の板と矢じりの板を置く
    //! @details 玉の下の床 (線の始まりの真下) を先に探す。見つからなければ何も置かない。
    //! 帯は玉の縁から先までを 0.1 m の区切りに分け、区切りの真ん中の真下を、玉の下の床より玉の半径だけ深い所まで探す。
    //! 床のある区切りを床の高さ + groundLift に置き、同じ高さで続く区切りは 1 枚につなぐ。床の無い区切りは置かない。
    //! 矢じりは奥行きの真ん中の真下を同じ深さまで探し、床があれば置く
    //! @param[in] probe 床を探す関数
    //! @param[in] groundLift 床から浮かせる高さ。単位は m
    //! @param[in,out] shape BuildSlamArrow が組んだ形。帯の板と矢じりの板を置き直す
    void PlaceSlamArrowOnGround(const SlamArrowGroundProbe& probe, float groundLift, SlamArrowShape& shape);

    //! @brief 組んだ矢印を、放った玉の一番下の点が通る道筋に置く
    //! @details 帯は玉の縁から先までを 0.1 m の区切りに分け、区切りの両端の高さを結んだ傾いた板にする。
    //! 端の高さは、玉の中心 (ballCenterHeight + LaunchHeightAt) から玉の半径を引いた高さ + groundLift。
    //! 玉の中心の高さと放った高さの高い方から、その一番下の点までの間に床があれば、玉は床に着いて転がるので床の高さに置く。
    //! 床を見るのは玉の中心が止まる所 (伸びきった先 − 玉の半径) までで、その先は止まる所の真下の床を見る。
    //! 矢じりも奥行きの両端の高さで同じく置く
    //! @param[in] probe 床を探す関数。空なら床を見ない
    //! @param[in] path 放った玉の道筋
    //! @param[in] ballCenterHeight 放つ時の玉の中心の高さ (世界の y)
    //! @param[in] groundLift 玉の一番下の点から浮かせる高さ。単位は m
    //! @param[in,out] shape BuildSlamArrow が組んだ形。帯の板と矢じりの板を置き直す
    void PlaceSlamArrowOnPath(const SlamArrowGroundProbe& probe,
                              const NS::Game::Player::LaunchPath& path,
                              float ballCenterHeight,
                              float groundLift,
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
    };

    //! 矢印を描く資材。どれも非所有
    struct SlamArrowDrawAssets
    {
        NS::Gfx::StaticMesh* mesh = nullptr;       //!< 共有の上向きの板
        NS::Gfx::Material* bandMaterial = nullptr; //!< 帯のマテリアル
        NS::Gfx::Material* headMaterial = nullptr; //!< 矢じりのマテリアル
    };

    //! @brief 置いた矢印の帯の板と矢じりの板、隠れた所へ描く矢じりの板を、描く単位にして out の後ろへ積む
    //! @details 帯の板を shape.band の順に積み、矢じりがあれば矢じりの板と、隠れた画素だけへ描く矢じりの板を積む。
    //! 帯は始まりでぼかし、色の付いた部分の先 (shape.colorFront) で色を切る。矢じりは溜めの始めから段の色で全部を塗り、
    //! ぼかさない。どの板も裏からも描く。カメラが矢じりの後ろにあり、矢じりの手前の端を見る角度が
    //! desc.headMinViewDegrees を下回る時は、矢じりの板を手前の端を軸に長さを変えずにカメラの方へ起こし、
    //! 手前の端をその角度で見せる。起こすのは 80 度まで。資材が空でも積む。空のまま描かないかは呼び手が見る
    //! @param[in] shape PlaceSlamArrowOnGround か PlaceSlamArrowOnPath で置いた形
    //! @param[in] desc 色と不透明度を決める値
    //! @param[in] viewProjection 描く視点のビュー × 射影
    //! @param[in] cameraPosition 描く視点のカメラの位置。世界座標
    //! @param[in] assets 板とマテリアル
    //! @param[in,out] out 描く単位の並び
    void AppendSlamArrowDrawItems(const SlamArrowShape& shape,
                                  const SlamArrowDesc& desc,
                                  const NS::Matrix& viewProjection,
                                  const NS::Vector3& cameraPosition,
                                  const SlamArrowDrawAssets& assets,
                                  std::vector<NS::Gfx::DrawItem>& out);

    //! @brief 溜めている間、狙いの線の向きへ放った玉の道筋に矢印を描く Component
    //! @details 溜めている間 (Player::ChargeJudge の IsCharging) に、同じ配置物の Player が控えた狙いの線と
    //! 狙う相手と溜め量から BuildSlamArrow で形を組む。狙う相手はいなくても組む。
    //! 接地していて縦の速さが 0 なら、今までどおり所属シーンの当たりへの光線で PlaceSlamArrowOnGround が床に貼る。
    //! 届く相手への弧と空中は PlaceSlamArrowOnPath が LaunchPitch と同じ重力の道筋に置く。
    //! 形は OnUpdate で組んで控え、描く時は板を積むだけ。
    //! 狙いの線が無いフレームと放したフレームは何も組まない。
    //! 板は組み込みの上向きの板 shadowQuad に、帯と矢じりのマテリアル (Shaders/ground_arrow.ps.hlsl) を貼った半透明。
    //! 矢じりはもう 1 枚、手前の物に隠れた画素だけへ薄く描き、相手や自機の玉の後ろに入った先も見せる。
    //! 世界を描いてにじませた後の重ね描きで、世界の奥行きを読んで描く。溜めの光とそのにじみが矢じりを白く覆わない。
    //! 矢印の板は 1 以下の色で書くので、にじみの前に描いてもにじまない
    //! 依存: Player, SlamAim (AimLine), NS::Obj::Collider, NS::Obj::Scene, NS::Obj::IUseCollision
    class SlamArrow : public NS::Obj::OverlayRenderer
    {
    public:
        SlamArrow() noexcept;

        //! 重ね描きの登録簿へ入り、同じ配置物の Player と身体を引き当てる。
        //! どちらかが無ければ以後何も組まない
        void OnStart() override;

        //! 組み込みの板と、帯と矢じりのマテリアルを引き当てる
        void ResolveAssets(NS::Obj::AssetManager& assets) override;

        //! 溜めていて狙いの線がある間は矢印を出してからのフレーム数を数え、矢印を組んで道筋に置いて控える。
        //! 溜めていないか狙いの線が無いフレームは控えを消し、フレーム数を戻す
        void OnUpdate() override;

        //! ロックオンの枠 (TargetMarker の 0) より先に描き、枠を矢印の上に重ねる
        [[nodiscard]] int OverlayOrder() const noexcept override { return -1; }

        //! 控えた矢印の帯の板と矢じりの板、隠れた所へ描く矢じりの板を描く。控えが無いか、資材が引けていないか、
        //! 描く装置が無ければ何も描かない
        void OnRenderOverlay(const NS::Gfx::RenderContext& context) override;

        //! @brief このフレームに控えた矢印を読む
        //! @param[out] outShape 控えた形。控えが無い場合は書き換えない
        //! @return 控えがある場合 true、それ以外の場合は false
        [[nodiscard]] bool TryGetShownArrow(SlamArrowShape& outShape) const;

        NS_REFLECT_BEGIN(SlamArrow, NS::Obj::OverlayRenderer)
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
        NS_REFLECT_END()

    private:
        SlamArrowDesc m_desc{};   // 矢印の見た目の調整値
        SlamArrowShape m_shown{}; // 控えた矢印。m_hasShown が偽の間は読まない
        bool m_hasShown = false;
        int m_framesSinceShown = -1;                   // 矢印を出していない間は負
        const ::Player* m_player = nullptr;            // 溜めと狙いの問い先。非所有
        const NS::Obj::Collider* m_collider = nullptr; // 玉の半径と床の問い先。非所有
        NS::Gfx::StaticMesh* m_mesh = nullptr;         // 共有の上向きの板 (非所有)
        NS::Gfx::Material* m_bandMaterial = nullptr;   // 帯のマテリアル (非所有)
        NS::Gfx::Material* m_headMaterial = nullptr;   // 矢じりのマテリアル (非所有)
        std::vector<NS::Gfx::DrawItem> m_drawScratch;  // 描く単位を毎フレーム積み直す置き場。確保を使い回す
    };
} // namespace NS::Game::Level
