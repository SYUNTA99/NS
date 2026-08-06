#pragma once

#include "Editor/CategoryPalette.h"
#include "Editor/GridMath.h"
#include "Editor/LevelFileBrowser.h"
#include "Editor/Undo/UndoStack.h"
#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Object/Scene/SceneData.h"

#include <functional>

namespace NS::Platform
{
    class Input;
}
namespace NS::UI
{
    class ImGuiContext;
}
namespace NS::Object
{
    class CameraComponent;
    struct SceneData;
} // namespace NS::Object
namespace NS::Editor
{
    class IObjectSnapshotApplier;

    //! @brief エディタの編集機能（カーソル、パレット、Undo履歴など）を統括するクラス。
    //! @details シーンに対する編集操作を管理し、非アクティブ時はすべての入力・描画処理をスキップする。
    class EditorMode : public NS::Core::NonCopyable
    {
    public:
        //! @brief 編集中のカーソル状態。レイキャスト結果や配置候補のセル情報を保持する
        struct CursorState
        {
            bool valid = false;                  //!< 有効な対象（ブロック面や地面）にヒットしたかどうか
            NS::Math::Vector3 placementCenter{}; //!< 配置先セルの中心ワールド座標
            NS::Math::Vector3 deleteCenter{};    //!< 削除対象セルの中心ワールド座標
            bool placementBlocked = false;       //!< 配置予定地にすでにブロックが存在するかどうか
            std::int16_t hitX = 0;               //!< ヒットしたセルのX座標
            std::int16_t hitY = 0;               //!< ヒットしたセルのY座標
            std::int16_t hitZ = 0;               //!< ヒットしたセルのZ座標
            std::int16_t placeX = 0;             //!< 配置先セルのX座標
            std::int16_t placeY = 0;             //!< 配置先セルのY座標
            std::int16_t placeZ = 0;             //!< 配置先セルのZ座標
            NS::Math::Vector3 hitNormal{};       //!< ヒットした面の法線ベクトル
        };

        //! @brief cell 1 個分の整数座標。 live 照会の受け渡しに使う
        struct CellCoord
        {
            std::int16_t x = 0;
            std::int16_t y = 0;
            std::int16_t z = 0;
        };

        EditorMode() noexcept = default;
        ~EditorMode() noexcept = default;

        //! @brief 保存時に live 実体から SceneData を作る捕捉関数を差す。 未設定なら保存できない
        void SetCaptureLevelFn(std::function<NS::Object::SceneData()> fn) noexcept { m_captureLevel = std::move(fn); }

        //! @brief grid 編集・ undo を live へ通す適用経路を差す。 未設定なら grid 編集は何もしない
        void SetApplier(IObjectSnapshotApplier* applier) noexcept { m_applier = applier; }

        //! @brief 読込済みシーンデータを実体側へ取り込む関数を差す。 読込の完了時に呼ぶ
        void SetLoadLevelFn(std::function<void(NS::Object::SceneData&&)> fn) noexcept { m_loadLevel = std::move(fn); }

        //! @brief cell に居る cell ブラシ配置物の永続 id を live から引く関数を差す。 不在は k_NoObjectId
        void SetFindCellObjectFn(std::function<std::uint32_t(std::int16_t, std::int16_t, std::int16_t)> fn) noexcept
        {
            m_findCellObject = std::move(fn);
        }

        //! @brief live の全 cell ブラシ配置物の cell 座標一覧を返す関数を差す。 カーソルの ray 判定が読む
        void SetCollectCellsFn(std::function<std::vector<CellCoord>()> fn) noexcept { m_collectCells = std::move(fn); }

        //! @brief 新規配置物へ永続 id を 1 個振る関数を差す
        void SetAllocateIdFn(std::function<std::uint32_t()> fn) noexcept { m_allocateId = std::move(fn); }
        void SetInput(NS::Platform::Input* input) noexcept { m_input = input; }
        void SetImGui(NS::UI::ImGuiContext* imgui) noexcept { m_imgui = imgui; }
        void SetCameraComponent(NS::Object::CameraComponent* camera) noexcept { m_camera = camera; }

        void SetActive(bool active) noexcept { m_active = active; }
        [[nodiscard]] bool IsActive() const noexcept { return m_active; }

        //! グリッドに対する編集入力を一時的に無効化する
        void SetInputSuppressed(bool suppressed) noexcept { m_inputSuppressed = suppressed; }

        //! 編集入力が基準にする表示矩形を渡す。カーソルのレイはこの矩形基準で飛ぶ
        void SetViewRect(const ViewRect& rect) noexcept { m_viewRect = rect; }

        //! マウスが表示矩形のパネル上に居るかを渡す。偽の間は配置カーソルを立てない
        void SetViewHovered(bool hovered) noexcept { m_viewHovered = hovered; }

        //! ギズモドラッグ中だけ undo/redo を止める。 旧 Transform への書込を防ぐ
        void SetUndoRedoSuppressed(bool suppressed) noexcept { m_undoRedoSuppressed = suppressed; }

        //! @brief 毎フレームの更新処理を行い、入力に応じた編集操作を実行する
        void Tick() noexcept;

        //! @brief 現在のカーソル位置に応じたプレビュー（AABBなど）の描画コマンドを発行する
        void RenderCursorPreview() noexcept;

