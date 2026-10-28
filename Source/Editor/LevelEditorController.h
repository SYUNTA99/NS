#pragma once

#include "Editor/EditorCamera.h"
#include "Editor/EditorMode.h"
#include "Editor/EditorActors.h"
#include "Editor/GizmoEditor.h"
#include "Editor/PlacementCatalog.h"
#include "Editor/PlayControls.h"
#include "Editor/StageBuildTool.h"
#include "Editor/Undo/ActorSnapshotApplier.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Windows/InputReader.h"
#include "NSlib/CoreObject/ClassObjs/VirtualCamera.h"
#include "NSlib/CoreObject/Reflection/SoundName.h"

#include "NSlib/Core/Types.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NS
{
    class SceneActor;
    class Transform;
    class CameraManager;
    class SceneCamera;
    class Object;
    class ActorList;
    class ThirdPersonFollow;
    class Scene;
    struct SceneView;
    class AssetManager;
    class Material;
    enum class FieldType;
    struct HitReactionSet;
    class HitSensorsObj;
} // namespace NS

namespace NS
{
    namespace Gfx
    {
        class DebugShapes;
    }
} // namespace NS

//! シーンの編集操作と編集・プレイの切替を管理する
class LevelEditorController : public NS::NonCopyable
{
public:
    explicit LevelEditorController(NS::Scene* scene) noexcept;
    ~LevelEditorController();

    //! scene の OnStart 完了後に呼ぶ。自由視点カメラ / EditorMode / ギズモを立ち上げ編集モードへ入り、
    //! startLevelName のレベルを EditorMode::OpenLevel で開く。空なら開かず、今のシーンのまま編集を始める
    void Setup(std::string_view startLevelName);

    //! 固定ステップ更新。編集中は自由視点カメラ / ギズモ / EditorMode を回す。プレイ中は Esc
    //! で隠したカーソルを出すだけ
    void Tick();

    //! 編集中のカーソルの枠 / パレット / ギズモを ImGui のフレームへ重ねる。プレイ中は何もしない
    void Render();

    //! Scene ビューの図形を積む。編集時は選択中、プレイ時は全アクターの当たりを表示する
    //! shapes は空で渡され、ビューの描画後に破棄される。viewProjection はそのビューの行列
    void DrawSceneViewShapes(NS::Gfx::DebugShapes& shapes, const NS::Matrix& viewProjection) noexcept;

    //! scene 破棄の前に呼ぶ。ギズモ選択解除と自由視点カメラの後始末
    void Teardown();

    //! 編集モードかプレイモードか
    enum class Mode : NS::uint8
    {
        Edit,
        Play
    };

    [[nodiscard]] Mode CurrentMode() const noexcept { return m_mode; }

    //! シーンを変えてよいか。編集モードの間だけ真
    //! 変える関数と画面の止め方は全部これを見る。EditorMode にも同じ答えを差す。
    //! プレイ中にインスペクタで欄を触る道 (NoteFieldEdit) は断らない。アクターへ書く欄はプレイ開始の写しへ写し、
    //! .actor へ書く欄はプレイ中も .actor へ書く
    [[nodiscard]] bool IsSceneEditable() const noexcept;

    //! 編集のショートカットを止めるか。ResolveInputOwnership の答えを毎回の描画の終わりに受ける
    //! キーを読むのは次の固定ステップの更新なので、1 つ前の描画の答えを読む
    void SetEditShortcutsBlocked(bool blocked) noexcept { m_editShortcutsBlocked = blocked; }
    [[nodiscard]] bool EditShortcutsBlocked() const noexcept { return m_editShortcutsBlocked; }

    //! 編集モードからプレイモードへ移行する
    void EnterPlay() noexcept;

    //! プレイモードから編集モードへ移行する
    void EnterEdit() noexcept;

    //! プレイ中に 1 固定ステップだけコマ送りする。自動で一時停止に入り、手触り検証で 1 コマずつ観察する
    void RequestStepFrame() noexcept;

