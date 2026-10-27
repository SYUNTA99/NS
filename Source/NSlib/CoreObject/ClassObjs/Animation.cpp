#include "NSlib/CoreObject/ClassObjs/Animation.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Animation/Retarget.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/Asset/SkinnedModel.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Gfx/D3DBuffer.h"

#include <algorithm>
#include <cmath>

namespace NS
{
    Animation::Animation()
    {
        LoadAssetField(m_model, "");
    }

    Animation::~Animation() = default;

    void Animation::SetMesh(Gfx::D3DSkeletalMesh* mesh) noexcept { m_mesh = mesh; }

    void Animation::SetSkeleton(const Anim::Skeleton* skeleton) noexcept { m_skeleton = skeleton; }

    void Animation::Play() noexcept { m_playing = true; }

    void Animation::Play(const Anim::AnimClip* clip)
    {
        if (clip == m_clip)
        {
            return;
        }
        m_clip = clip;
        m_time = 0.0f;
        FindClipBones();
    }

    void Animation::Pause() noexcept { m_playing = false; }

    void Animation::Stop() noexcept
    {
        m_playing = false;
        m_time = 0.0f;
    }

    void Animation::SetSpeed(float speed) noexcept
    {
        // 非有限値で再生時刻が壊れると速度を戻しても復帰できないため、設定を拒否する。
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

    void Animation::SetLooping(bool looping) noexcept { m_looping = looping; }

    float Animation::Time() const noexcept { return m_time; }

    float Animation::Duration() const noexcept
    {
        if (m_clip != nullptr)
        {
            return m_clip->duration;
        }
        return 0.0f;
    }

    bool Animation::IsPlaying() const noexcept { return m_playing; }

    void Animation::FindClipBones()
    {
        m_clipBones.clear();
        if (m_clip != nullptr && m_skeleton != nullptr)
        {
            m_clipBones = Anim::FindTrackBones(*m_clip, *m_skeleton);
        }
    }

    void Animation::UseModel(Model* renderer)
    {
        NS_ASSERT(Scene, renderer != nullptr, "Animation::UseModel: メッシュを差す Model が空");
        // 欄を書き換えるたびに読み直すので、前のモデルの骨格とメッシュを残さない
        SetSkeleton(nullptr);
        SetMesh(nullptr);
        Mesh* mesh = m_model->GetMesh();
        if (mesh != nullptr)
        {
            SetSkeleton(m_model->GetSkeleton());
            SetMesh(mesh->D3DSkeletal());

            // Model のメッシュの欄は空なので、ここで差さないと骨付きのメッシュが見た目に出ない
            renderer->SetMesh(mesh);
        }
        FindClipBones();
    }

    void Animation::BindToModel(Model* renderer)
    {
        NS_ASSERT(Scene, renderer != nullptr, "Animation::BindToModel: パレットを差す Model が空");
        Gfx::D3DBufferDesc cbDesc = Gfx::MakeConstantBufferDesc(sizeof(Gfx::BonePaletteCB));
        m_bonePaletteCB = Gfx::D3DBuffer::Create(cbDesc);
        for (std::size_t i = 0; i < k_MaxBones; ++i)
        {
            m_palette.bones[i] = Matrix::Identity;
        }

        // 描画する Model にパレットを差す。現在ポーズ境界は ApplyPose が毎フレーム差す
        if (m_bonePaletteCB->IsValid())
        {
            renderer->SetPerObjectVsConstant(
                m_bonePaletteCB.get(), &m_palette, sizeof(Gfx::BonePaletteCB), Gfx::k_BonePaletteSlot);
        }
        ApplyPose(renderer, m_time);
    }

    void Animation::Update(Model* renderer, float dt)
    {
        NS_ASSERT(Scene, renderer != nullptr, "Animation::Update: 姿勢を書く Model が空");
        const float duration = Duration();
        if (m_playing && duration > 0.0f)
        {
            m_time += dt * m_speed;
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
        ApplyPose(renderer, m_time);
    }

    void Animation::ApplyPose(Model* renderer, float time)
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
        if (m_clip != nullptr && m_clip->IsValid())
        {
            Anim::SampleClipPose(*m_clip, m_clipBones, *m_skeleton, time, m_poseScratch);
            m_skeleton->ComputePalette(m_poseScratch, m_paletteScratch);
        }
        else
        {
            m_skeleton->ComputeBindPalette(m_paletteScratch);
        }

        // CPU 側パレットへ写す。余りは恒等で埋める。GPU への upload は描画側が毎描画行う
        const std::size_t count = std::min(m_paletteScratch.size(), k_MaxBones);
        for (std::size_t i = 0; i < k_MaxBones; ++i)
        {
            if (i < count)
            {
                m_palette.bones[i] = m_paletteScratch[i];
            }
            else
            {
                m_palette.bones[i] = Matrix::Identity;
            }
        }

        // 現在ポーズの締まった境界を描画側へ差し、カリングをポーズ追従させる
        const AABB localBounds = Gfx::MergeSkinnedBounds(
            m_mesh->BoneSpheres(), m_palette.bones, m_mesh->BoneCount(), m_mesh->LocalBounds());
        renderer->SetLocalBoundsOverride(localBounds);
    }
} // namespace NS
