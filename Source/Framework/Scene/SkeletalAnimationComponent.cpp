#include "Framework/Scene/SkeletalAnimationComponent.h"

#include "Framework/Core/Clock.h"

#include <cmath>
#include <span>
#include <utility>

namespace NS::Scene
{
    SkeletalAnimationComponent::SkeletalAnimationComponent(NS::Graphics::SkeletalMesh* mesh,
                                                           NS::Graphics::Skeleton skeleton,
                                                           std::vector<NS::Graphics::AnimationClip> clips) noexcept
        : Component(static_cast<int>(TickPriority::Animation)), m_mesh(mesh), m_skeleton(std::move(skeleton)),
          m_clips(std::move(clips))
    {}

    void SkeletalAnimationComponent::Play() noexcept
    {
        m_playing = true;
    }

    void SkeletalAnimationComponent::Pause() noexcept
    {
        m_playing = false;
    }

    void SkeletalAnimationComponent::Stop() noexcept
    {
        m_playing = false;
        m_time = 0.0f;
    }

    void SkeletalAnimationComponent::SetSpeed(float speed) noexcept
    {
        m_speed = (speed > 0.0f) ? speed : 0.0f;
    }

    void SkeletalAnimationComponent::SetLooping(bool looping) noexcept
    {
        m_looping = looping;
    }

    bool SkeletalAnimationComponent::SelectClip(std::size_t index) noexcept
    {
        if (index >= m_clips.size())
        {
            return false;
        }
        m_current = index;
        m_time = 0.0f;
        return true;
    }

    bool SkeletalAnimationComponent::SelectClip(std::string_view name) noexcept
    {
        for (std::size_t i = 0; i < m_clips.size(); ++i)
        {
            if (m_clips[i].name == name)
            {
                return SelectClip(i);
            }
        }
        return false;
    }

    std::size_t SkeletalAnimationComponent::ClipCount() const noexcept
    {
        return m_clips.size();
    }

    std::size_t SkeletalAnimationComponent::CurrentClip() const noexcept
    {
        return m_current;
    }

    float SkeletalAnimationComponent::Time() const noexcept
    {
        return m_time;
    }

    float SkeletalAnimationComponent::Duration() const noexcept
    {
        return (m_current < m_clips.size()) ? m_clips[m_current].duration : 0.0f;
    }

    bool SkeletalAnimationComponent::IsPlaying() const noexcept
    {
        return m_playing;
    }

    void SkeletalAnimationComponent::OnStart()
    {
        ApplyPose(m_time);
    }

    void SkeletalAnimationComponent::OnUpdate()
    {
        const float duration = Duration();
        if (m_playing && duration > 0.0f)
        {
            m_time += NS::Core::FrameTimer::FixedDelta() * m_speed;
            if (m_looping)
            {
                m_time = std::fmod(m_time, duration);
                if (m_time < 0.0f)
                {
                    m_time += duration;
                }
            }
            else if (m_time >= duration)
            {
                m_time = duration;
                m_playing = false;
            }
        }
        ApplyPose(m_time);
    }

    void SkeletalAnimationComponent::ApplyPose(float time)
    {
        if (m_mesh == nullptr)
        {
            return;
        }
        if (m_current < m_clips.size() && m_clips[m_current].IsValid())
        {
            NS::Graphics::SampleClipPose(m_clips[m_current], m_skeleton, time, m_poseScratch);
            m_skeleton.ComputePalette(m_poseScratch, m_paletteScratch);
        }
        else
        {
            m_skeleton.ComputeBindPalette(m_paletteScratch);
        }
        m_mesh->SetBonePalette(std::span<const NS::Math::Matrix>(m_paletteScratch.data(), m_paletteScratch.size()));
    }

} // namespace NS::Scene