    //! 前面のパネルが画像を描いたフレームで、矩形とカーソルの重なりを渡す
    void SetGameView(int x, int y, int width, int height, bool hovered) noexcept;
    //! F5 の全画面描画用。予備の矩形を使い、カーソルが重なっている状態にする
    void ClearGameView() noexcept;
    //! UI 表示中に前面のパネルが裏へ隠れたフレームで呼ぶ。編集入力とオーバーレイを止める
    void HideGameView() noexcept;
    //! 現在の表示矩形。未設定時は全画面の予備矩形を返す
    [[nodiscard]] NS::Editor::ViewRect CurrentViewRect() const noexcept;
    [[nodiscard]] bool GameViewHidden() const noexcept { return m_gameViewHidden; }

    //! プレイ中に Scene タブへ自由視点を映すフレームで毎回呼ぶ。入力を自由視点カメラへ流す
    //! 編集モード中は何もしない
    void TickPlaySceneView(const NS::Editor::EditorCameraInput& input) noexcept;

    //! Scene パネルに映す視点。プレイ中は自由視点を上書き、編集中は CameraManager 任せ (nullopt)
    [[nodiscard]] std::optional<NS::CameraPose> SceneViewPose() noexcept;
    //! Game パネルに映す視点。編集中はゲームカメラを上書き、プレイ中は CameraManager 任せ (nullopt)
    [[nodiscard]] std::optional<NS::CameraPose> GameViewPose() noexcept;
    //! 可視な中央ビュー列を Scene へ流す。空なら現描画先へ 1 回だけ描く
    void SetSceneViews(std::vector<NS::SceneView> views);

    //! 編集中の場面の写し (Scene::ToJson)。当たりの下見が写しから別の場面を組むのに読む
    //! @return 写し。場面が無ければ空の場面の文書
    [[nodiscard]] nlohmann::json SceneSnapshot() const;

    [[nodiscard]] NS::Editor::EditorMode& Editor() noexcept { return m_editor; }

    //! 素材の編集中の値を確定し、アクターと素材の選択を外してシーン設定を表示する。
    void SelectSceneSettings();
    [[nodiscard]] const std::string& SkyboxPath() const noexcept;
    //! プロジェクトからの相対パスを適用し、取り消しの履歴へ積む。空なら非表示。
    //! 参照先の形式が対応外、シーン未設定、プレイ中の場合は変更せず false。
    bool SetSkyboxPath(std::string_view path);
    //! シーンの曲。シーンが無ければ名前が空
    [[nodiscard]] const NS::SoundName& SceneMusic() const noexcept;
    //! シーンの曲の音量。シーンが無ければ SceneEnvironment の既定
    [[nodiscard]] float SceneMusicVolume() const noexcept;
    //! 曲と音量を適用し、変わった時だけ取り消しの履歴へ積む。音量は 0〜1 に収める。
    //! シーン未設定、プレイ中、音量が非数の場合は変更せず false。
    bool SetSceneMusic(const NS::SoundName& music, float volume);

    //! 編集の自由視点カメラ。Inspector が感度をライブ編集する
    [[nodiscard]] NS::Editor::EditorCamera& EditorFreeCamera() noexcept { return m_editorCamera; }

    //! プレイの時間停止。実体はシーンの時間停止スイッチで、ここはエディタ操作の入口
    [[nodiscard]] bool PlayPaused() const noexcept;
    void TogglePlayPause() noexcept;

    //! Game の画像か見出しの左クリックで視点操作を再開する。一時停止中か捕捉中なら何もしない
    void RecaptureCursorOnGameClick(bool gameViewClicked) noexcept;

    //! 編集対象の ActorList。一覧と参照候補はここから直接取得する
    [[nodiscard]] const NS::ActorList& Actors() const noexcept;

