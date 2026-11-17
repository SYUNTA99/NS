#pragma once

#include "NSlib/Core/NonCopyable.h"

#include <string_view>
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace NS
{
    namespace Gfx
    {
        class D3DRenderTarget;
        class D3DTexture;
    }
    namespace Ed
    {
        class EditorController;

        class AssetsPanel : public NonCopyable
        {
        public:
            AssetsPanel();
            ~AssetsPanel();
            void Render(EditorController& editor) noexcept;

        private:
            //! 開いたフォルダの中身を辿り、素材の選択とドラッグを受け付ける
            void RenderTree(std::string_view dir, EditorController& editor) noexcept;
            void RenderFiles(const std::vector<std::string>& files, EditorController& editor);
            void RenderTile(const std::string& file, EditorController& editor, float width);
            void RenderSceneOpen(EditorController& editor);
            void RenderCreateMenu(std::string_view directory, EditorController& editor, bool onFolder);
            void RenderSceneCreate(EditorController& editor);
            [[nodiscard]] bool OpenPendingScene(EditorController& editor);
            const Gfx::D3DTexture* LoadThumbnail(const std::string& file);
            const Gfx::D3DTexture* LoadActorThumbnail(const std::string& file);

            struct ActorThumbnail;
            std::map<std::string, std::unique_ptr<ActorThumbnail>> m_actorThumbnails;
            std::map<std::string, std::unique_ptr<Gfx::D3DRenderTarget>> m_previews;
            std::string m_pendingSceneName;
            std::string m_sceneOpenError;
            std::string m_sceneCreateDirectory;
            std::string m_newSceneName;
            std::string m_sceneCreateError;
            bool m_sceneCreateRequested = false;
            bool m_revealSceneFolder = false;
        };
    } // namespace Ed
} // namespace NS
