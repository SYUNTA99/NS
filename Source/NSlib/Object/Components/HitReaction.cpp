#include "NSlib/Object/Components/HitReaction.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Graphics/RenderContext.h"
#include "NSlib/Graphics/Renderer.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/IUse/IUseCamera.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Windows/Input.h"

#include <limits>
#include <utility>

namespace NS::Obj
{
    namespace
    {
        // 揺れのフレーム数の上限。揺れの並びは始める時に全フレームぶんの領域を取るので、欄の打ち間違いの大きな値を止める
        // 1 秒を超える揺れは当たりの返りではなく、画面が揺れ続けている状態
        constexpr int k_MaxShakeFrames = 60;
        // 震えの線の画素の欄は描画先の高さがこの値のときの大きさで書く
        constexpr float k_ReferenceHeight = 720.0f;
        // 線の暗い縁が白い線からはみ出す片側の幅。描画先の高さ 720 のときの画素。明るい床の上でも白い線を読ませる
        constexpr float k_LineOutlineWidth = 1.5f;

        // 上限を超えた長さは警告して断る。カメラの無い場面は黙って断る
        bool CanStartCameraMotion(const Actor* owner, int frames)
        {
            if (frames > k_MaxShakeFrames)
            {
                NS_LOG_WARN(
                    Scene, "揺れのフレーム数 {} が上限 {} を超えていて、揺らさなかった", frames, k_MaxShakeFrames);
                return false;
            }
            return owner != nullptr && owner->GetCameraManager() != nullptr;
        }
    } // namespace

    std::array<HitShakeLineRect, 6> ShakeLineRects(const HitShakeLinesDesc& desc,
                                                   int frame,
                                                   const HitShakeLineSpan& span,
                                                   float pixelScale) noexcept
    {
        // 漫画の震えの描き文字のように、輪郭の外へ縦の線を並べる。外の線ほど短くし、輪郭から離れる向きを読ませる
        // 入れ替えのフレーム数ごとに全部を外へ間の半分ずらす。左右に振れる体の横揺れと同じ拍で線が動く
        const int flip = std::max(desc.flipFrames, 1);
        const bool outward = (std::max(frame, 0) / flip) % 2 == 1;
        const float gap = desc.gapPixels * pixelScale;
        const float width = desc.widthPixels * pixelScale;
        float shift = 0.0f;
        if (outward)
        {
            shift = gap * 0.5f;
        }
        std::array<HitShakeLineRect, 6> rects{};
        for (int i = 0; i < 3; ++i)
        {
            const float fromOutline = gap + shift + static_cast<float>(i) * (width + gap * 0.6f);
            const float length = desc.lengthPixels * pixelScale * (1.0f - 0.25f * static_cast<float>(i));
            const float top = span.centerY - length * 0.5f;
            rects[static_cast<std::size_t>(i)] = HitShakeLineRect{span.left - fromOutline - width, top, width, length};
            rects[static_cast<std::size_t>(i + 3)] = HitShakeLineRect{span.right + fromOutline, top, width, length};
        }
        return rects;
    }

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
        if (!CanStartCameraMotion(Owner(), desc.frames))
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

    bool HitReaction::StartSink(const CameraSinkDesc& desc)
    {
        if (desc.frames <= 0)
        {
            return true;
        }
        if (!CanStartCameraMotion(Owner(), desc.frames))
        {
            return false;
        }
        if (!AddCameraModifier(*Owner(), CameraSinkModifier::Create(desc)))
        {
            NS_LOG_WARN(Scene,
                        "沈む揺れの設定が壊れていて、揺らさなかった: 深さ {} 震え {} 行き過ぎ {} フレーム数 {}",
                        desc.bottomPixels,
                        desc.tremblePixels,
                        desc.overshootRatio,
                        desc.frames);
            return false;
        }
        return true;
    }