    enum class EditTool
    {
        Gizmo,
        Grid,
        StageBuild
    };
    [[nodiscard]] EditTool CurrentEditTool() const noexcept { return m_editTool; }
    void SetEditTool(EditTool tool) noexcept;
    [[nodiscard]] bool GizmoToolActive() const noexcept { return m_editTool == EditTool::Gizmo; }
    //! 配置ツールを Build / Object 切替える。Build へ戻す時はギズモ選択を解除する
    void SetGizmoToolActive(bool active) noexcept;

    //! 永続 id でアクターを 1 体だけ選択する。0 を渡すと選択を外す
    void SelectActorById(NS::uint32 id) noexcept;
    //! 現在の主対象の永続 id。未選択は 0。ギズモと Inspector はこの 1 体を見る
    [[nodiscard]] NS::uint32 SelectedActorId() const noexcept { return m_selectedActorId; }

    //! 選択中の全アクターの永続 id。主対象も含む
    [[nodiscard]] const std::vector<NS::uint32>& SelectedActorIds() const noexcept { return m_selectionIds; }
    [[nodiscard]] bool IsActorSelected(NS::uint32 id) const noexcept;
    //! 選択へ足す / 選択から外す。足した物が主対象になる
    void ToggleActorSelection(NS::uint32 id) noexcept;
    //! 選択を差し替える。primary が ids に含まれていなければ追加する
    void SelectActors(std::vector<NS::uint32> ids, NS::uint32 primary) noexcept;

    //! Inspector が編集 / 表示できる選択を持つか
    [[nodiscard]] bool HasInspectableSelection() const noexcept;

    //! ドラッグでの変形を始める / 確定する
    //! 確定では、始めた時から値が変わったシーンに書く項目 (姿勢の位置・回転・スケールなど) を上書きの一覧に足す
    void BeginTransformEdit() noexcept;
    void CommitTransformEdit() noexcept;

    //! Inspector のリフレクション項目をドラッグで編集し始める / 確定する
    void BeginClassObjEdit() noexcept;
    void CommitClassObjEdit() noexcept;

    //! プレイ中の手編集を、プレイを始めた時のシーンの JSON へも写す。編集復帰の組み直しを跨いで調整値が残る
    //! 写した欄は履歴へ 1 つずつ積まず、編集へ戻る時にプレイ開始の写しと比べて 1 まとまりで積む
    //! 編集モードでは実体が唯一の出所なので何もしない。Inspector の編集箇所が呼ぶ
    //! row は actor のクラスの表の行。JSON に無いアクター、作られていない行、存在しない欄、.actor へ書く欄は写さない
    void MirrorPlayEditToBaseline(const NS::SceneActor* actor, const NS::ClassObjInfo& row, std::string_view fieldName);

    //! プレイを始めた時のシーンの JSON。プレイ中の手編集を写した後の姿で、編集へ戻る時にこれでシーンを組み直す
    //! プレイ中でない時に呼ぶと NS_ASSERT で止まる
    [[nodiscard]] const nlohmann::json& PlayBaseline() const;

    //! Inspector で actor の欄の値が変わったフレームに呼ぶ。row は actor のクラスの表の行
    //! アクターへ書く欄はプレイ中の凍結へ写す。.actor へ書く欄は覚えておき、
    //! CommitClassObjEdit で .actor のファイルへ書いて、同じ .actor のアクターの全部へ写す。プレイ中も同じ
    void NoteFieldEdit(const NS::SceneActor* actor, const NS::ClassObjInfo& row, std::string_view fieldName);

    //! アクターの欄を書いた後の後始末。欄を変える・既定へ戻す・.actor の欄を直す・マテリアルを付ける の全部が呼ぶ
    //! 素材の欄を書いた時だけ、描くメッシュ・描くマテリアル・メッシュの当たりの形を読み直す
    //! 当たりの姿と形は、世界を止めた歩とプレイの歩が揃えるので、ここでは張り直さない。writtenType は書いた欄の型
    void FinishFieldWrite(NS::SceneActor* actor, NS::FieldType writtenType);

