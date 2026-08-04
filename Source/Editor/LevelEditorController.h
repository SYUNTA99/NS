#pragma once

#include "Editor/EditorMode.h"
#include "Editor/EditorObjects.h"
#include "Editor/GizmoEditor.h"
#include "Editor/Undo/ObjectSnapshotApplier.h"
#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/RenderSettings.h"
#include "Runtime/Object/Components/VirtualCameraComponent.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace NS::Object
{
    class GameObject;
    class Transform;
    class CameraBrainComponent;
    class CameraComponent;
    class World;
    class ThirdPersonFollowComponent;
    class Scene;
    struct FreeFlightInput;
    struct SceneView;
} // namespace NS::Object

namespace NS::UI
{
    class ImGuiContext;
}

class EditorCameraRig;

//! @brief レベル編集機能およびプレイモードの切り替えを統括するコントローラ
//! @details Scene 上の live な配置物 / カメラ / プレイ進行を操作し、cursor / palette / undo の
//! EditorMode ・ギズモ変形・free-fly カメラ・編集↔プレイのモード切替を実現する。Editor が 1 個所有し、
//! 出荷 build には本クラスも Editor も含めない
class LevelEditorController : public NS::Core::NonCopyable
{
public:
    explicit LevelEditorController(NS::Object::Scene* scene) noexcept;
    ~LevelEditorController();

    //! scene の OnStart 完了後に呼ぶ。free-fly カメラ / EditorMode / ギズモを立ち上げ編集モードへ入る
    void Setup(NS::UI::ImGuiContext* imgui);

    //! fixed step 更新。編集中は free-fly カメラ / ギズモ / EditorMode を回す。プレイ中は何もしない
    void Tick();

    //! ギズモ / palette / 編集ビジュアルといった render フレームの上乗せ描画と出所デバッグ入力の退避
    void Render();

    //! scene 破棄の前に呼ぶ。ギズモ選択解除と free-fly カメラの後始末
    void Teardown();

    //! 現在のモード（編集モード or プレイモード）
    enum class Mode : std::uint8_t
    {
        Edit,
        Play
    };

    [[nodiscard]] Mode CurrentMode() const noexcept { return m_mode; }

    //! 編集モードからプレイモードへ移行する
    void EnterPlay() noexcept;

    //! プレイモードから編集モードへ移行する
    void EnterEdit() noexcept;

    //! プレイ中に 1 fixed step だけコマ送りする。 自動で一時停止に入り、 手触り検証で 1 コマずつ観察する
    void RequestStepFrame() noexcept;

    //! 生きたパネル (編集= Scene / プレイ= Game) が image を描いたフレームで矩形と hover を渡す
    void SetGameView(int x, int y, int width, int height, bool hovered) noexcept;
    //! F5 の全画面直描き用。予備の全画面矩形 + hover 真を立てる
    void ClearGameView() noexcept;
    //! UI 表示中に生きたパネルが裏へ隠れたフレームで呼ぶ。編集入力とオーバーレイを止める
    void HideGameView() noexcept;
    //! 現在の表示矩形。未設定時は全画面の予備矩形を返す
    [[nodiscard]] NS::Editor::ViewRect CurrentViewRect() const noexcept;
    //! 生きたパネルの画像上にマウスが居るか。非表示フレームは偽
    [[nodiscard]] bool GameViewHovered() const noexcept { return m_gameViewHovered; }
    //! 生きたパネルが裏へ隠れているか
    [[nodiscard]] bool GameViewHidden() const noexcept { return m_gameViewHidden; }

    //! @brief Scene パネルが映っているフレームで真を渡す
    //! @details プレイ中の当たり表示の門。 Scene が映っていない間は線を積まず、 ゲーム画面へ出さない
    void SetSceneViewVisible(bool visible) noexcept { m_sceneViewVisible = visible; }

    //! プレイ中に Scene タブへ自由視点を映すフレームで毎回呼ぶ。入力を free-fly カメラへ流す
    //! 編集モード中は何もしない
    void TickPlaySceneView(const NS::Object::FreeFlightInput& input) noexcept;

    //! @brief Scene パネルに映す視点。プレイ中は自由視点を上書き、編集中は Brain 任せ (nullopt)
    [[nodiscard]] std::optional<NS::Object::CameraPose> SceneViewPose() noexcept;
    //! @brief Game パネルに映す視点。編集中はゲームカメラを上書き、プレイ中は Brain 任せ (nullopt)
    [[nodiscard]] std::optional<NS::Object::CameraPose> GameViewPose() noexcept;
    //! @brief 可視な中央ビュー列を world へ流す。空なら現描画先へ 1 回だけ描く従来動作
    void SetSceneViews(std::vector<NS::Object::SceneView> views);

