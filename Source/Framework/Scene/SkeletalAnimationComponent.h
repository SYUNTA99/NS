#pragma once

/// @file SkeletalAnimationComponent.h
/// @brief NS::Scene::SkeletalAnimationComponent — クリップを時間再生して SkeletalMesh のボーンパレットを更新する
///
/// @details fixed step ごとに再生時刻を進め、 AnimationClip を sampling してポーズを作り、
/// Skeleton でボーンパレット化して SkeletalMesh に渡す。 再生 / 停止 / 速度 / ループ / クリップ選択を制御できる
/// mesh は外部所有 (ctor で注入)、 skeleton と clips は本コンポーネントが持つ。 priority は Animation 帯

#include "Framework/Graphics/Animation.h"
#include "Framework/Graphics/SkeletalMesh.h"
#include "Framework/Graphics/Skeleton.h"
#include "Framework/Math/Math.h"
#include "Framework/Scene/Component.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace NS::Scene
{
    /// クリップを時間再生し SkeletalMesh のボーンパレットを毎ステップ更新する
    /// mesh は非所有 (ctor で注入)、 skeleton / clips は本コンポーネントが所有する
    class SkeletalAnimationComponent : public Component
    {
    public:
        SkeletalAnimationComponent(NS::Graphics::SkeletalMesh* mesh,
                                   NS::Graphics::Skeleton skeleton,
                                   std::vector<NS::Graphics::AnimationClip> clips) noexcept;

        void Play() noexcept;
        void Pause() noexcept;
        void Stop() noexcept;                ///< 再生時刻を 0 に戻して停止する
        void SetSpeed(float speed) noexcept; ///< 負値は 0 にクランプ
        void SetLooping(bool looping) noexcept;
        bool SelectClip(std::size_t index) noexcept;
        bool SelectClip(std::string_view name) noexcept;

        /// クリップを後から追加する (既存の選択・再生位置は維持)。 外部で読んだ別アニメの合体に使う
        void AddClips(std::vector<NS::Graphics::AnimationClip> clips);

        [[nodiscard]] std::size_t ClipCount() const noexcept;
        [[nodiscard]] std::size_t CurrentClip() const noexcept;
        [[nodiscard]] float Time() const noexcept;
        [[nodiscard]] float Duration() const noexcept; ///< 現在クリップの尺 (無ければ 0)
        [[nodiscard]] bool IsPlaying() const noexcept;

        void OnStart() override;
        void OnUpdate() override;

    private:
        void ApplyPose(float time);

        NS::Graphics::SkeletalMesh* m_mesh = nullptr;
        NS::Graphics::Skeleton m_skeleton;
        std::vector<NS::Graphics::AnimationClip> m_clips;
        std::size_t m_current = 0;
        float m_time = 0.0f;
        float m_speed = 1.0f;
        bool m_playing = true;
        bool m_looping = true;
        std::vector<NS::Graphics::BonePose> m_poseScratch;
        std::vector<NS::Math::Matrix> m_paletteScratch;
    };

} // namespace NS::Scene
