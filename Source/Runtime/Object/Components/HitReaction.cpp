#include "Runtime/Object/Components/HitReaction.h"

#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/RenderContext.h"
#include "Runtime/Graphics/Renderer.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/IUse/IUseCamera.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Platform/Input.h"

namespace NS::Obj
{
    namespace
    {
        // 揺れのフレーム数の上限。揺れの並びは始める時に全フレームぶんの領域を取るので、欄の打ち間違いの大きな値を止める
        // 1 秒を超える揺れは当たりの返りではなく、画面が揺れ続けている状態
        constexpr int k_MaxShakeFrames = 60;
    } // namespace

    // 持ち主の Actor の Update が呼ぶ。自機では ImpactResolver の後に呼ばれ、決めたフレームに最初の姿を出す
    HitReaction::HitReaction() noexcept : OverlayRenderer() {}

    void HitReaction::StartFlash(int frames, float alpha) noexcept
    {
        m_flashRemaining = std::max(frames, 0);
        m_flashFrames = m_flashRemaining;
        m_flashAlpha = alpha;
        m_flashJustStarted = true;
    }

    bool HitReaction::StartShake(const CameraShakeDesc& desc)
    {
        if (desc.frames <= 0)
        {
            return true;
        }
        if (desc.frames > k_MaxShakeFrames)
        {
            NS_LOG_WARN(Scene,
                        "揺れのフレーム数 {} が上限 {} を超えていて、揺らさなかった",
                        desc.frames,
                        k_MaxShakeFrames);
            return false;
        }
        // カメラの無い場面 (試しの台) では揺らす先が無い。設定の誤りではないので黙って返す
        if (Owner() == nullptr || Owner()->GetCameraManager() == nullptr)
        {
            return false;
        }
        if (!StartCameraShake(*Owner(), desc))
        {
            NS_LOG_WARN(Scene,
                        "揺れの設定が壊れていて、揺らさなかった: 横 {} 縦 {} フレーム数 {} 最長 {}",
                        desc.sideAmplitude,
                        desc.upAmplitude,
                        desc.frames,
                        desc.longestFlipFrames);
            return false;
        }
        return true;
    }

    bool HitReaction::StartZoomRoll(const CameraZoomRollDesc& desc)
    {
        if (Owner() == nullptr || Owner()->GetCameraManager() == nullptr)
        {
            return false;
        }
        if (!StartCameraZoomRoll(*Owner(), desc))
        {
            NS_LOG_WARN(Scene,
                        "寄りと傾きの設定が壊れていて、寄せなかった: 倍率 {} 傾き {} 保つ {} 戻す {}",
                        desc.zoom,
                        desc.rollDegrees,
                        desc.holdFrames,
                        desc.returnFrames);
            return false;
        }
        return true;
    }

    void HitReaction::StartPadVibration(const HitPadVibration& pad)
    {
        m_pad = pad;
        m_padElapsed = 0;
        m_padRunning = true;
        m_padJustStarted = true;
        WritePadVibration();
    }

    void HitReaction::Stop()
    {
        m_flashRemaining = 0;
        m_flashJustStarted = false;
        m_padJustStarted = false;
        // 書くフレーム数 0 の振動を書くと 0 が入り、止めたフレームの値が残らない
        m_pad = HitPadVibration{};
        m_padElapsed = 0;
        m_padRunning = false;
        WritePadVibration();
        if (Owner() != nullptr)
        {
            StopCameraEffects(*Owner());
        }
    }

    void HitReaction::OnUpdate()
    {
        // 始めたフレームは最初の姿のまま。次の更新から薄め、振動を進める
        if (m_flashJustStarted)
        {
            m_flashJustStarted = false;
        }
        else if (m_flashRemaining > 0)
        {
            --m_flashRemaining;
        }
        // 書かれなかったフレームは Gamepad::Update が 0 にするので、振動の間は毎フレーム書く
        if (m_padJustStarted)
        {
            m_padJustStarted = false;
        }
        else if (m_padRunning)
        {
            ++m_padElapsed;
            WritePadVibration();
        }
    }

    void HitReaction::WritePadVibration()
    {
        NS::Platform::GamepadVibration speed{};
        if (m_padElapsed < m_pad.frames && m_pad.fadeFrames > 0)
        {
            const float fade =
                static_cast<float>(m_pad.fadeFrames - m_padElapsed) / static_cast<float>(m_pad.fadeFrames);
            speed.left = m_pad.start.left * fade;
            speed.right = m_pad.start.right * fade;
        }
        else
        {
            // 終わりのフレームも 0 を書く。書かないと次の Input::Update までは前の値が読める
            m_padRunning = false;
        }
        // 以後のフレームは始めの値から 0 へ減るだけなので、範囲の外になるのは始めの値が外の時だけ
        if (!NS::Platform::Input::Get().Gamepad(0).SetVibration(speed.left, speed.right))
        {
            NS_LOG_WARN(
                Scene, "パッドの振動の速さが 0〜1 の外で、震わせなかった: 左 {} 右 {}", speed.left, speed.right);
            m_padRunning = false;
        }
    }

    void HitReaction::OnRenderOverlay(const NS::Gfx::RenderContext& context)
    {
        if (m_flashRemaining <= 0 || m_flashFrames <= 0)
        {
            return;
        }
        // 暗転の黒とは別の、瞬間に薄れる白。フレームごとに直線で下げる
        const float decay = static_cast<float>(m_flashRemaining) / static_cast<float>(m_flashFrames);
        context.renderer->DrawFullscreenColor(NS::Core::Color{1.0f, 1.0f, 1.0f, m_flashAlpha * decay});
    }

    void HitReaction::OnEndPlay()
    {
        // 基底が重ね描きの登録簿から自分を外す
        OverlayRenderer::OnEndPlay();
        Stop();
    }

    NS_CLASS(HitReaction)
} // namespace NS::Obj