    //! @brief 今の描画視点。カメラ不在なら nullopt。Scene ビューの向き表示が読む
    [[nodiscard]] std::optional<NS::Object::CameraPose> CurrentViewPose() noexcept;

    [[nodiscard]] NS::Editor::EditorMode& Editor() noexcept { return m_editor; }

    //! プレイの時間停止。実体はシーンの時間停止スイッチで、ここはエディタ操作の窓口
    [[nodiscard]] bool PlayPaused() const noexcept;
    void TogglePlayPause() noexcept;

    //! シーンの環境値。実体側が唯一の出所で、UI はこれを直接編集する
    [[nodiscard]] NS::Object::SceneEnvironment& Environment() noexcept;

    //! 編集対象の live world。一覧 UI と参照候補は範囲 for か ObjectAt でここを直接読む
    [[nodiscard]] const NS::Object::World& World() const noexcept;

    //! 配置ツールのモード（Build/Object）を取得する
    [[nodiscard]] bool ObjectToolActive() const noexcept { return m_editorToolMode == EditorToolMode::Object; }
    //! 配置ツールを Build / Object 切替える。Build へ戻す時はギズモ選択を解除する
    void SetObjectToolActive(bool active) noexcept;

    //! 現在選択中の配置物の表示用添字。選択の真実は永続 id で、添字は live から引き直す
    [[nodiscard]] std::size_t SelectedObjectIndex() const noexcept;
    //! Hierarchy から添字で配置物を選択する。Object ツールへ切替え、ギズモ選択も貼る
    void SelectObjectByIndex(std::size_t index) noexcept;
    //! 永続 id で配置物を 1 体だけ選択する。0 を渡すと選択を外す
    void SelectObjectById(std::uint32_t id) noexcept;
    //! 現在の主対象の永続 id。未選択は 0。ギズモと Inspector はこの 1 体を見る
    [[nodiscard]] std::uint32_t SelectedObjectId() const noexcept { return m_selectedObjectId; }

    //! 選択中の全配置物の永続 id。主対象も含む
    [[nodiscard]] const std::vector<std::uint32_t>& SelectedObjectIds() const noexcept { return m_selectionIds; }
    //! id が選択に含まれるか
    [[nodiscard]] bool IsObjectSelected(std::uint32_t id) const noexcept;
    //! 選択へ足す / 選択から外す。足した物が主対象になる
    void ToggleObjectSelection(std::uint32_t id) noexcept;
    //! @brief 選択を丸ごと差し替える
    //! @param ids 選択する永続 id 一覧
    //! @param primary 主対象。ids に無ければ足す
    void SelectObjects(std::vector<std::uint32_t> ids, std::uint32_t primary) noexcept;

    //! Inspector が編集 / 表示できる選択を持つか
    [[nodiscard]] bool HasInspectableSelection() const noexcept;
    //! Inspector 表示用に選択中 ObjectData のコピーを返す。未選択は既定値
    [[nodiscard]] NS::Object::ObjectData SelectedObjectSnapshot() const noexcept;

    //! 選択中の配置物の位置 / 回転 / スケールを live へ直接設定する。非選択時は何もしない
    void SetSelectedFreePosition(NS::Math::Vector3 position) noexcept;
    void SetSelectedFreeRotation(NS::Math::Quaternion rotation) noexcept;
    void SetSelectedFreeScale(NS::Math::Vector3 scale) noexcept;

    //! 変形編集（ドラッグ操作）の開始および確定処理を行う
    void BeginTransformEdit() noexcept;
    void CommitTransformEdit() noexcept;

    //! コンポーネント反射編集（ドラッグ操作）の開始および確定処理を行う
    void BeginComponentEdit() noexcept;
    void CommitComponentEdit() noexcept;

    //! 編集視点の中心あたりに新しい自由オブジェクトを 1 個追加して選択する。Undo 対応
    void AddObject();

    //! @brief 基本形を 1 個、編集視点の中心あたりへ追加して選択する。Undo 対応
    //! @param kind 立方体 / 球 / 坂 / 空の器
    void AddPrimitive(NS::Editor::PrimitiveKind kind);

    //! @brief メッシュ資産を 1 体として編集視点の中心あたりへ置く。Undo 対応
    //! @param meshPath 資産ファイルの絶対パス。参照は ContentRoot 相対へ直して持つ
    void AddObjectWithMesh(const std::filesystem::path& meshPath);

