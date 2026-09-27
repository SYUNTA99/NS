#pragma once

#include "Game/Level/CollisionInput.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/IRenderable.h"

#include <functional>
#include <vector>

namespace NS::Gfx
{
    class Material;
    class StaticMesh;
} // namespace NS::Gfx

namespace NS::Game::Player
{
    class PlayerComponent;
}

namespace NS::Game::Level
{
    //! 地面の矢印を組む時点の、溜めと狙いの様子
    struct SlamArrowState
    {
        AimLine line{};                 //!< 狙いの線。矢印はこの線の真下の床に貼る
        float ballRadius = 0.0f;        //!< 自機の玉の半径。単位は m。帯の幅はこの 2 倍
        bool hasTarget = false;         //!< 線の上に狙う相手が居る場合 true
        float targetContact = 0.0f;     //!< 線を進む玉の縁が狙う相手に触れる所までの、線に沿った距離。単位は m
        int framesSinceChargeStart = 0; //!< 溜めに入ったフレームを 0 として数えたフレーム数
        float charge01 = 0.0f;          //!< 溜め量 0..1
        bool chargeFull = false;        //!< 溜めきりの場合 true
    };

    //! 地面の矢印の形と色を決める値
    struct SlamArrowDesc
    {
        int growFrames = 10;               //!< 溜めに入ってから先まで伸びきるフレーム数
        float groundLift = 0.03f;          //!< 床から浮かせる高さ。単位は m
        float headWidth = 1.75f;           //!< 矢じりの幅。単位は m
        float headDepthRatio = 0.28f;      //!< 矢じりの奥行きの、玉の中心から先までの距離に対する割合
        float headDepthMin = 1.3f;         //!< 矢じりの奥行きの下限。単位は m
        float headDepthMax = 2.8f;         //!< 矢じりの奥行きの上限。単位は m
        float startFade = 0.5f;            //!< 帯の始まりをぼかす長さ。単位は m
        float frontSoftness = 0.3f;        //!< 色の付いた部分の先の境目をぼかす幅。単位は m
        float lateStageFrom = 1.0f / 3.0f; //!< 溜めの後半の色へ変わる溜め量
        NS::Core::Vector3 earlyColor{
            72.0f / 255.0f, 230.0f / 255.0f, 120.0f / 255.0f};              //!< 溜めの前半の色。RGB で各 0〜1
        NS::Core::Vector3 lateColor{1.0f, 208.0f / 255.0f, 48.0f / 255.0f}; //!< 溜めの後半の色。RGB で各 0〜1
        NS::Core::Vector3 fullColor{1.0f, 64.0f / 255.0f, 56.0f / 255.0f};  //!< 溜めきりの色。RGB で各 0〜1
        NS::Core::Vector3 plainColor{
            224.0f / 255.0f, 232.0f / 255.0f, 242.0f / 255.0f}; //!< 色の付いていない部分の色。RGB で各 0〜1
        NS::Core::Vector3 darkColor{12.0f / 255.0f, 20.0f / 255.0f, 36.0f / 255.0f}; //!< 暗い縁の色。RGB で各 0〜1
        float darkAlpha = 1.0f;                                                      //!< 暗い縁の不透明度
        float bandEdgeAlpha = 0.85f;      //!< 色の付いた部分の帯の明るい縁の不透明度
        float bandFillAlpha = 0.30f;      //!< 色の付いた部分の帯の塗りの不透明度。塗り全体の平均
        float headEdgeAlpha = 0.95f;      //!< 色の付いた部分の矢じりの明るい縁の不透明度
        float headFillAlpha = 0.85f;      //!< 色の付いた部分の矢じりの塗りの不透明度
        float plainBandEdgeAlpha = 0.30f; //!< 色の付いていない部分の帯の明るい縁の不透明度
        float plainBandFillAlpha = 0.08f; //!< 色の付いていない部分の帯の塗りの不透明度。塗り全体の平均
        float plainHeadEdgeAlpha = 0.35f; //!< 色の付いていない部分の矢じりの明るい縁の不透明度
        float plainHeadFillAlpha = 0.18f; //!< 色の付いていない部分の矢じりの塗りの不透明度
    };

    //! 床に貼る板 1 枚ぶんの、線に沿った範囲と高さ
    struct SlamArrowPiece
    {
        float alongNear = 0.0f; //!< 自機に近い端の、線に沿った距離。単位は m
        float alongFar = 0.0f;  //!< 自機から遠い端の、線に沿った距離。単位は m
        float height = 0.0f;    //!< 板を置く高さ (世界の y)。床の高さに浮かせる高さを足した値
    };

