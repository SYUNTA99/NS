#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Object/ITickable.h"
#include "NSlib/Object/Scene/SceneObjHolder.h"
#include "NSlib/Object/SubObjects/OverlayRenderer.h"

#include <array>

namespace NS::Obj
{
    class Actor;
    class Scene;

    //! @brief 震えの線 1 回。挟む物の輪郭の外の左右に縦の短い線を 3 本ずつ出し、決めたフレーム数ごとに外と内へずらす
    //! @details 挟む物は 1 つか 2 つ。2 つの時は画面の上で両方を囲む幅の外に出す。画素の欄は描画先の高さが 720 の
    //! ときの大きさで書く
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

    //! @brief 震えの線の矩形を作る。左の 3 本、右の 3 本の順
    //! @param[in] desc 震えの線の設定
    //! @param[in] frame 始めたフレームを 0 にしたフレーム数
    //! @param[in] span 挟む物の画面の上の幅。線はこの外に出す
    //! @param[in] pixelScale 描画先の高さを 720 で割った値。画素の欄に掛ける
    //! @return 6 枚の矩形
    [[nodiscard]] std::array<HitShakeLineRect, 6> ShakeLineRects(const HitShakeLinesDesc& desc,
                                                                 int frame,
                                                                 const HitShakeLineSpan& span,
                                                                 float pixelScale) noexcept;

    //! @brief 当たりの白と震えの線を進めて画面へ重ねる、シーンに 1 つの物
    //! @details 部品の HitReaction が頼む。頼んだフレームは最初の姿を出し、次の更新から進める。
    //! 描く支度の段で進むので、世界の速さに従い、自機以外の止めの間も進む
    class HitScreenDirector final : public ISceneObj, public ITickable, public IOverlay
    {
    public:
        explicit HitScreenDirector(Scene& scene);
        ~HitScreenDirector() noexcept override;

        //! @brief 白の光を始める。前の白が残っていても始め直す
        //! @param[in] requester 頼んだ物。Stop で同じ物を渡すと消える
        //! @param[in] frames 白の光のフレーム数。0 以下なら光らない
        //! @param[in] alpha 始めの濃さ。残りのフレーム数に比例して薄くなる
        void StartFlash(const Actor& requester, int frames, float alpha) noexcept;

        //! @brief 震えの線を始める。前の線が残っていても始め直す
        //! @param[in] requester 頼んだ物。Stop で同じ物を渡すと消える
        //! @param[in] desc 震えの線の設定。半径かフレーム数が 0 以下なら出さない
        void StartShakeLines(const Actor& requester, const HitShakeLinesDesc& desc) noexcept;

        //! requester が頼んだ白と震えの線を消す。ほかの物が頼んだ物は残す
        void Stop(const Actor& requester) noexcept;

        //! 白の残りフレーム数。出していない場合 0
        [[nodiscard]] int FlashFramesRemaining() const noexcept { return m_flashRemaining; }
        //! 今の白の濃さ。始めの濃さ × 残りのフレーム数 ÷ 始めのフレーム数
        [[nodiscard]] float FlashAlpha() const noexcept;
        //! 震えの線の残りフレーム数。出していない場合 0
        [[nodiscard]] int ShakeLinesFramesRemaining() const noexcept { return m_linesRemaining; }
        //! 最後に始めた震えの線の設定
        [[nodiscard]] const HitShakeLinesDesc& ShakeLines() const noexcept { return m_lines; }

        //! 白を 1 フレーム薄め、震えの線を 1 フレーム進める
        void OnTick() override;

        [[nodiscard]] int OverlayOrder() const noexcept override { return 1; }
        //! 残りのフレーム数に比例して薄くなる白を画面全体へ重ね、震えの線を描く
        void OnRenderOverlay(const NS::Gfx::RenderContext& context) override;

    private:
        // 震えの線を挟む物の中心と半径から投げて描く。どれかがカメラの後ろにある時は描かない
        void RenderShakeLines(const NS::Gfx::RenderContext& context) const noexcept;

        Scene& m_scene;
        // 頼んだ物は見分けるだけで、指す先は読まない
        const Actor* m_flashRequester = nullptr;
        int m_flashRemaining = 0;
        int m_flashFrames = 0; // 薄める割合の分母
        float m_flashAlpha = 0.0f;
        bool m_flashJustStarted = false;
        const Actor* m_linesRequester = nullptr;
        HitShakeLinesDesc m_lines{};
        int m_linesRemaining = 0;
        bool m_linesJustStarted = false;
    };
} // namespace NS::Obj