    //! 選択中の配置物に対応する runtime GameObject。未選択 / 未構築は nullptr
    [[nodiscard]] NS::Object::GameObject* SelectedObjectGameObject() noexcept;
    //! 選択中の配置物がプレイヤー実体か
    [[nodiscard]] bool SelectedIsPlayerObject() const noexcept;

    //! 選択中のオブジェクトを指す ObjectRef 参照を全オブジェクトから集める
    [[nodiscard]] std::vector<NS::Object::ObjectRefLocation> ReferencesToSelected();

    //! @brief 配置物の表示名を差し替えて undo へ積む
    //! @param id 対象の永続 object id。 居なければ何もしない
    //! @param name 新しい表示名。 空にすると型からの導出名へ戻る
    void RenameObject(std::uint32_t id, std::string_view name);

    //! @brief 配置物の親を差し替えて undo へ積む
    //! @param id 対象の永続 object id
    //! @param parentId 新しい親の永続 id。 0 で root へ戻す
    //! @retresult 付け替えたら true。 自分自身や自分の子孫を親に指定した場合は false で何もしない
    //! @details 見た目が動かないよう、 今の world 変換を新しい親空間の local へ計算し直して持ち替える
    bool SetObjectParent(std::uint32_t id, std::uint32_t parentId);

    //! コンポーネントの操作関連機能
    void AddComponentToSelected(std::string_view typeName);
    void RemoveComponentFromSelected(std::size_t componentIndex);

    //! @brief 選択中の配置物のコンポーネント 1 個について、 データの札を立て / 下ろす
    //! @details 下ろした札は保存に残り、 読み直しても下りたまま。 player の入力と transform は守って何もしない
    void SetComponentEnabledOnSelected(std::size_t componentIndex, bool enabled);

    void DuplicateSelectedObject();

    //! @brief 選択中の配置物を子孫ごと削除して undo へ 1 単位で積む
    //! @details プレイヤーが対象または子孫に含まれる場合は何もしない
    void DeleteSelectedObject();

    //! 編集カメラの注視点を選択中の配置物へ寄せる。 広がりに応じて距離も取り直す
    void FocusSelectedInView() noexcept;

    void CopyComponentToClipboard(std::size_t componentIndex);
    void PasteClipboardComponentToSelected();

    [[nodiscard]] bool HasClipboardComponent() const noexcept { return m_componentClipboard.has_value(); }

    //! カメラ操作関連のオブジェクトを取得する
    [[nodiscard]] NS::Object::GameObject* CameraBrainObject() noexcept;
    [[nodiscard]] NS::Object::GameObject* ActiveVirtualCameraObject() noexcept;

    void SelectCamera() noexcept;
    [[nodiscard]] bool IsCameraSelected() const noexcept { return m_specialSelection == SpecialSelection::Camera; }

    [[nodiscard]] bool HasGizmoSelection() const noexcept
    {
        return ObjectToolActive() && m_gizmo.Selected() != nullptr;
    }
    bool ApplyMaterialToSelected(const std::filesystem::path& matPath);

    //! デバッグ用のレンダリング設定を取得する
    [[nodiscard]] const NS::Graphics::RenderSettings& DebugResolvedSettings() const noexcept
    {
        return m_debugResolvedSettings;
    }
    [[nodiscard]] const NS::Graphics::RenderSettingsOverride& DebugSceneOverride() const noexcept
    {
        return m_debugSceneOverride;
    }
    [[nodiscard]] const NS::Graphics::RenderSettingsOverride& DebugPlayerObjectOverride() const noexcept
    {
        return m_debugPlayerObjectOverride;
    }

private:
    void TickEdit();

    //! プレイを終えて編集の姿へ戻す。部品を寝かせ pose を凍結時の姿へ復元し、カーソルを出す
    //! 編集モードでしか要らない遷移なのでエディタが持つ。演出破棄とゴール旗は CancelPlayEffects に任せる
    void LeavePlayForEdit() noexcept;

    //! 配置物を新しい永続 id で 1 体追加する唯一の経路。採番・履歴登録・選択をまとめて面倒を見る
    void PushCreateObject(NS::Object::ObjectData object);

    void RenderCameraGizmos(const NS::Math::Matrix& viewProjection, NS::Math::Size2D viewport) noexcept;
    //! @brief 当たり形状を線で描く
    //! @param all 真なら全配置物、 偽なら選んでいる分だけ
    void RenderColliderWireframes(bool all) noexcept;

    //! 主対象以外の選択物を枠で見せる。ギズモは 1 体にしか出ないので、選んだ範囲を目で追えるようにする
    void RenderSelectionOutlines() noexcept;
    void RefreshGizmoSelectables();
    void ResolveSelectionFromId() noexcept;

    //! 主対象だけを差し替える。選択集合には触らない
    void SetPrimarySelection(std::uint32_t id) noexcept;

