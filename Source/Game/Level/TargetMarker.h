#pragma once

#include "Game/Level/ImpactResolver.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/OverlayRenderer.h"
#include "Runtime/Object/Reflection/ObjectRef.h"

#include <vector>

namespace NS::Game::Player
{
    class PlayerComponent;
}

namespace NS::Game::Level
{
    class CollisionInput;

    //! 画面へ重ねる四角 1 枚。座標は描画先の画素で、左上が原点
    struct MarkerRect
    {
        float x = 0.0f;      //!< 左端の横の位置。単位は画素
        float y = 0.0f;      //!< 上端の縦の位置。単位は画素
        float width = 0.0f;  //!< 幅。単位は画素
        float height = 0.0f; //!< 高さ。単位は画素
    };

    //! 当たる相手の印と、突進の道筋の点を組んだ四角の並び
    struct TargetMarkerShape
    {
        //! 印の四隅のかぎ形。左上・右上・左下・右下の隅の順に、隅ごとに横の 1 本と縦の 1 本
        std::vector<MarkerRect> corners;
        //! 道筋の点。自機の側から相手の側へ並ぶ
        std::vector<MarkerRect> dots;
    };

    //! 印と道筋の点の形を決める値
    struct TargetMarkerDesc
    {
        float lineThickness = 3.0f; //!< 印の太さ。描画先の高さ 720 のときの画素
        float armRatio = 0.25f;     //!< 印の腕の長さ。印の矩形の短い辺に対する割合
        float dotSpacing = 0.5f;    //!< 道筋の点の間隔。単位は m
        float dotSize = 6.0f;       //!< 道筋の点の一辺。描画先の高さ 720 のときの画素
    };

    //! @brief 突進の線の予測から、当たる相手の印と道筋の点の四角を組む
    //! @details 印は target.bounds の 8 つの角を viewProjection で画面へ投げた矩形の四隅に置く。
    //! 角が 1 つでもカメラの後ろ (投げた w が 0 以下) なら印は組まない。
    //! 道筋の点は target.origin から target.direction の線の上に置き、線に沿った距離 target.along の所を終わりにして、
    //! desc.dotSpacing ずつ手前へ pathStart まで並べる。線の上で相手の中心に一番近い所より先には置かない。
    //! カメラの後ろの点は組まない。画素の大きさの欄には描画先の高さ ÷ 720 を掛ける
    //! @param[in] viewProjection 画面へ投げる行列。行ベクトルに右から掛ける
    //! @param[in] targetSize 描画先の幅と高さ。単位は画素
    //! @param[in] target 突進の線の予測
    //! @param[in] pathStart 道筋の点を置き始める、線に沿った距離。単位は m
    //! @param[in] desc 印と点の形を決める値
    //! @param[out] outShape 組んだ四角の並び。false の場合は書き換えない
    //! @return 組めた場合 true。desc の値が 0 以下か有限でない場合、描画先の大きさが 0 以下の場合、
    //! 線に沿った距離か pathStart が有限でない場合と、点が 1024 を超える場合は false
    [[nodiscard]] bool BuildTargetMarkerShape(const NS::Core::Matrix& viewProjection,
                                              NS::Core::Size2D targetSize,
                                              const SlamLineTarget& target,
                                              float pathStart,
                                              const TargetMarkerDesc& desc,
                                              TargetMarkerShape& outShape);

    //! @brief 溜めている間、狙う相手の印と突進の道筋の点を画面へ重ねて描く Component
    //! @details 同じ配置物の CollisionInput が控えた狙う相手を、溜めている間 (CollisionInput::IsCharging) だけ示す。
    //! 示す相手は OnUpdate で決めて控え、描く時はそれを投げるだけ。溜め量・威力・質量は形に入れない
    //! 依存: CollisionInput, ImpactResolver (SlamLineTarget), NS::Game::Player::PlayerComponent, NS::Gfx::Renderer
    class TargetMarker : public NS::Obj::OverlayRenderer
    {
    public:
        TargetMarker() noexcept;

        //! 重ね描きの登録簿へ入り、同じ配置物の CollisionInput と PlayerComponent を引き当てる。
        //! CollisionInput が無ければ以後何も示さない
        void OnStart() override;

        //! 溜めている間に狙う相手が居ればその予測を示す相手として控え、それ以外のフレームは控えを消す
        void OnUpdate() override;

        //! 控えた相手の印と道筋の点を描く。示す相手が無いフレームは何も描かない
        void OnRenderOverlay(const NS::Gfx::RenderContext& context) override;

        //! このフレームに示す相手。示さないフレームは未設定の参照
        [[nodiscard]] NS::Obj::ObjectRef ShownTargetRef() const noexcept;

        //! @brief 控えた相手の印と道筋の点を、欄の値で組む
        //! @param[in] viewProjection 画面へ投げる行列
        //! @param[in] targetSize 描画先の幅と高さ。単位は画素
        //! @param[out] outShape 組んだ四角の並び。false の場合は書き換えない
        //! @return 示す相手が居て組めた場合 true、それ以外の場合は false
        [[nodiscard]] bool BuildShownShape(const NS::Core::Matrix& viewProjection,
                                           NS::Core::Size2D targetSize,
                                           TargetMarkerShape& outShape) const;

        NS_REFLECT_BEGIN(TargetMarker, NS::Obj::OverlayRenderer)
        NS_REFLECT_FIELD(m_color, "印の色")
        NS_REFLECT_FIELD(m_desc.lineThickness, "印の太さ")
        NS_REFLECT_FIELD(m_desc.armRatio, "印の腕の割合")
        NS_REFLECT_FIELD(m_desc.dotSpacing, "道筋の点の間隔")
        NS_REFLECT_FIELD(m_desc.dotSize, "道筋の点の大きさ")
        NS_REFLECT_END()

    private:
        NS::Core::Vector3 m_color{1.0f, 0.85f, 0.2f}; // 印と道筋の点の色。RGB で各 0〜1
        TargetMarkerDesc m_desc{};
        SlamLineTarget m_shown{}; // 示す相手の予測。m_hasShown が偽の間は読まない
        bool m_hasShown = false;
        float m_pathStart = 0.0f; // 道筋の点を置き始める、線に沿った距離 (m)。自機の玉の半径
        const CollisionInput* m_input = nullptr;
        const NS::Game::Player::PlayerComponent* m_movement = nullptr;
    };
} // namespace NS::Game::Level
