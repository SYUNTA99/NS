#pragma once

#include "Runtime/Core/NonCopyable.h"

#include <filesystem>

class LevelEditorController;

namespace NS::Editor
{
    //! @brief Assets ツリーを出し、 .mat の適用と .gltf / .glb のドラッグ配置を仲介するパネル
    class AssetsPanel : public NS::Core::NonCopyable
    {
    public:
        void Render(LevelEditorController& editor) noexcept;

    private:
        //! dir 以下を再帰で辿って中身を出す。 .mat はクリック適用 / ドラッグ、 メッシュはドラッグ配置
        void RenderTree(const std::filesystem::path& dir, LevelEditorController& editor) noexcept;
    };
} // namespace NS::Editor