    //! ドラッグ開始時に、主対象以外の選択物の world 変換を控える
    void CaptureDragFollowers() noexcept;
    //! 主対象がドラッグで動いた分を、控えた残りの選択へ同じだけ効かせる
    void ApplyDragToFollowers() noexcept;

    [[nodiscard]] NS::Object::ThirdPersonFollowComponent* SelectedFollowCamera() noexcept;
    void SyncFollowCameraPoses();
    void ApplyFollowCameraGizmoDrag();
    void CaptureSelectionFromGizmo() noexcept;

    [[nodiscard]] NS::Object::CameraBrainComponent* Brain() const noexcept;
    [[nodiscard]] NS::Object::CameraComponent* MainCamera() const noexcept;

    NS::Object::Scene* m_scene = nullptr;        // 編集対象のシーン。回す/止める/コマ送りもこのシーンのスイッチ
    NS::Editor::ObjectSnapshotApplier m_applier; // 編集を live へ写す口。 undo コマンドが叩く適用先

    // 生きたパネルの表示矩形。未設定時は CurrentViewRect が全画面の予備矩形を返す
    NS::Editor::ViewRect m_gameViewRect{};
    bool m_gameViewRectValid = false;
    bool m_gameViewHovered = false;
    bool m_gameViewHidden = false;   // UI 表示中に生きたパネルが裏へ隠れているか
    bool m_sceneViewVisible = false; // Scene パネルが映っているか。 プレイ中の当たり表示の門

    NS::UI::ImGuiContext* m_imgui = nullptr;            // UI描画用コンテキスト、非所有
    std::unique_ptr<EditorCameraRig> m_editorCameraRig; // 編集用自由視点カメラ

    NS::Editor::EditorMode m_editor{}; // 編集モード管理（カーソル・Undo等）
    Mode m_mode = Mode::Edit;          // 現在の実行モード

    enum class EditorToolMode : std::uint8_t
    {
        Build,
        Object
    };
    EditorToolMode m_editorToolMode = EditorToolMode::Build; // ツールモード（Build/Object）

    NS::Editor::GizmoEditor m_gizmo{}; // 変形ギズモ管理

    std::vector<NS::Object::GameObject*> m_selectablePtrs;  // 選択可能なオブジェクト
    std::vector<NS::Math::Vector3> m_selectableHalfExtents; // 各オブジェクトのサイズ
    std::vector<std::uint8_t> m_selectablePickable;         // 選択可否フラグ

    enum class SpecialSelection : std::uint8_t
    {
        None,
        Camera
    };
    SpecialSelection m_specialSelection = SpecialSelection::None; // 特別な選択状態

    std::uint32_t m_selectedObjectId = NS::Object::k_NoObjectId; // 主対象の永続ID (選択の唯一の真実)
    std::vector<std::uint32_t> m_selectionIds;                   // 選択中の全配置物。主対象も含む
    NS::Object::Transform* m_lastGizmoSelected = nullptr;        // 前フレームの選択対象

    //! ドラッグ開始時点の姿。主対象の動きを同じだけ他へ流すための控え
    struct DragFollower
    {
        std::uint32_t id = 0;
        NS::Math::Matrix world{};
    };
    std::vector<DragFollower> m_dragFollowers; // 主対象に付いて動く残りの選択
    NS::Math::Matrix m_dragPrimaryWorld{};     // ドラッグ開始時の主対象の world 変換
    bool m_dragFollowersValid = false;         // 控えが有効か。単体選択なら偽

    std::optional<nlohmann::json> m_componentClipboard; // コンポーネントのクリップボード ({type, fields} 1 件)

    bool m_gizmoWasDragging = false; // ドラッグ状態の保持
    bool m_transformEditing = false; // 変形編集の開始状態

    // 編集開始時の状態スナップショット。選択している分だけ並ぶ
    std::vector<std::pair<std::uint32_t, NS::Object::ObjectData>> m_editBaselines;

    bool m_componentEditing = false;                                    // コンポーネント編集の開始状態
    std::uint32_t m_componentEditBaselineId = NS::Object::k_NoObjectId; // 編集開始時の対象ID
    NS::Object::ObjectData m_componentEditBaseline{};                   // 編集開始時の状態スナップショット

    NS::Graphics::RenderSettings m_debugResolvedSettings{};             // 解決済みレンダリング設定
    NS::Graphics::RenderSettingsOverride m_debugSceneOverride{};        // シーン単位の設定オーバーライド
    NS::Graphics::RenderSettingsOverride m_debugPlayerObjectOverride{}; // プレイヤー単位の設定オーバーライド
};
