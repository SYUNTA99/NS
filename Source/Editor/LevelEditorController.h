#pragma once

#include "Editor/EditorCamera.h"
#include "Editor/EditorMode.h"
#include "Editor/EditorObjects.h"
#include "Editor/GizmoEditor.h"
#include "Editor/Undo/ObjectSnapshotApplier.h"
#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Object/Components/VirtualCamera.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace NS::Obj
{
    class GameObject;
    class Transform;
    class CameraBrain;
    class CameraComponent;
    class Component;
    class ObjectList;
    class ThirdPersonFollow;
    class Scene;
    struct SceneView;
} // namespace NS::Obj

namespace NS::UI
{
    class ImGuiContext;
}

//! @brief レベル編集機能およびプレイモードの切り替えを統括するコントローラ
//! @details Scene 上の live な配置物 / カメラ / プレイ進行を操作し、cursor / palette / undo の
//! EditorMode ・ギズモ変形・free-fly カメラ・編集↔プレイのモード切替を実現する。Editor が 1 個所有し、
//! 出荷 build には本クラスも Editor も含めない
class LevelEditorController : public NS::Core::NonCopyable
{
public:
    explicit LevelEditorController(NS::Obj::Scene* scene) noexcept;
    ~LevelEditorController();

    //! scene の OnStart 完了後に呼ぶ。free-fly カメラ / EditorMode / ギズモを立ち上げ編集モードへ入る
    void Setup(NS::UI::ImGuiContext* imgui);

    //! fixed step 更新。編集中は free-fly カメラ / ギズモ / EditorMode を回す。プレイ中は何もしない
    void Tick();

    //! カーソルプレビュー / カメラギズモ / 当たり線 / 選択枠 / ツールバー / ギズモを描画フレームへ重ねる
    void Render();

    //! scene 破棄の前に呼ぶ。ギズモ選択解除と free-fly カメラの後始末
    void Teardown();

    //! 編集モードかプレイモードか
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

    //! プレイ中に 1 fixed step だけコマ送りする。自動で一時停止に入り、手触り検証で 1 コマずつ観察する
    void RequestStepFrame() noexcept;

    //! 前面のパネル (編集= Scene / プレイ= Game) が image を描いたフレームで矩形と hover を渡す
    void SetGameView(int x, int y, int width, int height, bool hovered) noexcept;
    //! F5 の全画面直描き用。予備の全画面矩形 + hover 真を立てる
    void ClearGameView() noexcept;
    //! UI 表示中に前面のパネルが裏へ隠れたフレームで呼ぶ。編集入力とオーバーレイを止める
    void HideGameView() noexcept;
    //! 現在の表示矩形。未設定時は全画面の予備矩形を返す
    [[nodiscard]] NS::Editor::ViewRect CurrentViewRect() const noexcept;
    //! 前面のパネルが裏へ隠れているか
    [[nodiscard]] bool GameViewHidden() const noexcept { return m_gameViewHidden; }

    //! @brief Scene パネルが映っているフレームで真を渡す
    //! @details プレイ中の当たり表示の条件。Scene が映っていない間は線を積まず、ゲーム画面へ出さない
    void SetSceneViewVisible(bool visible) noexcept { m_sceneViewVisible = visible; }

    //! プレイ中に Scene タブへ自由視点を映すフレームで毎回呼ぶ。入力を free-fly カメラへ流す
    //! 編集モード中は何もしない
    void TickPlaySceneView(const NS::Editor::EditorCameraInput& input) noexcept;

    //! @brief Scene パネルに映す視点。プレイ中は自由視点を上書き、編集中は Brain 任せ (nullopt)
    [[nodiscard]] std::optional<NS::Obj::CameraPose> SceneViewPose() noexcept;
    //! @brief Game パネルに映す視点。編集中はゲームカメラを上書き、プレイ中は Brain 任せ (nullopt)
    [[nodiscard]] std::optional<NS::Obj::CameraPose> GameViewPose() noexcept;
    //! @brief 可視な中央ビュー列を Scene へ流す。空なら現描画先へ 1 回だけ描く
    void SetSceneViews(std::vector<NS::Obj::SceneView> views);

    [[nodiscard]] NS::Editor::EditorMode& Editor() noexcept { return m_editor; }

    //! 編集の自由視点カメラ。Inspector が感度をライブ編集する
    [[nodiscard]] NS::Editor::EditorCamera& EditorFreeCamera() noexcept { return m_editorCamera; }

    //! プレイの時間停止。実体はシーンの時間停止スイッチで、ここはエディタ操作の入口
    [[nodiscard]] bool PlayPaused() const noexcept;
    void TogglePlayPause() noexcept;

    //! シーンの環境値。実体側が唯一の出所で、UI はこれを直接編集する
    [[nodiscard]] NS::Obj::SceneEnvironment& Environment() noexcept;

    //! 編集対象の live な ObjectList。一覧 UI と参照候補は範囲 for か ObjectAt でここを直接読む
    [[nodiscard]] const NS::Obj::ObjectList& Objects() const noexcept;

