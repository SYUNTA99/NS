#include "NSlib/CoreObject/System/PadRumbleDirector.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/UpdatePhase.h"
#include "NSlib/Core/Windows/Input.h"

#include <algorithm>

namespace NS
{
    PadRumbleDirector::PadRumbleDirector(Scene& scene)
    {
        scene.AddSystemTick(*this, UpdatePhase::RenderPrep);
    }

    PadRumbleDirector::~PadRumbleDirector() noexcept
    {
        // 振動の途中で場面を捨てても、パッドを震わせたままにしない
        if (m_running)
        {
            (void)OS::Input::Get().Gamepad().SetVibration(0.0f, 0.0f);
        }
    }

    void PadRumbleDirector::Start(const SceneActor* requester, const HitPadVibration& pad)
    {
        NS_ASSERT(Scene, requester != nullptr, "PadRumbleDirector::Start: 頼んだ物が null");
        m_layers.clear();
        m_layers.push_back(Layer{.pad = pad, .startElapsed = 0, .requester = requester});
        m_elapsed = 0;
        m_running = true;
        m_justStarted = true;
        Write();
    }

    void PadRumbleDirector::Blend(const SceneActor* requester, const HitPadVibration& pad)
    {
        NS_ASSERT(Scene, requester != nullptr, "PadRumbleDirector::Blend: 頼んだ物が null");
        if (!m_running)
        {
            Start(requester, pad);
            return;
        }
        m_layers.push_back(Layer{.pad = pad, .startElapsed = m_elapsed, .requester = requester});
        Write();
    }

    void PadRumbleDirector::Stop(const SceneActor* requester)
    {
        NS_ASSERT(Scene, requester != nullptr, "PadRumbleDirector::Stop: 頼んだ物が null");
        std::erase_if(m_layers, [requester](const Layer& layer) { return layer.requester == requester; });
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
        OS::GamepadVibration speed{};
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
        // 重ねた和と、キーの値を行き過ぎた曲線は 1 を超えるので、モーターの範囲 0〜1 に収める
        speed.left = std::clamp(speed.left, 0.0f, 1.0f);
        speed.right = std::clamp(speed.right, 0.0f, 1.0f);
        // 非数は clamp を通り抜けて書けないので、以後は書かない
        if (!OS::Input::Get().Gamepad().SetVibration(speed.left, speed.right))
        {
            NS_LOG_WARN(
                Scene, "パッドの振動の速さが非数で、震わせなかった: 左 {} 右 {}", speed.left, speed.right);
            m_running = false;
        }
    }
} // namespace NS
