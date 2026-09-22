#include "Runtime/Object/Components/SkeletalAnimation.h"
#include "Runtime/Core/AABB.h"

#include "Runtime/Platform/Clock.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/Buffer.h"
#include "Runtime/Object/AssetManager.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

namespace NS::Obj
{
    namespace
    {
        // セミコロン区切りの参照文字列を trim 済みエントリへ分解する。空エントリは捨てる
        [[nodiscard]] std::vector<std::string> SplitClipRefs(const std::string& refs)
        {
            std::vector<std::string> entries;
            std::size_t begin = 0;
            while (begin <= refs.size())
            {
                std::size_t end = refs.find(';', begin);
                if (end == std::string::npos)
                {
                    end = refs.size();
                }
                std::string_view entry(refs.data() + begin, end - begin);
                while (!entry.empty() && std::isspace(static_cast<unsigned char>(entry.front())) != 0)
                {
                    entry.remove_prefix(1);
                }
                while (!entry.empty() && std::isspace(static_cast<unsigned char>(entry.back())) != 0)
                {
                    entry.remove_suffix(1);
                }
                if (!entry.empty())
                {
                    entries.emplace_back(entry);
                }
                begin = end + 1;
            }
            return entries;
        }
    } // namespace

    SkeletalAnimation::SkeletalAnimation() noexcept
        // 移動が終わった後に骨を追従させる
        : Component(TickPriority::Update + 100)
    {}

    SkeletalAnimation::~SkeletalAnimation() = default;

    void SkeletalAnimation::SetMesh(NS::Gfx::SkeletalMesh* mesh) noexcept
    {
        m_mesh = mesh;
    }

    void SkeletalAnimation::SetSkeleton(const NS::Gfx::Skeleton* skeleton) noexcept
    {
        m_skeleton = skeleton;
    }

    void SkeletalAnimation::Play() noexcept
    {
        m_playing = true;
    }

    void SkeletalAnimation::Pause() noexcept
    {
        m_playing = false;
    }

    void SkeletalAnimation::Stop() noexcept
    {
        m_playing = false;
        m_time = 0.0f;
    }

    void SkeletalAnimation::SetSpeed(float speed) noexcept
    {
        // 正の無限大は speed > 0.0f を通り、m_time の折り返しで std::fmod が非数を返す。非数は大小比較が
        // 全て偽になるので以後の補正に引っかからず、速度を戻しても再生時刻が戻らない
        // 非有限値の扱いは SetMaxSpeedScale と同じ。0 に落とすと再生が止まる
        if (!std::isfinite(speed))
        {
            return;
        }
        if (speed > 0.0f)
        {
            m_speed = speed;
        }
        else
        {
            m_speed = 0.0f;
        }
    }

    void SkeletalAnimation::SetLooping(bool looping) noexcept
    {
        m_looping = looping;
    }

    bool SkeletalAnimation::SelectClip(std::size_t index) noexcept
    {
        if (index >= m_clips.size())
        {
            return false;
        }
        m_current = index;
        m_time = 0.0f;
        return true;
    }

    bool SkeletalAnimation::SelectClip(std::string_view name) noexcept
    {
        for (std::size_t i = 0; i < m_clips.size(); ++i)
        {
            if (m_clips[i] != nullptr && m_clips[i]->name == name)
            {
                return SelectClip(i);
            }
        }
        return false;
    }

    void SkeletalAnimation::AddClips(std::span<const NS::Gfx::AnimationClip> clips)
    {
        m_clips.reserve(m_clips.size() + clips.size());
        for (const NS::Gfx::AnimationClip& clip : clips)
        {
            m_clips.push_back(&clip);
        }
    }

    std::size_t SkeletalAnimation::ClipCount() const noexcept
    {
        return m_clips.size();
    }

    std::size_t SkeletalAnimation::CurrentClip() const noexcept
    {
        return m_current;
    }

    float SkeletalAnimation::Time() const noexcept
    {
        return m_time;
    }

    float SkeletalAnimation::Duration() const noexcept
    {
        if (m_current < m_clips.size() && m_clips[m_current] != nullptr)
        {
            return m_clips[m_current]->duration;
        }
        return 0.0f;
    }

    bool SkeletalAnimation::IsPlaying() const noexcept
    {
        return m_playing;
    }

