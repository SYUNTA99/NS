#include "Runtime/Object/SkyboxSubsystem.h"

#include "Runtime/Core/Filesystem.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/Camera.h"
#include "Runtime/Graphics/Skybox.h"
#include "Runtime/Object/Scene/Scene.h"

namespace NS::Object
{
    SkyboxSubsystem::SkyboxSubsystem() = default;
    SkyboxSubsystem::~SkyboxSubsystem() = default;

    void SkyboxSubsystem::Initialize(Scene&) noexcept
    {
        // 装置だけ先に作る。cubemap のパスは呼出時に受け取り、DrawSky の差分再読込が初回から入れる
        auto skybox = NS::Graphics::Skybox::Create();
        if (skybox && skybox->IsValid())
        {
            m_skybox = std::move(skybox);
        }
        else
        {
            NS_LOG_WARN(Graphics, "SkyboxSubsystem: Skybox 構築失敗のため空を描かない");
        }
    }

    void SkyboxSubsystem::Deinitialize() noexcept
    {
        m_skybox.reset();
        m_loadedSkyboxPath.clear();
    }

    void SkyboxSubsystem::DrawSky(NS::Graphics::Renderer& renderer,
                                  const NS::Graphics::Camera& camera,
                                  const std::filesystem::path& cubemapPath) noexcept
    {
        if (!m_skybox || !m_skybox->IsValid())
            return;

        // 毎フレーム LoadCubemap すると I/O が常時走るため、前回パスと差分があるときだけ再ロードする
        if (!cubemapPath.empty() && cubemapPath != m_loadedSkyboxPath)
        {
            // ユーザー編集ファイル由来のパスを ContentRoot 配下へ閉じ込める。外を指す値は読み込まない
            const auto absPath =
                ::NS::Core::FileSystem::ResolveUnder(::NS::Core::FileSystem::ContentRoot(), cubemapPath);
            if (!absPath.has_value())
            {
                NS_LOG_WARN(Graphics,
                            "SkyboxSubsystem: cubemap パス '{}' は ContentRoot 配下でないため読み込まない",
                            cubemapPath.string());
                // 拒否はパスを直すまで変わらないので、覚えて警告の連打を止める
                m_loadedSkyboxPath = cubemapPath;
            }
            else if (m_skybox->LoadCubemap(*absPath))
            {
                m_loadedSkyboxPath = cubemapPath;
            }
            else
            {
                NS_LOG_WARN(Graphics, "SkyboxSubsystem: cubemap 読込失敗 ({}), 既存を維持", absPath->string());
                // 失敗時は前回パスを更新しないので次フレームで再試行できる
            }
        }

        // view の平行移動成分を 0 化して camera 中心に空を固定する
        NS::Core::Matrix viewNoTranslate = camera.View();
        viewNoTranslate._41 = 0.0f;
        viewNoTranslate._42 = 0.0f;
        viewNoTranslate._43 = 0.0f;
        NS::Graphics::IssueSkybox(renderer, *m_skybox, viewNoTranslate * camera.Projection());
    }
} // namespace NS::Object