    //! 地面の矢印の形と色。距離は狙いの線の始まり (自機の位置) から線に沿って測る
    struct SlamArrowShape
    {
        NS::Core::Vector3 origin;         //!< 狙いの線の始まり。世界座標
        NS::Core::Vector3 direction;      //!< 狙いの水平の向き。正規化済みで y は 0
        float bandWidth = 0.0f;           //!< 帯の幅 (玉の通る幅)。単位は m
        float start = 0.0f;               //!< 帯の始まり (玉の縁) の距離。単位は m
        float tip = 0.0f;                 //!< このフレームの矢じりの先の距離。伸びている途中はその長さ。単位は m
        float fullTip = 0.0f;             //!< 伸びきった矢じりの先の距離。単位は m
        float headDepth = 0.0f;           //!< 矢じりの奥行き。単位は m
        float colorFront = 0.0f;          //!< 色の付いた部分の先の距離。単位は m
        bool fullyColored = false;        //!< 溜めきりで、矢じりの先まで全部に色を付ける場合 true
        NS::Core::Vector3 stageColor;     //!< 色の付いた部分の色。RGB で各 0〜1
        std::vector<SlamArrowPiece> band; //!< 帯を貼る板。自機の側から並ぶ。床の無い所は入れない
        bool hasHead = false;             //!< 矢じりの下に床がある場合 true
        SlamArrowPiece head{};            //!< 矢じりを貼る板。hasHead が偽の間は読まない
    };

    //! @brief 床を探す関数。from から真下へ maxDepth までの間の床の高さを outGroundY に返す
    //! @details 床があれば true、無ければ false を返し、outGroundY を書き換えない
    using SlamArrowGroundProbe = std::function<bool(const NS::Core::Vector3& from, float maxDepth, float& outGroundY)>;

    //! @brief 溜めと狙いの様子から、地面の矢印の長さ・色の付いた部分・色を組む。床の高さは置かない
    //! @details 帯は玉の縁 (state.ballRadius) から始まる。伸びきった先は、狙う相手がいれば
    //! state.targetContact + state.ballRadius (相手の手前の面)、いなければ state.line.length (突進が止まる所)。
    //! このフレームの先は、溜めに入ったフレームを 1 フレーム目として、desc.growFrames フレームで伸びきった先まで
    //! 等速に伸びる。矢じりの奥行きは、desc.headDepthRatio × 先の距離を desc.headDepthMin と desc.headDepthMax で
    //! 抑えた値。色の付いた部分の先は、玉の縁 + (伸びきった先 − 玉の縁) × 溜め量。溜めきりなら全部に色を付ける。
    //! 段の色は、溜めきりなら desc.fullColor、溜め量が desc.lateStageFrom 未満なら desc.earlyColor、
    //! それ以外は desc.lateColor。伸びきった先が玉の縁より手前なら、先を玉の縁に置き、描く物は無い
    //! @param[in] state 溜めと狙いの様子
    //! @param[in] desc 形と色を決める値
    //! @param[out] outShape 組んだ形。帯の板と矢じりの板は空。false の場合は書き換えない
    //! @return 組めた場合 true。desc の 0 以下にできない欄が 0 以下の場合、有限でない値がある場合、
    //! 狙いの向きの水平の長さが 0 の場合と、フレーム数が負の場合は false
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

    //! @brief 溜めている間、突進の線の真下の床に矢印を貼って描く Component
    //! @details 溜めている間 (CollisionInput::IsCharging) だけ、同じ配置物の CollisionInput が控えた狙いの線と
    //! 狙う相手と溜め量から BuildSlamArrow で形を組み、所属シーンの当たりへの光線で PlaceSlamArrowOnGround が
    //! 床に置く。形は OnUpdate で組んで控え、描く時は板を積むだけ。放したフレームは何も組まない。
    //! 板は組み込みの上向きの板 shadowQuad に、帯と矢じりのマテリアル (Shaders/ground_arrow.ps.hlsl) を貼った半透明
    //! 依存: CollisionInput, NS::Game::Player::PlayerComponent, NS::Obj::Scene, NS::Phys::PhysicsScene
    class SlamArrow : public NS::Obj::Component, public NS::Obj::IRenderable
    {
    public:
        SlamArrow() noexcept;

        //! 描く物の登録簿へ入り、同じ配置物の CollisionInput と PlayerComponent を引き当てる。
        //! どちらかが無ければ以後何も組まない
        void OnStart() override;

        //! 描く物の登録簿から出る
        void OnEndPlay() override;

        //! 組み込みの板と、帯と矢じりのマテリアルを引き当てる
        void ResolveAssets(NS::Obj::AssetManager& assets) override;

