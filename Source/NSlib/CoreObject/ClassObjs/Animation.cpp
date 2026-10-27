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

    void Animation::SetSkeleton(const Anim::Skeleton* skeleton) noexcept
    {
        m_skeleton = skeleton;
        // 作った姿勢と保存した姿勢は前の骨格の骨の並びなので、新しい骨格と混ぜない
        m_pose.clear();
        m_blendFrom.clear();
    }

    void Animation::Play() noexcept { m_playing = true; }

    void Animation::Play(const Anim::AnimClip* clip, float blendSeconds)
    {
        if (clip == m_clip)
        {
            return;
        }
        // 補間の途中なら m_pose は混ぜた後の姿勢なので、保存し直すだけで姿勢が飛ばない。
        // 止めている間は補間が進まないので、混ぜると前の姿勢のまま残る。止めている時は混ぜない
        const bool canBlend = m_playing && m_clip != nullptr && m_clip->IsValid() && m_skeleton != nullptr &&
                              m_pose.size() == m_skeleton->BoneCount() && std::isfinite(blendSeconds) &&
                              blendSeconds > 0.0f;
        if (canBlend)
        {
            m_blendFrom = m_pose;
            m_blendSeconds = blendSeconds;
            m_blendElapsed = 0.0f;
        }
        else
        {
            m_blendFrom.clear();
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
        // 補間が進まなくなるので、混ぜた姿勢のまま止めずに最初の姿勢を出す
        m_blendFrom.clear();
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

        if (m_bonePaletteCB->IsValid())
        {
            renderer->SetPerObjectVsConstant(
                m_bonePaletteCB.get(), &m_palette, sizeof(Gfx::BonePaletteCB), Gfx::k_BonePaletteSlot);
        }
        EvaluatePose(m_time);
        ApplyPose(renderer);
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
        // 補間は一時停止で止まるが、再生速度は掛けない。速度は自機が毎フレーム変えるので、掛けると秒の長さが揺れる。
        // ループしないクリップが補間より先に終わった時は、最後の姿勢へ向けて補間だけを進める
        const bool clipEnded = !m_looping && duration > 0.0f && m_time >= duration;
        if (!m_blendFrom.empty() && (m_playing || clipEnded))
        {
            m_blendElapsed += dt;
            if (m_blendElapsed >= m_blendSeconds)
            {
                m_blendFrom.clear();
            }
        }
        EvaluatePose(m_time);
        ApplyPose(renderer);
    }

    void Animation::EvaluatePose(float time)
    {
        if (m_skeleton == nullptr)
        {
            m_pose.clear();
            return;
        }
        if (m_clip != nullptr && m_clip->IsValid())
        {
            Anim::SampleClipPose(*m_clip, m_clipBones, *m_skeleton, time, m_poseScratch);
        }
        else
        {
            const std::vector<Anim::Bone>& bones = m_skeleton->Bones();
            m_poseScratch.resize(bones.size());
            for (std::size_t i = 0; i < bones.size(); ++i)
            {
                m_poseScratch[i] = bones[i].bindLocal;
            }
        }

        if (m_blendFrom.empty())
        {
            m_pose.swap(m_poseScratch);
            return;
        }
        Anim::BlendPose(m_blendFrom, m_poseScratch, m_blendElapsed / m_blendSeconds, m_pose);
    }

    void Animation::ApplyPose(Model* renderer)
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
        m_skeleton->ComputePalette(m_pose, m_paletteScratch);

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