    //! 編集視点の中心付近へ配置し、選択と Undo 履歴へ追加する
    void PlaceItem(const NS::Editor::PlacementItem& item);
    [[nodiscard]] NS::Editor::StageBuildTool& StageBuilder() noexcept { return m_stageBuilder; }
    // 作成道具を保って1回の履歴で置く。プレイ中や配置失敗では k_NoActorId。
    [[nodiscard]] NS::uint32 PlaceStagePart(const NS::Ray& ray);

    //! メッシュ資産を地形の部品として編集視点の中心あたりへ置く。Undo 対応
    //! @param[in] meshPath 資産ファイルの絶対パス。参照は ContentRoot 相対へ直して持つ
    void AddMeshParts(std::string_view meshPath);

    //! 選択中のアクターに対応する runtime SceneActor。未選択 / 未構築は nullptr
    [[nodiscard]] NS::SceneActor* SelectedActor() noexcept;
    //! 選択中のアクターがプレイヤー実体か
    [[nodiscard]] bool SelectedIsPlayerActor() const noexcept;

    //! 改名して Undo 履歴へ追加する。不在なら何もしない。重複には番号を付け、空名は Object にする
    void RenameActor(NS::uint32 id, std::string_view name);

    //! 選択中のアクターを新しい永続 id で複製して undo へ積む。プレイヤーは複製の対象から外す
    void DuplicateSelectedActor();

    //! 選択中のアクターを削除して取り消しの履歴へ 1 単位で積む
    //! プレイヤーを含む選択の場合は何もしない
    void DeleteSelectedActor();

    //! 編集カメラの注視点を選択中のアクターへ寄せる。広がりに応じて距離も取り直す
    void FocusSelectedInView() noexcept;

    //! Object ツール中にギズモがアクターを選択しているか
    [[nodiscard]] bool HasGizmoSelection() const noexcept
    {
        return GizmoToolActive() && m_gizmo.Selected() != nullptr;
    }
    //! 選択中のアクターの Model に材質を割り当てて undo へ積む。対象外なら false
    bool ApplyMaterialToSelected(std::string_view matPath);

    //! 素材の窓で押したマテリアル・メッシュ・画像・.actor を選ぶ。アクターの選択は外れる
    //! 道は ContentRoot からの道で覚え、SelectedAsset が引くたびに AssetManager から引く
    //! 読めない素材は選ばない。選ぶ前に、前の素材の確定していない編集を確定する
    //! @param[in] assetPath 素材のファイルの絶対パス
    void SelectAsset(std::string_view assetPath);
    //! 選んだ素材の ContentRoot からの道。選んでいなければ空
    [[nodiscard]] const std::string& SelectedAssetPath() const noexcept { return m_selectedAssetPath; }
    //! 素材の窓の行が選んだ素材か。assetPath は素材のファイルの絶対パス
    [[nodiscard]] bool IsAssetSelected(std::string_view assetPath) const;
    //! 選んだ素材 (NS::Material・NS::Mesh・NS::Texture、.actor ならそれを敷いた NS::SceneActor)。選んでいなければ nullptr
    //! .actor の 1 体はインスペクタで直すための物で、シーンへ入らない。
    //! .actor の文書が変わると (確定・取り消し) 次に引いた時に作り直すので、引いたフレームの間だけ使う
    [[nodiscard]] NS::Object* SelectedAsset();
    //! Inspector で選んだ素材の値が変わったフレームに呼ぶ
    void MarkAssetEdited() noexcept;
    void MarkEffectSetEdited(const nlohmann::json& before);
    //! Inspector で選んだ .actor の 1 体の欄の値が変わったフレームに呼ぶ。row はその 1 体のクラスの表の行
    //! NS_GLOBAL_FIELD の欄も .actor の値を直す。参照の欄は .actor が持たないので覚えない
    void NoteActorFileFieldEdit(const NS::ClassObjInfo& row, std::string_view fieldName);
    //! 値が変わった後の確定で、選んだ素材のファイルへ書く
    //! .mat はマテリアルを作り直して書き、作り直せなくても値は書いて前の絵で描き続ける。
    //! .actor は直した欄を書き、シーンの同じ .actor のアクターの全部へ写して、取り消しの履歴へ積む。
    //! 値が変わっていなければ何もしない
    void CommitAssetEdit();

