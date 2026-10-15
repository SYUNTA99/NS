#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Reflection/Curve.h"
#include "NSlib/CoreObject/SubObjects/OverlayRenderer.h"
#include "NSlib/CoreObject/System/SceneSystem.h"

#include <array>

namespace NS
{
    class SceneActor;
    class Scene;

    //! 対象の輪郭の左右へ出す震えの線。寸法は高さ 720 基準の画素
    struct HitShakeLinesDesc
    {
        NS::Vector3 center{};       //!< 線で挟む物の中心。世界の点
        float radius = 0.0f;        //!< 線で挟む物の半径。世界の長さ。0 以下なら出さない
        NS::Vector3 otherCenter{};  //!< 一緒に挟むもう 1 つの物の中心。世界の点
        float otherRadius = 0.0f;   //!< 一緒に挟むもう 1 つの物の半径。0 以下なら 1 つだけを挟む
        int frames = 0;             //!< 出すフレーム数。0 以下なら出さない
        int flipFrames = 1;         //!< 外と内へずらし直すフレーム数。1 未満は 1
        float lengthPixels = 80.0f; //!< 内側の線の長さの画素。外の線ほど短い
        float widthPixels = 7.0f;   //!< 線の太さの画素
        float gapPixels = 16.0f;    //!< 輪郭から内側の線までの間の画素。線どうしの間もこれから決める
    };

    //! @brief 歪みの輪 1 回。長さの欄はどれも画面の高さに対する割合
    struct HitDistortionRingDesc
    {
        NS::Vector3 center{};   //!< 輪の中心。世界の点
        Curve radius{};         //!< 輪の半径。始めたフレームを 0 にしたフレーム数で引く
        Curve push{};           //!< 輪の真ん中で絵を押し出す長さ。0 以下のフレームは歪めない
        float halfWidth = 0.0f; //!< 輪の半分の幅
        int frames = 0;         //!< 出すフレーム数。0 以下なら出さない
    };

    //! @brief 線で挟む物が画面の上で占める横の幅と、縦の中心。座標は描画先の画素で左上が原点
    struct HitShakeLineSpan
    {
        float left = 0.0f;    //!< 左の輪郭の画素
        float right = 0.0f;   //!< 右の輪郭の画素
        float centerY = 0.0f; //!< 縦の中心の画素
    };

    //! @brief 画面の矩形 1 枚。座標は描画先の画素で左上が原点
    struct HitShakeLineRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    //! 対象の画面幅 span の外へ、左 3 本・右 3 本の順で矩形を返す
    //! frame は開始を 0 とした歩数、pixelScale は描画先の高さ ÷ 720
    [[nodiscard]] std::array<HitShakeLineRect, 6> ShakeLineRects(const HitShakeLinesDesc& desc,
                                                                 int frame,
                                                                 const HitShakeLineSpan& span,
                                                                 float pixelScale) noexcept;

    //! シーン共通の画面演出。開始した歩は初期状態を描き、次の更新から世界の速さで進める
    class HitScreenDirector final : public SceneSystem, public IOverlay
    {
    public:
        explicit HitScreenDirector(Scene& scene);
        ~HitScreenDirector() noexcept override;

        //! @brief 白の光を始める。前の白が残っていても始め直す
        //! @param[in] requester 頼んだ物。Stop で同じ物を渡すと消える
        //! @param[in] frames 白の光のフレーム数。0 以下なら光らない
        //! @param[in] alpha 始めの濃さ。残りのフレーム数に比例して薄くなる
        void StartFlash(const SceneActor& requester, int frames, float alpha) noexcept;

        //! @brief 震えの線を始める。前の線が残っていても始め直す
        //! @param[in] requester 頼んだ物。Stop で同じ物を渡すと消える
        //! @param[in] desc 震えの線の設定。半径かフレーム数が 0 以下なら出さない
        void StartShakeLines(const SceneActor& requester, const HitShakeLinesDesc& desc) noexcept;

        //! requester が頼んだ白と震えの線を消す。ほかの物が頼んだ物は残す
        void Stop(const SceneActor& requester) noexcept;

        //! @brief 歪みの輪を始め、始まりの姿を場面へ書く。前の輪が残っていても始め直す
        //! @param[in] requester 頼んだ物。StopDistortionRing で同じ物を渡すと消える
        //! @param[in] desc 歪みの輪の設定。フレーム数が 0 以下なら出さない
        void StartDistortionRing(const SceneActor& requester, const HitDistortionRingDesc& desc) noexcept;

        //! requester が頼んだ歪みの輪を消す。ほかの物が頼んだ輪と、白と震えの線は残す
        void StopDistortionRing(const SceneActor& requester) noexcept;

        //! 白の残りフレーム数。出していない場合 0
        [[nodiscard]] int FlashFramesRemaining() const noexcept { return m_flashRemaining; }
        //! 今の白の濃さ。始めの濃さ × 残りのフレーム数 ÷ 始めのフレーム数
        [[nodiscard]] float FlashAlpha() const noexcept;
        //! 震えの線の残りフレーム数。出していない場合 0
        [[nodiscard]] int ShakeLinesFramesRemaining() const noexcept { return m_linesRemaining; }
        //! 最後に始めた震えの線の設定
        [[nodiscard]] const HitShakeLinesDesc& ShakeLines() const noexcept { return m_lines; }

        //! 白を 1 フレーム薄め、震えの線と歪みの輪を 1 フレーム進める
        void OnTick() override;

        [[nodiscard]] int OverlayOrder() const noexcept override { return 1; }
        //! 残りのフレーム数に比例して薄くなる白を画面全体へ重ね、震えの線を描く
        void OnRenderOverlay(const NS::Gfx::RenderContext& context) override;

    private:
        // 震えの線を挟む物の中心と半径から投げて描く。どれかがカメラの後ろにある時は描かない
        void RenderShakeLines(const NS::Gfx::RenderContext& context) const noexcept;
        void WriteDistortionRing() noexcept;
        void EndDistortionRing() noexcept;

        Scene& m_scene;
        // 頼んだ物は見分けるだけで、指す先は読まない
        const SceneActor* m_flashRequester = nullptr;
        int m_flashRemaining = 0;
        int m_flashFrames = 0; // 薄める割合の分母
        float m_flashAlpha = 0.0f;
        bool m_flashJustStarted = false;
        const SceneActor* m_linesRequester = nullptr;
        HitShakeLinesDesc m_lines{};
        int m_linesRemaining = 0;
        bool m_linesJustStarted = false;
        const SceneActor* m_ringRequester = nullptr;
        HitDistortionRingDesc m_ring{};
        int m_ringElapsed = 0;
        bool m_ringActive = false;
        bool m_ringJustStarted = false;
    };
} // namespace NS
