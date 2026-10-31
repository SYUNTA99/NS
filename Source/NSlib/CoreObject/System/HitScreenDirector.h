#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/OverlayRenderer.h"
#include "NSlib/CoreObject/Reflection/Curve.h"
#include "NSlib/CoreObject/System/SceneSystem.h"


namespace NS
{
    class SceneActor;
    class Scene;

    //! 歪みの輪 1 回。長さの欄はどれも画面の高さに対する割合
    struct HitDistortionRingDesc
    {
        Vector3 center{};   // 輪の中心。世界の点
        Curve radius{};         // 輪の半径。始めたフレームを 0 にしたフレーム数で引く
        Curve push{};           // 輪の真ん中で絵を押し出す長さ。0 以下のフレームは歪めない
        float halfWidth = 0.0f; // 輪の半分の幅
        int frames = 0;         // 出すフレーム数。0 以下なら出さない
    };

    //! 床の波 1 回。center の真下から、上を向いた面へ光の輪を広げる
    struct HitGroundWaveDesc
    {
        Vector3 center{};   // 輪の中心の真上の世界の点。水平の位置だけを使う
        Curve radius{};     // 輪の半径 (m)。始めたフレームを 0 にしたフレーム数で引く
        Curve strength{};   // 輪の強さ。0 以下のフレームは出さない
        int frames = 0;     // 出すフレーム数。0 以下なら出さない
    };

    //! シーン共通の画面演出。開始した歩は初期状態を描き、次の更新から世界の速さで進める
    class HitScreenDirector final : public SceneSystem, public IOverlay
    {
    public:
        explicit HitScreenDirector(Scene& scene);
        ~HitScreenDirector() noexcept override;

        //! 白の光を始める。前の白が残っていても始め直す
        //! @param[in] requester 頼んだ物。Stop で同じ物を渡すと消える
        //! @param[in] frames 白の光のフレーム数。0 以下なら光らない
        //! @param[in] alpha 始めの濃さ。残りのフレーム数に比例して薄くなる
        void StartFlash(const SceneActor* requester, int frames, float alpha) noexcept;

        //! requester が頼んだ白を消す。ほかの物が頼んだ白は残す
        void Stop(const SceneActor* requester) noexcept;

        //! @brief requester が頼んで出している白の濃さ・歪みの輪の押し出し・床の波の強さに scale を掛ける
        //! @details 始め直さず、残りをその強さで続ける
        void Scale(const SceneActor* requester, float scale) noexcept;

        //! 歪みの輪を始め、始まりの姿を場面へ書く。前の輪が残っていても始め直す
        //! @param[in] requester 頼んだ物。StopDistortionRing で同じ物を渡すと消える
        //! @param[in] desc 歪みの輪の設定。フレーム数が 0 以下なら出さない
        void StartDistortionRing(const SceneActor* requester, const HitDistortionRingDesc& desc) noexcept;

        //! requester が頼んだ歪みの輪を消す。ほかの物が頼んだ輪と白は残す
        void StopDistortionRing(const SceneActor* requester) noexcept;

        //! 床の波を始め、始まりの姿を場面へ書く。前の波が残っていても始め直す
        //! @param[in] requester 頼んだ物。StopGroundWave で同じ物を渡すと消える
        //! @param[in] desc 床の波の設定。フレーム数が 0 以下なら出さない
        void StartGroundWave(const SceneActor* requester, const HitGroundWaveDesc& desc) noexcept;

        //! requester が頼んだ床の波を消す。ほかの物が頼んだ波は残す
        void StopGroundWave(const SceneActor* requester) noexcept;

        //! 白の残りフレーム数。出していない場合 0
        [[nodiscard]] int FlashFramesRemaining() const noexcept { return m_flashRemaining; }
        //! 今の白の濃さ。始めの濃さ × 残りのフレーム数 ÷ 始めのフレーム数
        [[nodiscard]] float FlashAlpha() const noexcept;
        void OnTick() override;

        [[nodiscard]] int OverlayOrder() const noexcept override { return 1; }
        //! 残りのフレーム数に比例して薄くなる白を画面全体へ重ねる
        void OnRenderOverlay(const Gfx::RenderContext& context) override;

    private:
        void WriteDistortionRing() noexcept;
        void EndDistortionRing() noexcept;
        void WriteGroundWave() noexcept;
        void EndGroundWave() noexcept;

        Scene& m_scene;
        // 頼んだ物は見分けるだけで、指す先は読まない
        const SceneActor* m_flashRequester = nullptr;
        int m_flashRemaining = 0;
        int m_flashFrames = 0; // 薄める割合の分母
        float m_flashAlpha = 0.0f;
        bool m_flashJustStarted = false;
        const SceneActor* m_ringRequester = nullptr;
        HitDistortionRingDesc m_ring{};
        int m_ringElapsed = 0;
        bool m_ringActive = false;
        bool m_ringJustStarted = false;
        const SceneActor* m_waveRequester = nullptr;
        HitGroundWaveDesc m_wave{};
        int m_waveElapsed = 0;
        bool m_waveActive = false;
        bool m_waveJustStarted = false;
    };
} // namespace NS