    void SkeletalAnimation::ResolveAssets(AssetManager& assets)
    {
        if (m_modelRef.empty())
        {
            // 結合先の骨格が無いので Clips だけあっても解決できない
            if (!m_clipsRef.empty())
            {
                NS_LOG_WARN(Graphics, "SkeletalAnimation: model 参照が空のまま Clips を結合しようとしている");
            }
            return;
        }

        const std::optional<std::string> resolved = ResolveContentPath(m_modelRef);
        if (!resolved)
        {
            NS_LOG_WARN(Graphics, "SkeletalAnimation: model 参照を解決できない: {}", m_modelRef);
            return;
        }

        LoadedSkinnedModel loaded = assets.GetOrLoadSkinnedModel(*resolved);
        if (!loaded.valid)
        {
            // 読込失敗は GetOrLoadSkinnedModel が path 込みで報告済み
            return;
        }

        SetSkeleton(loaded.skeleton);
        if (loaded.clips != nullptr)
        {
            AddClips(std::span(*loaded.clips));
        }

        SetMesh(loaded.mesh);

        // 参照解決は component の並び順で回るので、priority 200 の MeshRenderer は 300 のここより先に解決済み
        // ここで差し替えないと skinned mesh が見た目に反映されない
        if (GameObject* owner = Owner())
        {
            if (MeshRenderer* renderer = owner->FindComponent<MeshRenderer>())
            {
                renderer->SetMesh(loaded.mesh);
            }
        }

        // Clips 欄のエントリを骨名で model の骨格へ結合して足す。失敗エントリは skip して残りを続ける
        if (loaded.skeleton == nullptr)
        {
            return;
        }
        for (const std::string& entry : SplitClipRefs(m_clipsRef))
        {
            const std::optional<std::string> resolvedClip = ResolveContentPath(entry);
            if (!resolvedClip)
            {
                NS_LOG_WARN(Graphics, "SkeletalAnimation: clip 参照を解決できない: {}", entry);
                continue;
            }
            const std::vector<NS::Gfx::AnimationClip>* bound =
                assets.GetOrLoadBoundClips(*resolvedClip, *resolved);
            if (bound == nullptr)
            {
                // 失敗はキャッシュ側が path 込みで報告済み
                continue;
            }
            AddClips(std::span(*bound));
        }
    }

    void SkeletalAnimation::OnStart()
    {
        NS::Gfx::BufferDesc cbDesc = NS::Gfx::MakeConstantBufferDesc(sizeof(NS::Gfx::BonePaletteCB));
        m_bonePaletteCB = NS::Gfx::Buffer::Create(cbDesc);
        for (std::size_t i = 0; i < NS::Gfx::k_MaxBones; ++i)
        {
            m_palette.bones[i] = NS::Core::Matrix::Identity;
        }

        // 描画する MeshRenderer にパレットを差す。現在ポーズ境界は ApplyPose が毎フレーム差す
        if (GameObject* owner = Owner())
        {
            m_renderer = owner->FindComponent<MeshRenderer>();
        }

        if (m_renderer != nullptr && m_bonePaletteCB->IsValid())
        {
            m_renderer->SetPerObjectVsConstant(m_bonePaletteCB.get(),
                                               &m_palette,
                                               sizeof(NS::Gfx::BonePaletteCB),
                                               NS::Gfx::k_BonePaletteSlot);
        }
        ApplyPose(m_time);
    }

    void SkeletalAnimation::OnUpdate()
    {
        const float duration = Duration();
        if (m_playing && duration > 0.0f)
        {
            m_time += NS::Platform::FrameTimer::FixedDelta() * m_speed;
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

    void SkeletalAnimation::ApplyPose(float time)
    {
        if (m_mesh == nullptr)
        {
            return;
        }
        if (m_skeleton == nullptr)
        {
            // 骨格が届くまでパレットは恒等のまま
            return;
        }
        if (m_current < m_clips.size() && m_clips[m_current] != nullptr && m_clips[m_current]->IsValid())
        {
            NS::Gfx::SampleClipPose(*m_clips[m_current], *m_skeleton, time, m_poseScratch);
            m_skeleton->ComputePalette(m_poseScratch, m_paletteScratch);
        }
        else
        {
            m_skeleton->ComputeBindPalette(m_paletteScratch);
        }

        // CPU 側パレットへ写す。余りは恒等で埋める。GPU への upload は描画側が毎描画行う
        const std::size_t count = std::min(m_paletteScratch.size(), NS::Gfx::k_MaxBones);
        for (std::size_t i = 0; i < NS::Gfx::k_MaxBones; ++i)
        {
            if (i < count)
            {
                m_palette.bones[i] = m_paletteScratch[i];
            }
            else
            {
                m_palette.bones[i] = NS::Core::Matrix::Identity;
            }
        }

        // 現在ポーズの締まった境界を描画側へ差し、カリングをポーズ追従させる
        if (m_renderer != nullptr)
        {
            const NS::Core::AABB localBounds = NS::Gfx::MergeSkinnedBounds(
                m_mesh->BoneSpheres(), m_palette.bones, m_mesh->BoneCount(), m_mesh->LocalBounds());
            m_renderer->SetLocalBoundsOverride(localBounds);
        }
    }

    // mesh / skeleton / clips は asset 由来なので data からは空で作る。mesh 未注入の間 ApplyPose は何もしない
    NS_CLASS(SkeletalAnimation)
} // namespace NS::Obj
