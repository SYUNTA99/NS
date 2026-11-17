#pragma once

#include "Editor/LevelFileBrowser.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"

#include <functional>
#include <vector>

namespace NS
{
    namespace OS
    {
        class Input;
        class InputPresses;
    } // namespace OS
} // namespace NS
namespace NS
{
    class Scene;
} // namespace NS
namespace NS
{
    namespace Ed
    {
        //! CreateLevel の答え。作れなかった時は理由を返し、作成の窓が理由ごとの文を出す
        enum class CreateLevelResult : uint8
        {
            Created,
            BadPath,       // 名前か保存先のフォルダに使えない文字がある
            AlreadyExists, // 同じ名前のファイルがある。上書きしない
            NotEditable,   // 編集モードでない
            WriteFailed,   // ファイルへ書けなかった
        };

        //! レベルを開く・保存する・アクターを作る・未保存の版を数えるエディタの機能
        class EditorMode : public NonCopyable
        {
        public:
            //! 保存時に実体からシーンの JSON 文書を作る捕捉関数を差す。未設定なら保存できない
            void SetCaptureLevelFn(std::function<nlohmann::json()> fn) noexcept { m_captureLevel = std::move(fn); }

            //! 編集するシーンを設定する。未設定ならアクターを作らない
            void SetScene(Scene* scene) noexcept { m_scene = scene; }

            //! 読込済みシーンデータを実体側へ取り込む関数を差す。読込の完了時に呼ぶ
            void SetLoadLevelFn(std::function<void(nlohmann::json&&)> fn) noexcept { m_loadLevel = std::move(fn); }

            //! 保存のショートカットが Ctrl と Shift を読む入力を設定する
            void SetInput(OS::Input* input) noexcept { m_input = input; }

            //! シーンを変えてよいかを答える関数を差す。LevelEditorController::IsSceneEditable を差す
            //! 偽の間は作る・開く・保存を全部断る。未設定なら変えてよい
            void SetSceneEditableFn(std::function<bool()> fn) noexcept { m_sceneEditable = std::move(fn); }

            //! 保存のショートカットの入力を 1 固定ステップ分進める
            //! @param[in] editShortcutsBlocked ResolveInputOwnership の答え。真の間は Ctrl+S を受けない
            //! @param[in] presses 持ち主がこの固定ステップに読んだ押し
            void Tick(bool editShortcutsBlocked, const OS::InputPresses& presses) noexcept;

            //! 新しいアクターを作ってシーンを未保存にする。作る経路 (置く・複製) は全部ここを通す
            //! 各 JSON に新しい永続 id を振り、写した元の id を指す参照を、一緒に作る物の新しい id へ付け替える。
            //! 作った物を選ぶかは呼び手が決める
            //! @return 作った物の id を渡した順に返す。休止の間 (プレイ中) と、シーンが無い時は作らずに空を返す
            [[nodiscard]] std::vector<uint32> CreateActors(std::vector<nlohmann::json> objects);

            //! シーンを変えた後に 1 回呼び、編集の版を進める。.actor・.mat はすぐファイルへ書くので呼ばない
            void MarkEdited() noexcept { ++m_editVersion; }
            //! シーンの編集の通算回数。保存と読込では戻らない
            [[nodiscard]] uint64 EditVersion() const noexcept { return m_editVersion; }
            //! 最後の保存 / 読込のあとに編集が入ったか
            [[nodiscard]] bool HasUnsavedChanges() const noexcept { return EditVersion() != m_savedEditVersion; }

            //! 今開いているレベル名。まだ名前が決まっていなければ空
            [[nodiscard]] const std::string& CurrentLevelName() const noexcept { return m_currentLevelName; }

            [[nodiscard]] bool IsLevelDirty() const noexcept { return m_levelDirty; }
            void ClearLevelDirty() noexcept { m_levelDirty = false; }

            void HandleSaveLoadInput(const OS::InputPresses& presses) noexcept;

            //! 保存 / 読込のモーダルを描き、確定した操作をその場で実行する
            void RenderFileBrowser() noexcept;

            //! 終了する前に保存する。書き先は QuitSaveLevelName
            [[nodiscard]] bool SaveForQuit() noexcept;

            //! 終了の保存の書き先のレベルの名前。今のレベルの名前、起動レベルが読めなかった時は退避の名前、
            //! どちらも無ければ起動シーンの名前
            [[nodiscard]] std::string QuitSaveLevelName() const noexcept;

            //! レベルを読み、プレイヤーと落下死の範囲が無ければ補ってから実体へ取り込む
            //! 読めたら名前を控え、未保存でない状態にする。起動と「開く」の両方がここを通る
            //! @return 名前が使えない・取り込み関数が無い・読めない時は false で、今のレベルと名前を保つ。
            //! 名前の無いまま実在するファイルを読めなかった時は、終了の保存の書き先を退避の名前にする
            bool OpenLevel(std::string_view name) noexcept;
            //! エディタ固有の作業状態の保存先。空なら記録と復元を行わない
            void SetSessionPath(std::string path) noexcept { m_sessionPath = std::move(path); }
            //! 前回開いたシーンを復元し、記録が無いか開けなければ fallback を開く
            [[nodiscard]] bool OpenStartupLevel(std::string_view fallback) noexcept;
            // 現在の編集内容を保ち、既存ファイルを上書きしない
            [[nodiscard]] CreateLevelResult CreateLevel(std::string_view name) noexcept;

            //! 現在名で上書き保存。名前が無ければ名前を付けて保存を開く
            void RequestSave() noexcept;
            //! 名前を付けて保存のモーダルを開く
            void OpenSaveModal() noexcept { m_fileBrowser.OpenSaveModal(m_currentLevelName); }

        private:
            std::function<nlohmann::json()> m_captureLevel;
            Scene* m_scene = nullptr;
            std::function<void(nlohmann::json&&)> m_loadLevel;
            OS::Input* m_input = nullptr;

            std::function<bool()> m_sceneEditable;
            bool m_levelDirty = true;

            LevelFileBrowser m_fileBrowser{};

            std::string m_currentLevelName;
            std::string m_sessionPath;
            bool m_bootLevelLoadFailed = false; // 起動レベルが実在かつ読込失敗した
            uint64 m_editVersion = 0;      // シーンの編集の通算回数
            uint64 m_savedEditVersion = 0; // 最後に保存 / 読込した時点の編集の版

            std::string m_statusMessage;
            bool m_statusError = false;
            float m_statusTimer = 0.0f;

            //! 差した関数の答え。シーンを変える関数は全部、頭でこれを見る
            [[nodiscard]] bool IsSceneEditable() const noexcept;

            [[nodiscard]] bool SaveLevelToName(std::string_view name) noexcept;
            void RememberCurrentLevel() const noexcept;
            void OverwriteCurrentLevel() noexcept;
        };
    } // namespace Ed
} // namespace NS