    //! 配置ツールのモード (Build / Object) を取得する
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
    //! @param[in] ids 選択する永続 id 一覧
    //! @param[in] primary 主対象。ids に無ければ足す
    void SelectObjects(std::vector<std::uint32_t> ids, std::uint32_t primary) noexcept;

    //! Inspector が編集 / 表示できる選択を持つか
    [[nodiscard]] bool HasInspectableSelection() const noexcept;

    //! 選択中の配置物の位置 / 回転 / スケールを live へ直接設定する。非選択時は何もしない
    void SetSelectedFreePosition(NS::Core::Vector3 position);
    void SetSelectedFreeRotation(NS::Core::Quaternion rotation);
    void SetSelectedFreeScale(NS::Core::Vector3 scale);

    //! ドラッグでの変形を始める / 確定する
    void BeginTransformEdit() noexcept;
    void CommitTransformEdit() noexcept;

    //! Inspector のリフレクション項目をドラッグで編集し始める / 確定する
    void BeginComponentEdit() noexcept;
    void CommitComponentEdit() noexcept;

    //! プレイ中の手編集を凍結スナップショットへも写す。編集復帰の組み直しを跨いで調整値が残る
    //! 編集モードでは live が唯一の出所なので何もしない。Inspector の編集箇所と transform 設定子が呼ぶ
    void MirrorPlayEditToBaseline(const NS::Obj::Component& comp, std::string_view fieldName);

    //! 編集視点の中心あたりに新しい自由オブジェクトを 1 個追加して選択する。Undo 対応
    void AddObject();

    //! @brief 基本形を 1 個、編集視点の中心あたりへ追加して選択する。Undo 対応
    //! @param[in] kind 立方体 / 球 / 坂 / 空の GameObject
    void AddPrimitive(NS::Editor::PrimitiveKind kind);

    //! @brief メッシュ資産を 1 体として編集視点の中心あたりへ置く。Undo 対応
    //! @param[in] meshPath 資産ファイルの絶対パス。参照は ContentRoot 相対へ直して持つ
    void AddObjectWithMesh(std::string_view meshPath);

    //! 選択中の配置物に対応する runtime GameObject。未選択 / 未構築は nullptr
    [[nodiscard]] NS::Obj::GameObject* SelectedObjectGameObject() noexcept;
    //! 選択中の配置物がプレイヤー実体か
    [[nodiscard]] bool SelectedIsPlayerObject() const noexcept;

    //! @brief 配置物の表示名を差し替えて undo へ積む
    //! @param[in] id 対象の永続 object id。居なければ何もしない
    //! @param[in] name 新しい表示名。空にすると型からの導出名へ戻る
    void RenameObject(std::uint32_t id, std::string_view name);

    //! @brief 配置物の親を差し替えて undo へ積む
    //! @param[in] id 対象の永続 object id
    //! @param[in] parentId 新しい親の永続 id。0 で root へ戻す
    //! @retresult 付け替えたら true。自分自身や自分の子孫を親に指定した場合は false で何もしない
    //! @details 見た目が動かないよう、今の world 変換を新しい親空間の local へ計算し直して持ち替える
    bool SetObjectParent(std::uint32_t id, std::uint32_t parentId);

    //! @brief 選択中の配置物へコンポーネントを 1 個追加して undo へ積む
    //! @param[in] typeName 追加するコンポーネントの型名
    void AddComponentToSelected(std::string_view typeName);
    //! @brief 選択中の配置物からコンポーネントを 1 個削除して undo へ積む
    //! @details 残り 1 個になる削除・player の入力・transform は消さず何もしない
    //! @param[in] componentIndex 削除するコンポーネントの添字
    void RemoveComponentFromSelected(std::size_t componentIndex);

    //! @brief 選択中の配置物のコンポーネント 1 個について、データの active を切り替える
    //! @details false は保存に残り、読み直しても false のまま。player の入力と transform は守って何もしない
    void SetComponentEnabledOnSelected(std::size_t componentIndex, bool enabled);

    //! 選択中の配置物を新しい永続 id で複製して undo へ積む。プレイヤーは複製の対象から外す
    void DuplicateSelectedObject();

    //! @brief 選択中の配置物を子孫ごと削除して undo へ 1 単位で積む
    //! @details プレイヤーが対象または子孫に含まれる場合は何もしない
    void DeleteSelectedObject();

    //! 編集カメラの注視点を選択中の配置物へ寄せる。広がりに応じて距離も取り直す
    void FocusSelectedInView() noexcept;

    //! @brief 選択中の配置物からコンポーネントを 1 個クリップボードへ控える
    //! @param[in] componentIndex 控えるコンポーネントの添字
    void CopyComponentToClipboard(std::size_t componentIndex);
    //! クリップボードのコンポーネントを選択中の配置物の末尾へ追加する。上書きはしない
    void PasteClipboardComponentToSelected();

    //! @brief 選択中の配置物の componentIndex 番目の component の名前を変えて undo へ積む
    //! @details 配置物の中で重なれば番号を付ける。空は型名に戻す。参照は id で持つので切れない
    void RenameComponentOnSelected(std::size_t componentIndex, std::string_view name);