    //! actorFileName の .actor の名前の束を sets に替えてファイルへ書き、取り消しの履歴へ積む
    //! シーンの同じ .actor のアクターの全部が束を読み直すので、次の当たりから効く。プレイ中も同じ。
    //! .actor が読まれていない時は WARN を出して書かない。束が変わっていなければ何もしない
    void WriteActorFileHitReactions(std::string_view actorFileName, const std::vector<NS::HitReactionSet>& sets);

    //! actorFileName の .actor の hitSensors を sensors の今の値に替えてファイルへ書き、取り消しの履歴へ積む
    //! シーンの同じ .actor のアクターの全部がセンサーの値を読み直す。プレイ中も同じ。
    //! .actor が読まれていない時は WARN を出して書かない。値が変わっていなければ何もしない
    void WriteActorFileHitSensors(std::string_view actorFileName, const NS::HitSensorsObj& sensors);

    //! baseName の .actor から、name の派生の .actor を作ってファイルへ書く
    //! 何も書いていない派生で、baseName が元ならそれを、派生ならその元を指す。派生が書いた項目は写さない。
    //! name が空・ファイルの名前に使えない字を含む・既にある時と、baseName が読まれていない時は WARN を出して作らない。
    //! ファイルを作る事は取り消しの履歴へ積まない
    //! @return 作れた場合 true、それ以外の場合は false
    bool CreateDerivedActorFile(std::string_view baseName, std::string_view name);

    //! actorId のアクターのシーンの上書きのうち派生に書ける項目を、name の派生の .actor へ移し、アクターをその派生に切り替える
    //! 派生は CreateDerivedActorFile と同じ形で作るので、移るのはシーンの上書きだけ。
    //! アクターが派生を指していれば、その派生が書いた値は元の値に戻る。位置は変わらない。
    //! アクターの切り替えは取り消しの履歴へ積み、ファイルは取り消しても残す。編集モードでない時と、作れない時は WARN を出して何もしない
    //! @return 切り替えた場合 true、それ以外の場合は false
    bool DeriveActorFileFromActor(NS::uint32 actorId, std::string_view name);

private:
    //! actorFileName の .actor の文書に change を当て、変わっていればファイルへ書いて取り消しの履歴へ積む
    //! .actor が読まれていない時は WARN を出して書かない。what は WARN に出す書く物の名前
    void PushActorFileChange(std::string_view actorFileName,
                             const std::function<void(nlohmann::json&)>& change,
                             std::string_view what);

    void TickEdit(const NS::OS::InputPresses& presses);
    void FocusLoadedScene() noexcept;

    //! プレイ中のカーソルを置き直す
    //! Captured は消して Game の画像の中心へ固定し、マウスを相対にする。Released はその逆
    void ApplyPlayCursor(NS::Editor::PlayCursor cursor) noexcept;

    //! 世界を動かし直す前に、自機がまだ読んでいない押しを捨てる
    //! 捨てるのは自機の読み手だけ。描画の中のツールバーから呼ばれても、エディタがまだ読んでいない押しは残る
    void DiscardPlayerPresses() noexcept;

    //! プレイを終えて編集の姿へ戻す。凍結スナップショットから世界を組み直し、操作系を休止させ、カーソルを出す
    //! 編集モードでしか要らない遷移なのでエディタが持つ。組み直しで確保が起きるため noexcept にしない
    void LeavePlayForEdit();

