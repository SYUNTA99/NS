#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/Reflection/Curve.h"

namespace NS
{
    class SceneActor;
    class SceneRenderer;
} // namespace NS

namespace GL
{
    namespace Level
    {
        //! 歪みの輪 1 回。半径・押し出し・半分の幅は画面の高さに対する割合
        struct HitDistortionRingDesc
        {
            NS::Vector3 center{};   // 輪の中心。世界の点
            NS::Curve radius{};     // 輪の半径。始めたフレームを 0 にしたフレーム数で引く
            NS::Curve push{};       // 輪の真ん中で絵を押し出す長さ。0 以下のフレームは歪めない
            float halfWidth = 0.0f; // 輪の半分の幅
            int frames = 0;         // 出すフレーム数。0 以下なら出さない
        };

        //! 床の波 1 回。center の真下から、上を向いた面へ光の輪を広げる
        struct HitGroundWaveDesc
        {
            NS::Vector3 center{}; // 輪の中心の真上の世界の点。水平の位置だけを使う
            NS::Curve radius{};   // 輪の半径 (m)。始めたフレームを 0 にしたフレーム数で引く
            NS::Curve strength{}; // 輪の強さ。0 以下のフレームは出さない
            int frames = 0;       // 出すフレーム数。0 以下なら出さない
        };

        //! 1 フレームの床の波。SceneRenderer の名前の無いシーンの値に並べ、Shaders/Game/SurfaceHooks.hlsli が読む
        struct GroundWaveFrame
        {
            float centerX = 0.0f;  // 輪の中心のワールド X 座標
            float centerZ = 0.0f;  // 輪の中心のワールド Z 座標
            float radius = 0.0f;   // 半径 (m)
            float strength = 0.0f; // 強さ。0 以下なら出さない
        };

        //! 当たりの画面の演出 (歪みの輪・床の波)。GameScene がシーンに 1 つ持ち、シーンが止まっている歩でも進める
        //! 開始した歩は初期状態を描き、次の更新から進める。描く値は渡した SceneRenderer へ書く
        class HitScreenDirector final : public NS::NonCopyable
        {
        public:
            explicit HitScreenDirector(NS::SceneRenderer& renderer) : m_renderer(renderer) {}

            //! 歪みの輪を始め、始まりの姿を場面へ書く。前の輪が残っていても始め直す
            //! @param[in] requester 頼んだ物。StopDistortionRing で同じ物を渡すと消える
            //! @param[in] desc 歪みの輪の設定。フレーム数が 0 以下なら出さない
            void StartDistortionRing(const NS::SceneActor* requester, const HitDistortionRingDesc& desc) noexcept;

            //! requester が頼んだ歪みの輪を消す。ほかの物が頼んだ輪は残す
            void StopDistortionRing(const NS::SceneActor* requester) noexcept;

            //! 床の波を始め、始まりの姿を場面へ書く。前の波が残っていても始め直す
            //! @param[in] requester 頼んだ物。StopGroundWave で同じ物を渡すと消える
            //! @param[in] desc 床の波の設定。フレーム数が 0 以下なら出さない
            void StartGroundWave(const NS::SceneActor* requester, const HitGroundWaveDesc& desc) noexcept;

            //! requester が頼んだ床の波を消す。ほかの物が頼んだ波は残す
            void StopGroundWave(const NS::SceneActor* requester) noexcept;

            //! @brief requester が頼んで出している歪みの輪の押し出しと床の波の強さに scale を掛ける
            //! @details 始め直さず、残りをその強さで続ける。HitReaction::ScaleRunning は輪と波に触らないので、
            //! 当たりの演出を弱める持ち主が一緒に呼ぶ
            void Scale(const NS::SceneActor* requester, float scale) noexcept;

            //! 頼んだ物に関わらず、歪みの輪と床の波を全部消す。シーンがアクターを組み直す前に呼ぶ
            void Clear() noexcept;

            //! 歪みの輪と床の波を 1 フレーム進める
            void OnTick() noexcept;

            //! renderer の名前の無いシーンの値から、描く床の波を読む。書いていなければ強さ 0
            [[nodiscard]] static GroundWaveFrame GroundWaveShown(const NS::SceneRenderer& renderer) noexcept;

        private:
            void WriteDistortionRing() noexcept;
            void EndDistortionRing() noexcept;
            void WriteGroundWave() noexcept;
            void EndGroundWave() noexcept;

            NS::SceneRenderer& m_renderer;
            // 頼んだ物は見分けるだけで、指す先は読まない
            const NS::SceneActor* m_ringRequester = nullptr;
            HitDistortionRingDesc m_ring{};
            int m_ringElapsed = 0;
            bool m_ringActive = false;
            bool m_ringJustStarted = false;
            const NS::SceneActor* m_waveRequester = nullptr;
            HitGroundWaveDesc m_wave{};
            int m_waveElapsed = 0;
            bool m_waveActive = false;
            bool m_waveJustStarted = false;
        };
    } // namespace Level
} // namespace GL