    //! クリップボードにコンポーネントが控えられているか
    [[nodiscard]] bool HasClipboardComponent() const noexcept { return m_componentClipboard.has_value(); }

    //! Object ツール中にギズモが配置物を選択しているか
    [[nodiscard]] bool HasGizmoSelection() const noexcept
    {
        return ObjectToolActive() && m_gizmo.Selected() != nullptr;
    }
    //! 選択中の配置物の MeshRenderer に材質を割り当てて undo へ積む。対象外なら false
    bool ApplyMaterialToSelected(std::string_view matPath);

private:
    void TickEdit();

    //! プレイを終えて編集の姿へ戻す。凍結スナップショットから世界を組み直し、操作系を休止させ、カーソルを出す
    //! 編集モードでしか要らない遷移なのでエディタが持つ。組み直しで確保が起きるため noexcept にしない
    void LeavePlayForEdit();

    //! 配置物を新しい永続 id で 1 体追加する唯一の経路。採番・履歴登録・選択をまとめて面倒を見る
    void PushCreateObject(NS::Obj::ObjectData object);

    void RenderCameraGizmos(const NS::Core::Matrix& viewProjection, NS::Core::Size2D viewport) noexcept;
    //! @brief 当たり形状を線で描く
    //! @param[in] all 真なら全配置物、偽なら選んでいる分だけ
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

    [[nodiscard]] NS::Obj::ThirdPersonFollow* SelectedFollowCamera() noexcept;
    void SyncFollowCameraPoses();
    void ApplyFollowCameraGizmoDrag();
    void CaptureSelectionFromGizmo() noexcept;

    [[nodiscard]] NS::Obj::CameraBrain* Brain() const noexcept;
    [[nodiscard]] NS::Obj::CameraComponent* MainCamera() const noexcept;

    NS::Obj::Scene* m_scene = nullptr;           // 編集対象のシーン。回す/止める/コマ送りもこのシーンのスイッチ
    NS::Editor::ObjectSnapshotApplier m_applier; // 編集を live へ写す口。undo コマンドが叩く適用先

    // 前面のパネルの表示矩形。未設定時は CurrentViewRect が全画面の予備矩形を返す
    NS::Editor::ViewRect m_gameViewRect{};
    bool m_gameViewRectValid = false;
    bool m_gameViewHovered = false;
    bool m_gameViewHidden = false;   // UI 表示中に前面のパネルが裏へ隠れているか
    bool m_sceneViewVisible = false; // Scene パネルが映っているか。プレイ中の当たり表示の条件

    NS::UI::ImGuiContext* m_imgui = nullptr; // UI描画用コンテキスト、非所有
    NS::Editor::EditorCamera m_editorCamera; // 編集用自由視点カメラ

    // 編集復帰時にプレイ視点から自由視点へ繋ぐブレンド。TickEdit が進める
    NS::Obj::CameraPose m_editBlendFrom{};
    float m_editBlendElapsed = 0.0f;
    bool m_editBlending = false;

    NS::Editor::EditorMode m_editor{}; // カーソルと Undo を持つ編集モード
    Mode m_mode = Mode::Edit;          // 現在の実行モード

    enum class EditorToolMode : std::uint8_t
    {
        Build,
        Object
    };
    EditorToolMode m_editorToolMode = EditorToolMode::Build; // ツールモード (Build / Object)

    NS::Editor::GizmoEditor m_gizmo{}; // 変形ギズモ管理

    std::vector<NS::Obj::GameObject*> m_selectablePtrs; // 選択可能なオブジェクト
    std::vector<std::uint8_t> m_selectablePickable;     // 1 は MeshRenderer を持つ配置物。ギズモが先に選ぶ

    std::uint32_t m_selectedObjectId = NS::Obj::k_NoObjectId; // 主対象の永続 id。選択の一次情報
    std::vector<std::uint32_t> m_selectionIds;                // 選択中の全配置物。主対象も含む
    NS::Obj::Transform* m_lastGizmoSelected = nullptr;        // 前フレームの選択対象

    //! ドラッグ開始時点の姿。主対象の動きを同じだけ他へ流すための控え
    struct DragFollower
    {
        std::uint32_t id = 0;
        NS::Core::Matrix world{};
    };
    std::vector<DragFollower> m_dragFollowers; // 主対象に付いて動く残りの選択
    NS::Core::Matrix m_dragPrimaryWorld{};     // ドラッグ開始時の主対象の world 変換

    std::optional<nlohmann::json> m_componentClipboard; // コンポーネントのクリップボード ({type, fields} 1 件)

    bool m_gizmoWasDragging = false; // ドラッグ状態の保持
    bool m_transformEditing = false; // 変形編集の開始状態

    // 編集開始時の状態スナップショット。選択している分だけ並ぶ
    std::vector<std::pair<std::uint32_t, NS::Obj::ObjectData>> m_editBaselines;

    bool m_componentEditing = false;                                 // コンポーネント編集の開始状態
    std::uint32_t m_componentEditBaselineId = NS::Obj::k_NoObjectId; // 編集開始時の対象 id
    NS::Obj::ObjectData m_componentEditBaseline{};                   // 編集開始時の状態スナップショット
};