    //! アクターを 1 体作り、作った物を選ぶ。置く道が使う。作るのは EditorMode::CreateActors
    void CreateActorAndSelect(nlohmann::json object);

    //! 素材の選択を外す。前に、選んだ素材の確定していない編集を確定して .mat に書く
    //! 選択を外す道は全部これを通す。外してから確定すると、書く先の道が消えて編集を失う
    void ClearAssetSelection();

    void RenderCameraGizmos(NS::Gfx::DebugShapes& shapes,
                            const NS::Matrix& viewProjection,
                            NS::Size2D viewport) noexcept;
    //! 当たり形状を積む。all が true なら全アクター、false なら選択分だけ
    void RenderCollisionWireframes(NS::Gfx::DebugShapes& shapes, bool all) noexcept;

    //! 判定と同じ面と命中点を描く。狙い対象は狙いの方向、他は自機から相手への水平方向を使う
    //! 自機がなければ編集カメラからの水平方向を使う
    void RenderHitFaces(NS::Gfx::DebugShapes& shapes) noexcept;

    //! 主対象以外の選択物を枠で見せる。ギズモは 1 体にしか出ないので、選んだ範囲を目で追えるようにする
    void RenderSelectionOutlines(NS::Gfx::DebugShapes& shapes) noexcept;
    void RefreshGizmoSelectables();
    void ResolveSelectionFromId() noexcept;

    //! 主対象だけを差し替える。選択集合には触らない
    void SetPrimarySelection(NS::uint32 id) noexcept;

    //! ドラッグ開始時に、主対象以外の選択物の世界変換を控える
    void CaptureDragFollowers() noexcept;
    //! 主対象がドラッグで動いた分を、控えた残りの選択へ同じだけ効かせる
    void ApplyDragToFollowers() noexcept;

    [[nodiscard]] NS::ThirdPersonFollow* SelectedFollowCamera() noexcept;
    void SyncFollowCameraPoses();
    void ApplyFollowCameraGizmoDrag();
    void CaptureSelectionFromGizmo() noexcept;

    [[nodiscard]] NS::CameraManager* Cameras() const noexcept;
    [[nodiscard]] NS::SceneCamera* MainCamera() const noexcept;

    NS::Scene* m_scene = nullptr;                // 編集対象のシーン。回す/止める/コマ送りもこのシーンのスイッチ
    NS::Editor::ActorSnapshotApplier m_applier; // 履歴の操作を実体へ適用する

    // 前面のパネルの表示矩形。未設定時は CurrentViewRect が全画面の予備矩形を返す
    NS::Editor::ViewRect m_gameViewRect{};
    bool m_gameViewRectValid = false;
    bool m_gameViewHovered = false;
    bool m_gameViewHidden = false; // UI 表示中に前面のパネルが裏へ隠れているか

    NS::Editor::EditorCamera m_editorCamera;

    // 編集復帰時にプレイ視点から自由視点へ繋ぐブレンド。TickEdit が進める
    NS::CameraPose m_editBlendFrom{};
    float m_editBlendElapsed = 0.0f;
    bool m_editBlending = false;

    NS::Editor::EditorMode m_editor{};
    NS::Editor::StageBuildTool m_stageBuilder;
    Mode m_mode = Mode::Edit;

    EditTool m_editTool = EditTool::Gizmo;
    std::optional<NS::Vector3> m_stagePlacement;
    void TickStageBuild(const NS::OS::InputPresses& presses);

    NS::Editor::GizmoEditor m_gizmo{};

    std::vector<NS::SceneActor*> m_selectablePtrs;
    std::vector<NS::uint8> m_selectablePickable; // 1 は Model を持つアクター。ギズモが先に選ぶ

    NS::uint32 m_selectedActorId = NS::k_NoActorId; // 主対象の永続 id。選択の一次情報
    std::vector<NS::uint32> m_selectionIds;           // 選択中の全アクター。主対象も含む
    NS::Transform* m_lastGizmoSelected = nullptr;     // 前フレームの選択対象