    bool HitReaction::StartNudge(const CameraNudgeDesc& desc)
    {
        if (desc.frames <= 0)
        {
            return true;
        }
        if (!CanStartCameraMotion(Owner(), desc.frames))
        {
            return false;
        }
        if (!AddCameraModifier(*Owner(), CameraNudgeModifier::Create(desc)))
        {
            NS_LOG_WARN(Scene,
                        "ずれの設定が壊れていて、ずらさなかった: 向き ({}, {}, {}) フレーム数 {}",
                        desc.direction.x,
                        desc.direction.y,
                        desc.direction.z,
                        desc.frames);
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

    void HitReaction::StartShakeLines(const HitShakeLinesDesc& desc) noexcept
    {
        if (!(desc.radius > 0.0f) || desc.frames <= 0)
        {
            m_linesRemaining = 0;
            m_linesJustStarted = false;
            return;
        }
        m_lines = desc;
        m_linesRemaining = desc.frames;
        m_linesJustStarted = true;
    }

    void HitReaction::StartPadVibration(const HitPadVibration& pad)
    {
        m_pads.clear();
        m_pads.push_back(PadLayer{.pad = pad, .startElapsed = 0});
        m_padElapsed = 0;
        m_padRunning = true;
        m_padJustStarted = true;
        WritePadVibration();
    }

    void HitReaction::BlendPadVibration(const HitPadVibration& pad)
    {
        if (!m_padRunning)
        {
            StartPadVibration(pad);
            return;
        }
        m_pads.push_back(PadLayer{.pad = pad, .startElapsed = m_padElapsed});
        WritePadVibration();
    }

    void HitReaction::Stop()
    {
        m_flashRemaining = 0;
        m_flashJustStarted = false;
        m_linesRemaining = 0;
        m_linesJustStarted = false;
        m_padJustStarted = false;
        // 振動の無い状態を書くと 0 が入り、止めたフレームの値が残らない
        m_pads.clear();
        m_padElapsed = 0;
        m_padRunning = false;
        WritePadVibration();
        if (Owner() != nullptr)
        {
            StopCameraHitEffects(*Owner());
        }
    }

    bool HitReaction::AddTrauma(const CameraTraumaDesc& desc)
    {
        // カメラの無い場面 (試しの台) では揺らす先が無い。設定の誤りではないので黙って返す
        if (Owner() == nullptr || Owner()->GetCameraManager() == nullptr)
        {
            return false;
        }
        if (!AddCameraTrauma(*Owner(), desc))
        {
            NS_LOG_WARN(Scene, "トラウマの量が壊れていて、揺らさなかった: {}", desc.trauma);
            return false;
        }
        return true;
    }

    // 持ち主の Actor の Update が呼ぶ。自機では ImpactResolver の後に呼ばれ、決めたフレームに最初の姿を出す
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
        if (m_linesJustStarted)
        {
            m_linesJustStarted = false;
        }
        else if (m_linesRemaining > 0)
        {
            --m_linesRemaining;
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
        NS::OS::GamepadVibration speed{};
        bool anyRunning = false;
        for (const PadLayer& layer : m_pads)
        {
            const int elapsed = m_padElapsed - layer.startElapsed;
            if (elapsed < layer.pad.frames)
            {
                speed.left += layer.pad.left.Evaluate(static_cast<float>(elapsed));
                speed.right += layer.pad.right.Evaluate(static_cast<float>(elapsed));
                anyRunning = true;
            }
        }
        if (!anyRunning)
        {
            // 終わりのフレームも 0 を書く。書かないと次の Input::Update までは前の値が読める
            m_padRunning = false;
        }
        // 曲線の途中で範囲の外へ出たら、そのフレームで振動を止める
        if (!NS::OS::Input::Get().Gamepad(0).SetVibration(speed.left, speed.right))
        {
            NS_LOG_WARN(
                Scene, "パッドの振動の速さが 0〜1 の外で、震わせなかった: 左 {} 右 {}", speed.left, speed.right);
            m_padRunning = false;
        }
    }

    void HitReaction::OnRenderOverlay(const NS::Gfx::RenderContext& context)
    {
        if (context.renderer == nullptr)
        {
            return;
        }
        if (m_flashRemaining > 0 && m_flashFrames > 0)
        {
            // 暗転の黒とは別の、瞬間に薄れる白。フレームごとに直線で下げる
            const float decay = static_cast<float>(m_flashRemaining) / static_cast<float>(m_flashFrames);
            context.renderer->DrawFullscreenColor(NS::Color{1.0f, 1.0f, 1.0f, m_flashAlpha * decay});
        }
        RenderShakeLines(context);
    }

    void HitReaction::RenderShakeLines(const NS::Gfx::RenderContext& context) const noexcept
    {
        if (m_linesRemaining <= 0)
        {
            return;
        }
        const NS::Size2D size = context.renderer->Size();
        if (size.width <= 0 || size.height <= 0)
        {
            return;
        }
        const float width = static_cast<float>(size.width);
        const float height = static_cast<float>(size.height);
        // 真後ろのカメラでは自機が相手に重なるので、2 つを囲む幅の外へ出す。縦は 2 つの上端と下端の真ん中
        float left = std::numeric_limits<float>::max();
        float right = std::numeric_limits<float>::lowest();
        float top = std::numeric_limits<float>::max();
        float bottom = std::numeric_limits<float>::lowest();
        const std::array<std::pair<NS::Vector3, float>, 2> bodies{std::pair{m_lines.center, m_lines.radius},
                                                                  std::pair{m_lines.otherCenter, m_lines.otherRadius}};
        for (const std::pair<NS::Vector3, float>& body : bodies)
        {
            if (!(body.second > 0.0f))
            {
                continue;
            }
            NS::Vector2 centerPixel{};
            float centerW = 0.0f;
            if (!NS::Gfx::TryProjectToPixels(context.viewProjection, body.first, width, height, centerPixel, centerW))
            {
                return;
            }
            const float radiusPixels =
                NS::Gfx::ProjectedLengthPixels(context.viewProjection, body.second, centerW, height);
            left = std::min(left, centerPixel.x - radiusPixels);
            right = std::max(right, centerPixel.x + radiusPixels);
            top = std::min(top, centerPixel.y - radiusPixels);
            bottom = std::max(bottom, centerPixel.y + radiusPixels);
        }
        const float pixelScale = height / k_ReferenceHeight;
        const HitShakeLineSpan span{.left = left, .right = right, .centerY = (top + bottom) * 0.5f};
        const std::array<HitShakeLineRect, 6> rects =
            ShakeLineRects(m_lines, m_lines.frames - m_linesRemaining, span, pixelScale);
        // 暗い縁を先に描き、白い線を上に重ねる
        const float outline = k_LineOutlineWidth * pixelScale;
        for (const HitShakeLineRect& rect : rects)
        {
            context.renderer->DrawScreenRect(rect.x - outline,
                                             rect.y - outline,
                                             rect.width + outline * 2.0f,
                                             rect.height + outline * 2.0f,
                                             NS::Color{0.0f, 0.0f, 0.0f, 0.6f});
        }
        for (const HitShakeLineRect& rect : rects)
        {
            context.renderer->DrawScreenRect(
                rect.x, rect.y, rect.width, rect.height, NS::Color{1.0f, 1.0f, 1.0f, 1.0f});
        }
    }

    void HitReaction::OnEndPlay()
    {
        // 基底が重ね描きの登録簿から自分を外す
        OverlayRenderer::OnEndPlay();
        Stop();
        // プレイを終えた後の視点にトラウマを残さない
        if (Owner() != nullptr)
        {
            StopCameraEffects(*Owner());
        }
    }

    NS_CLASS(HitReaction)
} // namespace NS::Obj
