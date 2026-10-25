#pragma once

#include "NSlib/Core/NonCopyable.h"

#include <string_view>
#include <string>
#include <vector>
#include <map>
#include <memory>

class LevelEditorController;

namespace NS
{
    namespace Gfx
    {
        class D3DRenderTarget;
        class D3DTexture;
    }
    namespace Editor
    {
        class AssetsPanel : public NonCopyable
        {
        public:
            AssetsPanel();
            ~AssetsPanel();
            void Render(LevelEditorController& editor) noexcept;

        private:
            //! 開いたフォルダの中身を辿り、素材の選択とドラッグを受け付ける
            void RenderTree(std::string_view dir, LevelEditorController& editor) noexcept;
            void RenderFiles(const std::vector<std::string>& files, LevelEditorController& editor);
            void RenderTile(const std::string& file, LevelEditorController& editor, float width);
            void RenderSceneOpen(LevelEditorController& editor);
            [[nodiscard]] bool OpenPendingScene(LevelEditorController& editor);
            const Gfx::D3DTexture* LoadThumbnail(const std::string& file);

            std::map<std::string, std::unique_ptr<Gfx::D3DRenderTarget>> m_previews;
            std::string m_pendingSceneName;
            std::string m_sceneOpenError;
        };
    } // namespace Editor
} // namespace NS