    //! ドラッグ開始時点の姿。主対象の動きを同じだけ他へ流すための控え
    struct DragFollower
    {
        NS::uint32 id = 0;
        NS::Matrix world{};
    };
    std::vector<DragFollower> m_dragFollowers; // 主対象に付いて動く残りの選択
    NS::Matrix m_dragPrimaryWorld{};           // ドラッグ開始時の主対象の世界変換

    bool m_gizmoWasDragging = false;
    bool m_transformEditing = false;

    // 変形を始めた時の 1 体の控え
    struct TransformEditBaseline
    {
        NS::uint32 id = NS::k_NoActorId;
        nlohmann::json snapshot; // 履歴に積む前の姿。保存と同じ形で、上書きの一覧の項目だけを持つ
        nlohmann::json fields;   // 全部の部品の全部の欄の値。確定の時に比べ、変わった項目を上書きの一覧に足す
    };
    // 選択している分だけ並ぶ
    std::vector<TransformEditBaseline> m_editBaselines;

    bool m_classObjEditing = false;
    NS::uint32 m_classObjEditBaselineId = NS::k_NoActorId; // 編集開始時の対象 id
    nlohmann::json m_classObjEditBaseline;                  // 編集開始時の状態スナップショット

    // プレイを始めた時のシーンの JSON。プレイ中の手編集を写していき、編集へ戻る時にこれでシーンを組み直す
    // プレイに入っていない間は空
    std::optional<nlohmann::json> m_playBaseline;
    // m_playBaseline の、手編集を写す前の写し。編集へ戻る時に、プレイ中に写した欄を見つけて履歴へ積むために比べる
    // プレイに入っていない間は空
    std::optional<nlohmann::json> m_playStartBaseline;

    //! 直したが、まだ .actor へ書いていない欄。CommitClassObjEdit・CommitAssetEdit で書いて空にする
    struct ActorFileEdit
    {
        NS::uint32 actorId = NS::k_NoActorId; // k_NoActorId なら素材の窓で選んだ .actor の 1 体
        const NS::ClassObjInfo* row = nullptr;
        std::string fieldName;
    };
    std::vector<ActorFileEdit> m_actorFileEdits;

    //! m_actorFileEdits を .actor へ書き、1 欄ずつ取り消しの履歴へ積む
    void CommitActorFileEdits();

    // 素材の窓で選んだ素材の ContentRoot からの道。アクターの選択と排他。
    // 姿のポインタを持たないのは、AssetManager::Clear の後に古いポインタを残さないため
    std::string m_selectedAssetPath;
    nlohmann::json m_effectSetEditBefore;
    bool m_assetEdited = false; // 選んだ素材の値が変わり、まだ確定していない

    bool m_editShortcutsBlocked = false;
    // Tick の頭で固定ステップごとに読む。編集の部品は、ここで読んだ押しを引数で受ける
    NS::OS::InputReader m_inputReader;

    //! 選んだ .actor の値で作った 1 体。素材の窓で直す値はこの 1 体に入れ、確定の時に .actor へ書く
    std::unique_ptr<NS::SceneActor> m_actorFileDraft;
    nlohmann::json m_actorFileDraftSource; // m_actorFileDraft を作った時の .actor の文書

    //! 選んだ素材が .actor なら、今の文書を敷いた 1 体。文書が変わっていれば作り直す。.actor でなければ nullptr
    [[nodiscard]] NS::SceneActor* SelectedActorFile();
};

namespace NS
{
    namespace Editor
    {
        //! material の値を、一時のファイルに書いてから filePath の .mat と置き換える
        //! .mat から読んだマテリアルでない時と、書けない値の時は書かずに false
        //! 書けなかった時は filePath の前の中身が残る
        [[nodiscard]] bool WriteMaterialFile(const AssetManager& assets, const Material& material, std::string_view filePath);
    } // namespace Editor
} // namespace NS
