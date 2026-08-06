#pragma once

#include "Runtime/Core/NonCopyable.h"

#include <filesystem>
#include <memory>

namespace NS::Graphics
{
    class Camera;
    class Renderer;
    class Skybox;
} // namespace NS::Graphics

namespace NS::Object
{
    class Scene;

    /// @brief skybox 装置を所有するシーン Subsystem
    /// @details 装置は Initialize で生成し、device 不在なら持たないまま各操作が何もしない
    /// 照明のシーン上書きは Scene が配置された平行光から直接組むので、ここは空の描画だけを担う
    /// Scene が直接所有する
    /// 依存: NS::Graphics::Skybox
    class SkyboxSubsystem final : public NS::Core::NonCopyable
    {
    public:
        SkyboxSubsystem();
        ~SkyboxSubsystem();

        /// skybox 装置を生成する。device 不在なら装置を持たず DrawSky は何もしない
        void Initialize(Scene& scene) noexcept;

        /// skybox 装置と読込済みパスの控えごと破棄する
        void Deinitialize() noexcept;

        /// 渡されたパスが前回と違えば cubemap を読み直し、camera 中心固定で空を描く
        /// パスは ContentRoot 配下の相対のみ許可。空パス・装置未初期化・読込失敗は既存表示を維持する
        void DrawSky(NS::Graphics::Renderer& renderer,
                     const NS::Graphics::Camera& camera,
                     const std::filesystem::path& cubemapPath) noexcept;

    private:
        std::unique_ptr<NS::Graphics::Skybox> m_skybox; // 空描画装置、 device 不在なら null
        /// 差分フレームのみ cubemap を再ロードするため前回パスを保持する
        std::filesystem::path m_loadedSkyboxPath{};
    };
} // namespace NS::Object