        [[nodiscard]] const NS::Editor::UndoStack& Undo() const noexcept { return m_undo; }
        [[nodiscard]] NS::Editor::UndoStack& Undo() noexcept { return m_undo; }

        //! @brief テストや自動処理向けに、入力を介さずに指定座標へ直接編集操作を実行する
        void PlaceUnderCursorProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept;
        void DeleteAtProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept;
        void RotateAtProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept;

        //! @brief 最後の保存 / 読込のあとに編集が入ったか
        [[nodiscard]] bool HasUnsavedChanges() const noexcept { return m_undo.Version() != m_savedUndoVersion; }

        //! @brief 今開いているレベル名。 まだ名前が決まっていなければ空
        [[nodiscard]] const std::string& CurrentLevelName() const noexcept { return m_currentLevelName; }

        //! @brief レベルデータに変更が加えられたかどうかを返す
        [[nodiscard]] bool IsLevelDirty() const noexcept { return m_levelDirty; }
        void ClearLevelDirty() noexcept { m_levelDirty = false; }

        [[nodiscard]] CategoryPalette& Palette() noexcept { return m_palette; }

        //! テスト用にカーソル状態を直接設定する
        void SetCursorForTest(const CursorState& state) noexcept { m_cursor = state; }

        //! @brief 保存および読み込みに関するショートカット入力の処理を行う
        void HandleSaveLoadInput() noexcept;

        //! ファイルブラウザのモーダルUIを描画し、セーブ・ロード処理を実行する
        void RenderFileBrowser() noexcept;

        //! @brief アプリケーション終了時などの保存処理を行う
        [[nodiscard]] bool SaveForQuit() noexcept;

        //! @brief 起動レベルが実在するのに読めなかったことを印す。 終了保存が既存ファイルを上書きしないようにする
        void MarkBootLevelLoadFailed() noexcept { m_bootLevelLoadFailed = true; }

        //! 1 手戻す。 戻せたら true。 メニューとキーが同じ入口で呼ぶ
        bool PerformUndo() noexcept;
        //! 1 手やり直す。 やり直せたら true
        bool PerformRedo() noexcept;
        [[nodiscard]] bool CanUndo() const noexcept { return m_undo.UndoSize() > 0; }
        [[nodiscard]] bool CanRedo() const noexcept { return m_undo.RedoSize() > 0; }

        //! 現在名で上書き保存。 名前が無ければ名前を付けて保存を開く
        void RequestSave() noexcept;
        //! 名前を付けて保存のモーダルを開く
        void OpenSaveModal() noexcept { m_fileBrowser.OpenSaveModal(m_currentLevelName); }
        //! 読込のモーダルを開く
        void OpenLoadModal() noexcept { m_fileBrowser.OpenLoadModal(); }

    private:
        std::function<NS::Object::SceneData()> m_captureLevel;    // 保存時に live から SceneData を作る
        IObjectSnapshotApplier* m_applier = nullptr;               // grid 編集・ undo を live へ通す適用経路
        std::function<void(NS::Object::SceneData&&)> m_loadLevel; // 読込済みデータを実体側へ取り込む
        std::function<std::uint32_t(std::int16_t, std::int16_t, std::int16_t)>
            m_findCellObject;                                   // cell に居る配置物の永続 id を live から引く
        std::function<std::vector<CellCoord>()> m_collectCells; // live の cell ブラシ座標一覧
        std::function<std::uint32_t()> m_allocateId;            // 新規配置物の永続 id 採番
        NS::Platform::Input* m_input = nullptr;
        NS::UI::ImGuiContext* m_imgui = nullptr;
        NS::Object::CameraComponent* m_camera = nullptr;

        bool m_active = true;
        bool m_inputSuppressed = false;
        bool m_undoRedoSuppressed = false; // ギズモドラッグ中だけ立てて undo/redo を止める
        bool m_levelDirty = true;
        ViewRect m_viewRect{};              // 編集入力が基準にする表示矩形。幅 0 の間は入力を受けない
        bool m_viewHovered = true;          // マウスがパネル上に居るか。全画面直描き時は常に真
        std::uint8_t m_currentRotation = 0; // 90度刻みの回転状態 (0〜3)

        CursorState m_cursor{};
        CategoryPalette m_palette{};
        NS::Editor::UndoStack m_undo;
        LevelFileBrowser m_fileBrowser{};

        std::string m_currentLevelName;
        bool m_bootLevelLoadFailed = false;   // 起動レベルが実在かつ読込失敗した
        std::uint64_t m_savedUndoVersion = 0; // 最後に保存 / 読込した時点の履歴の版

        std::string m_statusMessage;
        bool m_statusError = false;
        float m_statusTimer = 0.0f;

        // プレビュー表示用の滑らかな回転状態
        NS::Math::Quaternion m_displayedYawQuat{NS::Math::Quaternion::Identity};

        void UpdateCursorFromInput() noexcept;
        void HandlePlaceDeleteInput() noexcept;
        void HandleRotationInput() noexcept;
        void HandleUndoRedoInput() noexcept;

        //! cell に cell ブラシ配置物が居るか。 live 照会が未設定なら常に不在
        [[nodiscard]] bool HasObjectAtCell(std::int16_t x, std::int16_t y, std::int16_t z) const noexcept;

        [[nodiscard]] bool SaveLevelToName(std::string_view name) noexcept;
        void OverwriteCurrentLevel() noexcept;
    };
} // namespace NS::Editor