#include "Game/Level/ImpactTremor.h"

#include <algorithm>
#include <cmath>

namespace NS::Game::Level
{
    namespace
    {
        // 画素の欄の基準の画面の高さ。Replay の撮る画面と、震えを選んだ見本の大きさに揃える
        constexpr float k_ReferenceScreenHeight = 720.0f;
    } // namespace

    float ScreenPixelsToMeters(float pixels, const NS::Obj::CameraPose& pose, const NS::Core::Vector3& at) noexcept
    {
        NS::Core::Vector3 forward = pose.target - pose.position;
        if (!(forward.Length() > NS::Core::k_Epsilon))
        {
            return 0.0f;
        }
        forward.Normalize();
        float depth = NS::Core::Dot(at - pose.position, forward);
        if (!(depth > 0.0f))
        {
            depth = (at - pose.position).Length();
        }
        return pixels * 2.0f * depth * std::tan(pose.fovY.value * 0.5f) / k_ReferenceScreenHeight;
    }

    NS::Gfx::TremorCB MakeTremor(const TackleTremorDesc& desc,
                                 int elapsedFrames,
                                 const NS::Core::Vector3& root,
                                 float bodyLength,
                                 const NS::Obj::CameraPose& pose) noexcept
    {
        NS::Gfx::TremorCB tremor{};
        const int ringFrames = desc.length - std::max(desc.reachFrames, 0);
        if (elapsedFrames < 0 || elapsedFrames >= desc.length || ringFrames <= 0 ||
            !std::isfinite(desc.amplitudePixels))
        {
            return tremor;
        }
        NS::Core::Vector3 forward = pose.target - pose.position;
        if (!(forward.Length() > NS::Core::k_Epsilon))
        {
            return tremor;
        }
        forward.Normalize();
        // 画面の右と上。真上か真下を見ている時は世界の X を右にする
        NS::Core::Vector3 right = NS::Core::Cross(NS::Core::Vector3{0.0f, 1.0f, 0.0f}, forward);
        if (right.Length() > NS::Core::k_Epsilon)
        {
            right.Normalize();
        }
        else
        {
            right = NS::Core::Vector3{1.0f, 0.0f, 0.0f};
        }
        NS::Core::Vector3 up = NS::Core::Cross(forward, right);
        up.Normalize();

        tremor.contactOffset = desc.contactOffset;
        tremor.amplitude = std::max(ScreenPixelsToMeters(desc.amplitudePixels, pose, root), 0.0f);
        tremor.right = right;
        tremor.up = up;
        tremor.elapsedFrames = static_cast<float>(elapsedFrames);
        if (bodyLength > 0.0f && desc.reachFrames > 0)
        {
            tremor.framesPerMeter = static_cast<float>(desc.reachFrames) / bodyLength;
        }
        tremor.ringFrames = static_cast<float>(ringFrames);
        return tremor;
    }
} // namespace NS::Game::Level
