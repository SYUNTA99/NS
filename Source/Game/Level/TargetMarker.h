#pragma once

#include "Game/Level/CollisionInput.h"
#include "Game/Level/ImpactResolver.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/OverlayRenderer.h"
#include "Runtime/Object/Reflection/Curve.h"
#include "Runtime/Object/Reflection/ObjectRef.h"

#include <vector>

namespace NS::Game::Player
{
    class PlayerComponent;
}

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

    //! ロックオンの枠と、突進の道筋の点を組んだ四角の並び。相手のいない線では枠が空
    struct TargetMarkerShape
    {
        //! ロックオンの枠
        LockOnFrameShape frame;
        //! 道筋の点。自機の側から線の先へ並ぶ
        std::vector<MarkerRect> dots;
    };

    //! ロックオンの枠と道筋の点の形を決める値。画素の欄は描画先の高さ 720 のときの画素
    struct TargetMarkerDesc
    {
        NS::Core::Vector3 color{1.0f, 0.85f, 0.2f}; //!< 捉えている間の枠と道筋の点の色。RGB で各 0〜1
        float lineThickness = 3.0f;                 //!< 枠の線の太さ。画素
        float armRatio = 0.25f;                     //!< 枠の腕の長さ。枠の一辺に対する割合
        float dotSpacing = 0.5f;                    //!< 道筋の点の間隔。単位は m
        float dotSize = 6.0f;                       //!< 道筋の点の一辺。画素
        float frameGap = 6.0f;                      //!< 枠と相手の輪郭の間。画素
        float frameMinSide = 70.0f;                 //!< 枠の一辺の下限。画素
        float frameAlpha = 0.9f;                    //!< 捉えている間の枠の不透明度
        float appearScale = 5.0f;                   //!< 捉えた瞬間の枠の一辺の、捉えている間の一辺に対する倍率
        float appearMaxSide = 465.0f;               //!< 捉えた瞬間の枠の一辺の上限。画素
        int appearFrames = 6;                       //!< 捉えた瞬間の大きさから捉えている間の大きさへ縮むフレーム数
        //! 縮みの進み。横は捉えてからのフレーム数 ÷ appearFrames、縦は 0 が捉えた瞬間の形で 1 が捉えている間の形
        NS::Obj::Curve appearCurve{.keys = {NS::Obj::Curve::Key{0.0f, 0.0f},
                                            NS::Obj::Curve::Key{0.25f, 0.12f},
                                            NS::Obj::Curve::Key{0.5f, 0.38f},
                                            NS::Obj::Curve::Key{0.75f, 0.70f},
                                            NS::Obj::Curve::Key{1.0f, 1.0f}},
                                   .count = 5};
        NS::Core::Vector3 appearColor{1.0f, 1.0f, 1.0f};    //!< 捉えた瞬間の枠の色。RGB で各 0〜1
        float appearAlpha = 0.35f;                          //!< 捉えた瞬間の枠の不透明度
        float lostScale = 0.9f;                             //!< 外れた後の枠の一辺の、直前の一辺に対する倍率
        int lostFrames = 2;                                 //!< 外れた後に枠を出すフレーム数
        NS::Core::Vector3 outlineColor{0.1f, 0.08f, 0.02f}; //!< 暗い縁の色。RGB で各 0〜1
        float outlineAlpha = 0.6f;                          //!< 枠の不透明度が frameAlpha のときの暗い縁の不透明度
    };

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

    //! @brief 突進の線の予測から、当たる相手のロックオンの枠と道筋の点の四角を組む
    //! @details 枠は target.bounds から BuildLockOnFrame で組む (外れていない枠)。
    //! 道筋の点は target.origin から target.direction の線の上に置き、線に沿った距離 target.along の所を終わりにして、
    //! desc.dotSpacing ずつ手前へ pathStart まで並べる。線の上で相手の中心に一番近い所より先には置かない。
    //! カメラの後ろの点は組まない。画素の大きさの欄には描画先の高さ ÷ 720 を掛ける。
    //! 相手のいない線は BuildAimPathShape が組む
    //! @param[in] viewProjection 画面へ投げる行列。行ベクトルに右から掛ける
    //! @param[in] targetSize 描画先の幅と高さ。単位は画素
    //! @param[in] target 突進の線の予測
    //! @param[in] pathStart 道筋の点を置き始める、線に沿った距離。単位は m
    //! @param[in] framesSinceCapture 狙う相手なしからありへ移ったフレームを 0 として数えたフレーム数
    //! @param[in] desc 枠と点の形を決める値
    //! @param[out] outShape 組んだ四角の並び。false の場合は書き換えない
    //! @return 組めた場合 true。desc の値が壊れている場合、描画先の大きさが 0 以下の場合、
    //! 線に沿った距離か pathStart が有限でない場合と、点が 1024 を超える場合は false
    [[nodiscard]] bool BuildTargetMarkerShape(const NS::Core::Matrix& viewProjection,
                                              NS::Core::Size2D targetSize,
                                              const SlamLineTarget& target,
                                              float pathStart,
                                              int framesSinceCapture,
                                              const TargetMarkerDesc& desc,
                                              TargetMarkerShape& outShape);

    //! @brief 狙う相手のいない狙いの線から、突進の道筋の点の四角を組む。枠は組まない
    //! @details 道筋の点は line.origin から line.direction の線の上に置き、
    //! 突進が止まる所 (線に沿った距離 line.length) を終わりにして、desc.dotSpacing ずつ手前へ pathStart まで並べる。
    //! 点の置き方・大きさ・カメラの後ろの扱いは BuildTargetMarkerShape の点と同じ
    //! @param[in] viewProjection 画面へ投げる行列。行ベクトルに右から掛ける
    //! @param[in] targetSize 描画先の幅と高さ。単位は画素
    //! @param[in] line 狙いの線
    //! @param[in] pathStart 道筋の点を置き始める、線に沿った距離。単位は m
    //! @param[in] desc 点の形を決める値。枠の欄も BuildTargetMarkerShape と同じく確かめる
    //! @param[out] outShape 組んだ四角の並び。枠は空。false の場合は書き換えない
    //! @return 組めた場合 true。desc の値が壊れている場合、描画先の大きさが 0 以下の場合、
    //! 線の長さか pathStart が有限でない場合と、点が 1024 を超える場合は false
    [[nodiscard]] bool BuildAimPathShape(const NS::Core::Matrix& viewProjection,
                                         NS::Core::Size2D targetSize,
                                         const AimLine& line,
                                         float pathStart,
                                         const TargetMarkerDesc& desc,
                                         TargetMarkerShape& outShape);

    //! @brief 溜めている間、突進の道筋の点と、狙う相手のロックオンの枠を画面へ重ねて描く Component
    //! @details 溜めている間 (CollisionInput::IsCharging) だけ、同じ配置物の CollisionInput が控えた狙いの線に
    //! 道筋の点を出す。線の上に狙う相手がいれば点は相手の中心の真横で終わり、相手に枠を付ける。
    //! いなければ点は突進が止まる所まで並ぶ。溜めている間に相手が外れたら、直前の枠を縮めて欄のフレーム数だけ出す。
    //! 示す物と、捉えてから・外れてからのフレーム数は OnUpdate で決めて控え、描く時はそれを投げるだけ。
    //! 溜め量・威力・質量は形に入れない
    //! 依存: CollisionInput (AimLine), ImpactResolver (SlamLineTarget), NS::Game::Player::PlayerComponent,
    //! NS::Gfx::Renderer
    class TargetMarker : public NS::Obj::OverlayRenderer
    {
    public:
        TargetMarker() noexcept;

        //! 重ね描きの登録簿へ入り、同じ配置物の CollisionInput と PlayerComponent を引き当てる。
        //! CollisionInput が無ければ以後何も示さない
        void OnStart() override;

        //! 溜めている間は狙いの線と、狙う相手が居ればその予測を控え、捉えてから・外れてからのフレーム数を数える。
        //! 溜めていないフレームは控えを全部消す
        void OnUpdate() override;

        //! 控えた道筋の点とロックオンの枠を描く。狙いの線を控えていないフレームは何も描かない
        void OnRenderOverlay(const NS::Gfx::RenderContext& context) override;

        //! このフレームに示す相手。示さないフレームと、外れた後に枠だけを出すフレームは未設定の参照
        [[nodiscard]] NS::Obj::ObjectRef ShownTargetRef() const noexcept;

        //! @brief 控えた道筋の点とロックオンの枠を、欄の値で組む
        //! @details 示す相手が居れば BuildTargetMarkerShape、居なければ控えた狙いの線を BuildAimPathShape で組み、
        //! 外れた後のフレームなら外れた相手の枠を BuildLockOnFrame で足す
        //! @param[in] viewProjection 画面へ投げる行列
        //! @param[in] targetSize 描画先の幅と高さ。単位は画素
        //! @param[out] outShape 組んだ四角の並び。false の場合は書き換えない
        //! @return 狙いの線を控えていて組めた場合 true、それ以外の場合は false
        [[nodiscard]] bool BuildShownShape(const NS::Core::Matrix& viewProjection,
                                           NS::Core::Size2D targetSize,
                                           TargetMarkerShape& outShape) const;

        NS_REFLECT_BEGIN(TargetMarker, NS::Obj::OverlayRenderer)
        NS_REFLECT_FIELD(m_desc.color, "印の色")
        NS_REFLECT_FIELD(m_desc.lineThickness, "印の太さ")
        NS_REFLECT_FIELD(m_desc.armRatio, "印の腕の割合")
        NS_REFLECT_FIELD(m_desc.dotSpacing, "道筋の点の間隔")
        NS_REFLECT_FIELD(m_desc.dotSize, "道筋の点の大きさ")
        NS_REFLECT_FIELD(m_desc.frameGap, "枠と輪郭の間")
        NS_REFLECT_FIELD(m_desc.frameMinSide, "枠の一辺の下限")
        NS_REFLECT_FIELD(m_desc.frameAlpha, "枠の不透明度")
        NS_REFLECT_FIELD(m_desc.appearScale, "枠が出る時の倍率")
        NS_REFLECT_FIELD(m_desc.appearMaxSide, "枠が出る時の一辺の上限")
        NS_REFLECT_FIELD(m_desc.appearFrames, "枠が縮むフレーム数")
        NS_REFLECT_FIELD(m_desc.appearCurve, "枠が縮む進みの曲線")
        NS_REFLECT_FIELD(m_desc.appearColor, "枠が出る時の色")
        NS_REFLECT_FIELD(m_desc.appearAlpha, "枠が出る時の不透明度")
        NS_REFLECT_FIELD(m_desc.lostScale, "外れた時の枠の倍率")
        NS_REFLECT_FIELD(m_desc.lostFrames, "外れた時の枠のフレーム数")
        NS_REFLECT_FIELD(m_desc.outlineColor, "枠の縁の色")
        NS_REFLECT_FIELD(m_desc.outlineAlpha, "枠の縁の不透明度")
        NS_REFLECT_END()

    private:
        TargetMarkerDesc m_desc{};
        SlamLineTarget m_shown{}; // 示す相手の予測。m_hasShown が偽の間は読まない
        bool m_hasShown = false;
        int m_framesSinceCapture = 0;  // 外れた後は外れたフレームの値で止める
        NS::Core::AABB m_lostBounds{}; // 外れた相手の外接箱。m_framesSinceLost が負の間は読まない
        int m_framesSinceLost = -1;
        AimLine m_line{}; // 点を並べる狙いの線。m_hasLine が偽の間は読まない
        bool m_hasLine = false;
        float m_pathStart = 0.0f; // 道筋の点を置き始める、線に沿った距離 (m)。自機の玉の半径
        const CollisionInput* m_input = nullptr;
        const NS::Game::Player::PlayerComponent* m_movement = nullptr;
    };
} // namespace NS::Game::Level
