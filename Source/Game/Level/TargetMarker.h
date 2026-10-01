#pragma once

#include "Game/Level/CollisionInput.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Player/PlayerVisualParams.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/OverlayRenderer.h"
#include "Runtime/Object/Reflection/ActorRef.h"
#include "Runtime/Object/Reflection/Curve.h"

#include <vector>

namespace NS::Game::Level
{
    //! 画面へ重ねる四角 1 枚。座標は描画先の画素で、左上が原点
    struct MarkerRect
    {
        float x = 0.0f;      //!< 左端の横の位置。単位は画素
        float y = 0.0f;      //!< 上端の縦の位置。単位は画素
        float width = 0.0f;  //!< 幅。単位は画素
        float height = 0.0f; //!< 高さ。単位は画素
    };

    //! ロックオンの枠の出方を決めるフレーム数。フレームは固定ステップで数える
    struct LockOnFrames
    {
        int sinceCapture = 0; //!< 狙う相手なしからありへ移ったフレームを 0 として数えたフレーム数
        int sinceLost = -1;   //!< 狙う相手ありからなしへ移ったフレームを 0 として数えたフレーム数。外れていなければ負
    };

    //! ロックオンの枠。明るい線の四角と、その下に先に描く暗い縁の四角
    struct LockOnFrameShape
    {
        //! 明るい線。左上・右上・左下・右下の隅の順に、隅ごとに横の 1 本と縦の 1 本
        std::vector<MarkerRect> corners;
        //! 暗い縁。corners と同じ並びで、各四角を両側 1 画素ずつ広げた物
        std::vector<MarkerRect> outline;
        NS::Core::Color color{};        //!< 明るい線の色と不透明度
        NS::Core::Color outlineColor{}; //!< 暗い縁の色と不透明度
    };

    //! ロックオンの枠の形を決める値。画素の欄は描画先の高さ 720 のときの画素

    //! @brief 相手の外接箱と、捉えてから・外れてからのフレーム数から、ロックオンの枠の四角を組む
    //! @details 外接箱の中心を viewProjection で画面へ投げ、外接箱の半分の長さの最大を半径として、
    //! その奥行きでの画素の半径を出す。捉えている間の一辺は、2 × (画素の半径 + desc.frameGap) と
    //! desc.frameMinSide の大きい方。捉えた瞬間の一辺はその desc.appearScale 倍を desc.appearMaxSide で抑えた大きさで、
    //! 捉えている間の一辺より小さくはしない。desc.appearFrames フレームかけて、desc.appearCurve の進みで
    //! 捉えている間の一辺へ縮む。色と不透明度も同じ進みで、desc.appearColor と desc.appearAlpha から
    //! desc.color と desc.frameAlpha へ寄せる。frames.sinceLost が 0 以上なら、frames.sinceCapture の時の枠の一辺に
    //! desc.lostScale を掛ける。frames.sinceLost が desc.lostFrames 以上なら枠は空。
    //! 中心がカメラの後ろ (投げた w が 0 以下) の場合も枠は空。
    //! 画素の欄には描画先の高さ ÷ 720 を掛ける
    //! @param[in] viewProjection 画面へ投げる行列。行ベクトルに右から掛ける
    //! @param[in] targetSize 描画先の幅と高さ。単位は画素
    //! @param[in] bounds 相手の外接箱
    //! @param[in] frames 捉えてから・外れてからのフレーム数
    //! @param[in] desc 枠の形を決める値
    //! @param[out] outFrame 組んだ枠。false の場合は書き換えない
    //! @return 組めた場合 true。desc の 0 以下にできない欄が 0 以下の場合、有限でない値か負のフレーム数がある場合と、
    //! 描画先の大きさが 0 以下の場合は false
    [[nodiscard]] bool BuildLockOnFrame(const NS::Core::Matrix& viewProjection,
                                        NS::Core::Size2D targetSize,
                                        const NS::Core::AABB& bounds,
                                        LockOnFrames frames,
                                        const TargetMarkerDesc& desc,
                                        LockOnFrameShape& outFrame);

    //! @brief 溜めている間、狙う相手のロックオンの枠を画面へ重ねて描く Component
    //! @details 溜めている間 (CollisionInput::IsCharging) だけ、同じ配置物の CollisionInput が控えた
    //! 狙う相手に枠を付ける。
    //! 溜めている間に相手が外れたら、直前の枠を縮めて欄のフレーム数だけ出す。
    //! 示す物と、捉えてから・外れてからのフレーム数は OnUpdate で決めて控え、描く時はそれを投げるだけ。
    //! 溜め量・威力・質量は形に入れない。突進の道筋は SlamArrow が地面に描く
    //! 依存: CollisionInput, ImpactResolver (SlamLineTarget), NS::Gfx::Renderer
    class TargetMarker : public NS::Obj::OverlayRenderer
    {
    public:
        TargetMarker() noexcept;

        //! 重ね描きの登録簿へ入り、同じ配置物の CollisionInput を引き当てる。
        //! CollisionInput が無ければ以後何も示さない
        void OnStart() override;

        //! 溜めている間は狙う相手が居ればその予測を控え、捉えてから・外れてからのフレーム数を数える。
        //! 溜めていないフレームは控えを全部消す
        void OnUpdate() override;

        //! 控えたロックオンの枠を描く。示す相手も外れた後の枠も無いフレームは何も描かない
        void OnRenderOverlay(const NS::Gfx::RenderContext& context) override;

        //! このフレームに示す相手。示さないフレームと、外れた後に枠だけを出すフレームは未設定の参照
        [[nodiscard]] NS::Obj::ActorRef ShownTargetRef() const noexcept;

        //! @brief 控えたロックオンの枠を、欄の値で組む
        //! @details 示す相手が居ればその枠を、外れた後のフレームなら外れた相手の枠を BuildLockOnFrame で組む
        //! @param[in] viewProjection 画面へ投げる行列
        //! @param[in] targetSize 描画先の幅と高さ。単位は画素
        //! @param[out] outFrame 組んだ枠。false の場合は書き換えない
        //! @return 示す相手か外れた後の枠があって組めた場合 true、それ以外の場合は false
        [[nodiscard]] bool BuildShownShape(const NS::Core::Matrix& viewProjection,
                                           NS::Core::Size2D targetSize,
                                           LockOnFrameShape& outFrame) const;

        NS_REFLECT_NONE(TargetMarker, NS::Obj::OverlayRenderer)

    private:
        [[nodiscard]] const TargetMarkerDesc& Tuning() const noexcept;
        SlamLineTarget m_shown{}; // 示す相手の予測。m_hasShown が偽の間は読まない
        bool m_hasShown = false;
        int m_framesSinceCapture = 0;  // 外れた後は外れたフレームの値で止める
        NS::Core::AABB m_lostBounds{}; // 外れた相手の外接箱。m_framesSinceLost が負の間は読まない
        int m_framesSinceLost = -1;
        const CollisionInput* m_input = nullptr;
    };
} // namespace NS::Game::Level