        //! 溜めている間は溜めに入ってからのフレーム数を数え、矢印を組んで床に置いて控える。
        //! 溜めていないフレームは控えを消す
        void OnUpdate() override;

        //! 控えた矢印の帯の板と矢じりの板を積む。控えが無いか、資材が引けていなければ何も積まない
        void Collect(const NS::Gfx::RenderContext& context, std::vector<NS::Gfx::DrawItem>& out) override;

        //! 半透明の並びに入る
        [[nodiscard]] NS::Obj::RenderBucket Bucket() const noexcept override
        {
            return NS::Obj::RenderBucket::Transparent;
        }

        //! 半透明の並びの中心。自機の影と同じ自機の位置
        [[nodiscard]] NS::Core::Vector3 SortCenter() const noexcept override;

        //! 自機の影 (優先度 0) より後に描く
        [[nodiscard]] int SortPriority() const noexcept override { return 1; }

        //! 控えた矢印を覆う箱。控えが無ければ自機の位置の大きさ 0 の箱
        [[nodiscard]] NS::Core::AABB WorldBounds() const noexcept override;

        //! @brief このフレームに控えた矢印を読む
        //! @param[out] outShape 控えた形。控えが無い場合は書き換えない
        //! @return 控えがある場合 true、それ以外の場合は false
        [[nodiscard]] bool TryGetShownArrow(SlamArrowShape& outShape) const;

        NS_REFLECT_BEGIN(SlamArrow, NS::Obj::Component)
        NS_REFLECT_FIELD(m_desc.growFrames, "矢印が伸びるフレーム数")
        NS_REFLECT_FIELD(m_desc.groundLift, "矢印を浮かせる高さ")
        NS_REFLECT_FIELD(m_desc.headWidth, "矢じりの幅")
        NS_REFLECT_FIELD(m_desc.headDepthRatio, "矢じりの奥行きの割合")
        NS_REFLECT_FIELD(m_desc.headDepthMin, "矢じりの奥行きの下限")
        NS_REFLECT_FIELD(m_desc.headDepthMax, "矢じりの奥行きの上限")
        NS_REFLECT_FIELD(m_desc.startFade, "帯の始まりのぼかし")
        NS_REFLECT_FIELD(m_desc.frontSoftness, "色の境目のぼかし")
        NS_REFLECT_FIELD(m_desc.lateStageFrom, "後半の色へ変わる溜め量")
        NS_REFLECT_FIELD(m_desc.earlyColor, "溜めの前半の色")
        NS_REFLECT_FIELD(m_desc.lateColor, "溜めの後半の色")
        NS_REFLECT_FIELD(m_desc.fullColor, "溜めきりの色")
        NS_REFLECT_FIELD(m_desc.plainColor, "色の付いていない部分の色")
        NS_REFLECT_FIELD(m_desc.darkColor, "矢印の暗い縁の色")
        NS_REFLECT_FIELD(m_desc.darkAlpha, "矢印の暗い縁の不透明度")
        NS_REFLECT_FIELD(m_desc.bandEdgeAlpha, "帯の明るい縁の不透明度")
        NS_REFLECT_FIELD(m_desc.bandFillAlpha, "帯の塗りの不透明度")
        NS_REFLECT_FIELD(m_desc.headEdgeAlpha, "矢じりの明るい縁の不透明度")
        NS_REFLECT_FIELD(m_desc.headFillAlpha, "矢じりの塗りの不透明度")
        NS_REFLECT_FIELD(m_desc.plainBandEdgeAlpha, "色の無い帯の明るい縁の不透明度")
        NS_REFLECT_FIELD(m_desc.plainBandFillAlpha, "色の無い帯の塗りの不透明度")
        NS_REFLECT_FIELD(m_desc.plainHeadEdgeAlpha, "色の無い矢じりの明るい縁の不透明度")
        NS_REFLECT_FIELD(m_desc.plainHeadFillAlpha, "色の無い矢じりの塗りの不透明度")
        NS_REFLECT_END()

    private:
        SlamArrowDesc m_desc{};
        SlamArrowShape m_shown{}; // 控えた矢印。m_hasShown が偽の間は読まない
        bool m_hasShown = false;
        int m_framesSinceChargeStart = -1; // 溜めていない間は負
        const CollisionInput* m_input = nullptr;
        const NS::Game::Player::PlayerComponent* m_movement = nullptr;
        NS::Gfx::StaticMesh* m_mesh = nullptr;       // 共有の上向きの板 (非所有)
        NS::Gfx::Material* m_bandMaterial = nullptr; // 帯のマテリアル (非所有)
        NS::Gfx::Material* m_headMaterial = nullptr; // 矢じりのマテリアル (非所有)
    };
} // namespace NS::Game::Level
