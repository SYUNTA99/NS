#include "NSlib/Object/Scene/PadRumbleDirector.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Object/ActorList.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/UpdatePhase.h"
#include "NSlib/Windows/Input.h"

namespace NS::Obj
{
    PadRumbleDirector::PadRumbleDirector(Scene& scene) : m_scene(scene)
    {
        m_scene.Objects().AddTicker(this, UpdatePhase::RenderPrep);
    }

    PadRumbleDirector::~PadRumbleDirector() noexcept
    {
        m_scene.Objects().RemoveTicker(this);
        // 振動の途中で場面を捨てても、パッドを震わせたままにしない
        if (m_running)
        {
            (void)NS::OS::Input::Get().Gamepad().SetVibration(0.0f, 0.0f);
        }
    }

    void PadRumbleDirector::Start(const Actor& requester, const HitPadVibration& pad)
    {
        m_layers.clear();
        m_layers.push_back(Layer{.pad = pad, .startElapsed = 0, .requester = &requester});
        m_elapsed = 0;
        m_running = true;
        m_justStarted = true;
        Write();
    }

    void PadRumbleDirector::Blend(const Actor& requester, const HitPadVibration& pad)
    {
        if (!m_running)
        {
            Start(requester, pad);
            return;
        }
        m_layers.push_back(Layer{.pad = pad, .startElapsed = m_elapsed, .requester = &requester});
        Write();
    }

    void PadRumbleDirector::Stop(const Actor& requester)
    {
        std::erase_if(m_layers, [&requester](const Layer& layer) { return layer.requester == &requester; });
        if (!m_layers.empty())
        {
            Write();
            return;
        }
        // 振動の無い状態を書くと 0 が入り、止めたフレームの値が残らない
        m_elapsed = 0;
        m_running = false;
        m_justStarted = false;
        Write();
    }

    // 書かれなかったフレームは Gamepad::Update が 0 にするので、振動の間は毎フレーム書く
    void PadRumbleDirector::OnTick()
    {
        if (m_justStarted)
        {
            m_justStarted = false;
        }
        else if (m_running)
        {
            ++m_elapsed;
            Write();
        }
    }

    void PadRumbleDirector::Write()
    {
        NS::OS::GamepadVibration speed{};
        bool anyRunning = false;
        for (const Layer& layer : m_layers)
        {
            const int elapsed = m_elapsed - layer.startElapsed;
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
            m_running = false;
        }
        // 曲線が範囲の外へ出て書けなかったら、以後は書かない
        if (!NS::OS::Input::Get().Gamepad().SetVibration(speed.left, speed.right))
        {
            NS_LOG_WARN(
                Scene, "パッドの振動の速さが 0〜1 の外で、震わせなかった: 左 {} 右 {}", speed.left, speed.right);
            m_running = false;
        }
    }
} // namespace NS::Obj
