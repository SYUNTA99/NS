#pragma once

#include <string>

namespace NS
{
    namespace Editor
    {
        //! 起動シーンのレベルの名前。Game::StartSceneName を Scenes/ 配下の形へ直した物
        [[nodiscard]] std::string StartLevelName() noexcept;

        //! レベル保存用ディレクトリが存在することを確認し、なければ作成する
        //! @return 作成に失敗した場合は false を返す
        [[nodiscard]] bool EnsureScenesDirectoryExists() noexcept;
    } // namespace Editor
} // namespace NS
