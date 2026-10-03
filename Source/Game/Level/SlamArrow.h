#pragma once

#include "Game/Level/SlamAim.h"
#include "Game/Player/LaunchPitch.h"
#include "Game/Player/PlayerVisualParams.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/IRenderable.h"

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
        int framesSinceShown = 0; //!< 矢印を出したフレームを 0 として数えたフレーム数
        float charge01 = 0.0f;    //!< 溜め量 0..1
        bool chargeFull = false;  //!< 溜めきりの場合 true
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

    //! @brief 溜めている間、狙いの線の向きへ放った玉の道筋に矢印を描く Component
    //! @details 溜めている間 (Player::ChargeJudge の IsCharging) に、同じ配置物の Player が控えた狙いの線と
    //! 狙う相手と溜め量から BuildSlamArrow で形を組む。狙う相手はいなくても組む。
    //! 接地していて縦の速さが 0 なら、今までどおり所属シーンの当たりへの光線で PlaceSlamArrowOnGround が床に貼る。
    //! 届く相手への弧と空中は PlaceSlamArrowOnPath が LaunchPitch と同じ重力の道筋に置く。
    //! 形は OnUpdate で組んで控え、描く時は板を積むだけ。
    //! 狙いの線が無いフレームと放したフレームは何も組まない。
    //! 板は組み込みの上向きの板 shadowQuad に、帯と矢じりのマテリアル (Shaders/ground_arrow.ps.hlsl) を貼った半透明
    //! 依存: Player, SlamAim (AimLine), NS::Obj::Collider, NS::Obj::Scene, NS::Obj::IUseCollision
    class SlamArrow : public NS::Obj::Component, public NS::Obj::IRenderable
    {
    public:
        SlamArrow() noexcept;

        //! 描く物の登録簿へ入り、同じ配置物の Player と身体を引き当てる。
        //! どちらかが無ければ以後何も組まない
        void OnStart() override;

        //! 描く物の登録簿から出る
        void OnEndPlay() override;

        //! 組み込みの板と、帯と矢じりのマテリアルを引き当てる
        void ResolveAssets(NS::Obj::AssetManager& assets) override;

        //! 溜めていて狙いの線がある間は矢印を出してからのフレーム数を数え、矢印を組んで道筋に置いて控える。
        //! 溜めていないか狙いの線が無いフレームは控えを消し、フレーム数を戻す
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

        NS_REFLECT_NONE(SlamArrow, NS::Obj::Component)

    private:
        [[nodiscard]] const SlamArrowDesc& Tuning() const noexcept;
        SlamArrowShape m_shown{}; // 控えた矢印。m_hasShown が偽の間は読まない
        bool m_hasShown = false;
        int m_framesSinceShown = -1;                   // 矢印を出していない間は負
        const ::Player* m_player = nullptr;            // 溜めと狙いの問い先。非所有
        const NS::Obj::Collider* m_collider = nullptr; // 玉の半径と床の問い先。非所有
        NS::Gfx::StaticMesh* m_mesh = nullptr;         // 共有の上向きの板 (非所有)
        NS::Gfx::Material* m_bandMaterial = nullptr;   // 帯のマテリアル (非所有)
        NS::Gfx::Material* m_headMaterial = nullptr;   // 矢じりのマテリアル (非所有)
    };
} // namespace NS::Game::Level
