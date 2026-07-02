#include "Editor/LevelEditorController.h"

#include "Editor/EditorCameraRig.h"
#include "Game/LevelPlayScene.h"
#include "Game/Player.h"

#include "Editor/ComponentClipboard.h"
#include "Editor/Undo/AddComponentCommand.h"
#include "Editor/Undo/AddObjectCommand.h"
#include "Editor/Undo/DuplicateObjectCommand.h"
#include "Editor/Undo/RemoveComponentCommand.h"
#include "Editor/Undo/TransformCommand.h"
#include "Framework/App/Application.h"
#include "Framework/Core/Filesystem.h"
#include "Framework/Graphics/DebugDraw.h"
#include "Framework/Physics/SweptOBB.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Keyboard.h"
#include "Framework/Platform/Window.h"
#include "Framework/Scene/AssetManager.h"
#include "Framework/Scene/Component.h"
#include "Framework/Scene/Components/BoxColliderComponent.h"
#include "Framework/Scene/Components/CameraBrainComponent.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"
#include "Framework/Scene/Components/PlacedVirtualCamera.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Transform.h"
#include "Framework/UI/ImGuiContext.h"
#include "Game/Blocks/BuildPlacedObject.h"
#include "Game/Level/LevelData.h"

#include <cstring>
#include <memory>
#include <string>

namespace
{
    constexpr NS::Math::Vector3 kCellHalfExtents{0.5f, 0.5f, 0.5f};
} // namespace

LevelEditorController::LevelEditorController(LevelPlayScene* scene) noexcept : m_scene(scene) {}

LevelEditorController::~LevelEditorController() = default;

NS::Game::Level::LevelData& LevelEditorController::Level() noexcept
{
    return m_scene->Level();
}

NS::Game::Level::PlayState& LevelEditorController::Play() noexcept
{
    return m_scene->Play();
}

void LevelEditorController::Setup(NS::UI::ImGuiContext* imgui)
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr || m_scene == nullptr)
        return;

    m_imgui = imgui;

    // 編集モード専用の free-fly カメラを Player / follow camera と並列で立ち上げる
    // MB64 の freecam に相当し mouse + gamepad で Orbit / Pan / Zoom する
    m_editorCameraRig = std::make_unique<EditorCameraRig>();
    m_editorCameraRig->AttachScene(m_scene);

    // free-fly vcam の投影設定で、 編集は遠景を 5000 まで見せ near 0.1 は既定
    // far は EditorCameraComponent の kMaxDistance より広く取り、 最大ズームアウトでも地形を映す
    m_editorCameraRig->EditorCam().SetNearPlane(0.1f);
    m_editorCameraRig->EditorCam().SetFarPlane(5000.0f);
    m_editorCameraRig->EditorCam().SetFovY(NS::Math::ToRadians(NS::Math::Degrees{60.0f}));

    // 初期視点は spawn 位置を中心に少し引いた位置から見下ろす
    const NS::Math::Vector3 spawnPos{static_cast<float>(m_scene->Level().spawnX),
                                     static_cast<float>(m_scene->Level().spawnY),
                                     static_cast<float>(m_scene->Level().spawnZ)};
    m_editorCameraRig->EditorCam().SetCenter(spawnPos);
    // 起動直後は spawn block を真ん中近めに見せる距離。 1m cube が画面の十数 % を占める
    m_editorCameraRig->EditorCam().SetDistance(5.0f);

    m_editorCameraRig->OnStart();

    // free-fly vcam を Brain へ登録する。 follow / area camera は scene が登録済
    if (m_scene->m_brain)
        m_scene->m_brain->AddVirtualCamera(&m_editorCameraRig->EditorCam());

    // EditorMode に依存先を注入する
    m_editor.SetLevel(&m_scene->Level());
    m_editor.SetEditIds(&m_scene->World().EditIds(), &m_scene->World().NextEditId());
    m_editor.SetInput(&app->Input());
    m_editor.SetImGui(imgui);
    m_editor.SetCameraComponent(m_scene->m_mainCamera);
    m_editor.SetActive(true);
    // scene が OnStart で rebuild 済なので、 初回 Tick の二重 rebuild を抑制
    m_editor.ClearLevelDirty();

    // ギズモに依存先を注入する。 選択候補は自由オブジェクト + grid solid ブロックを連結して渡す
    m_gizmo.SetInput(&app->Input());
    m_gizmo.SetImGui(imgui);
    RefreshGizmoSelectables();

    // scene は OnStart でプレイ開始済。 player 凍結 / free-fly camera 有効の編集モードへ切替える
    m_scene->SetPlaying(false);
    m_editorCameraRig->EditorCam().SetActive(true);
    m_mode = Mode::Edit;
}

void LevelEditorController::Teardown()
{
    // ギズモは free オブジェクトの Transform を非所有参照するので、 scene 破棄前に選択を外す
    m_gizmo.ClearSelection();
    if (m_editorCameraRig)
    {
        // Brain は free-fly vcam を非所有参照する。 vcam を畳む前に Brain から外して dangling を避ける
        if (m_scene != nullptr && m_scene->m_brain)
            m_scene->m_brain->RemoveVirtualCamera(&m_editorCameraRig->EditorCam());
        m_editorCameraRig->OnEndPlay();
    }
    m_editorCameraRig.reset();
    m_selectablePtrs.clear();
    m_selectableHalfExtents.clear();
}

void LevelEditorController::EnterPlay() noexcept
{
    if (m_mode == Mode::Play)
        return;
    m_mode = Mode::Play;
    // player spawn / 物理 / follow camera は scene が握る
    m_scene->SetPlaying(true);
    // SetPlaying(true) の rebuild を跨いでも生ポインタが残らないよう、 候補と選択を実体へ解決し直す
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
    m_editor.SetActive(false);
    if (m_editorCameraRig)
        m_editorCameraRig->EditorCam().SetActive(false);
}

void LevelEditorController::EnterEdit() noexcept
{
    if (m_mode == Mode::Edit)
        return;
    m_mode = Mode::Edit;
    // player 凍結 / follow・area camera 休止 / play 状態リセットは scene が握る
    m_scene->SetPlaying(false);
    // Play 中の rebuild を跨いだ選択を、 id から現在の実体へ貼り直してから編集へ戻る
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
    m_editor.SetActive(true);
    if (m_editorCameraRig)
        m_editorCameraRig->EditorCam().SetActive(true);
}

void LevelEditorController::Tick()
{
    if (m_mode == Mode::Edit)
    {
        TickEdit();
    }
    else if (m_scene->Play().clearTriggered)
    {
        // クリア成立で編集へ戻す。 出荷にはこの controller が無いためクリア演出は別途必要
        EnterEdit();
    }
}

void LevelEditorController::TickEdit()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;

    // Esc: Object モードで選択中ならまず選択解除に使い終了させない
    if (app->Input().Keyboard().IsPressed(NS::Platform::Key::Escape))
    {
        if (m_editorToolMode == EditorToolMode::Object && m_gizmo.Selected() != nullptr)
        {
            m_gizmo.ClearSelection();
            // Player を掴んでいた場合も特殊選択を解除し、 次フレームの再貼り付けで掴み続けないようにする
            m_specialSelection = SpecialSelection::None;
            return;
        }
        NS::App::Application::Quit();
        return;
    }

    // free-fly camera を駆動
    if (m_editorCameraRig)
    {
        m_editorCameraRig->Root().Snapshot();
        m_editorCameraRig->OnUpdate();
    }

    // free-fly 更新後に実カメラへ反映し、 ギズモ / 編集の ray-pick が当フレームの視点を使えるようにする
    if (m_scene->m_brain)
        m_scene->m_brain->Evaluate(1.0f);

    // 同時に 1 モードだけが LMB/R/Ctrl+Z を消費する。 Object 中は grid 入力を抑制しギズモへ回す
    // モード切替は EditorLayer の UI ボタン SetObjectToolActive から行う。 Tab は Edit↔Play 専用
    const bool objectMode = (m_editorToolMode == EditorToolMode::Object);
    m_gizmo.SetActive(objectMode);
    m_editor.SetInputSuppressed(objectMode);

    if (objectMode && m_editorCameraRig && m_scene->m_brain)
    {
        // 毎フレーム live な scene から候補 span を作り直し、 選択を id → 実体へ解決し直す
        // Play 突入 / undo / promote の rebuild を跨いでも生ポインタが残らない fail-safe の要
        RefreshGizmoSelectables();
        ResolveSelectionFromId();

        const auto vp = m_scene->m_brain->ViewProjection();
        const auto viewport = app->Window().Size();
        const bool wasDragging = m_gizmoWasDragging;
        m_gizmo.Tick(vp, viewport);

        // 掴んだら自由化: 選択が grid solid なら自由 Transform オブジェクトへ昇格する。 free はそのまま gizmo 変形
        if (NS::Scene::Transform* selected = m_gizmo.Selected())
        {
            for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
            {
                if (&m_scene->World().Objects()[i]->Root() != selected)
                    continue;
                const std::size_t objectIndex = m_scene->World().SourceIndices()[i];
                const NS::Game::Level::ObjectInstance& entry = m_scene->Level().objects[objectIndex];
                if (NS::Game::Blocks::IsGridSolidObject(entry))
                    PromoteGridBlockToFree(objectIndex);
                break;
            }
        }

        // ビューポートでのギズモ選択変化を選択 id と Inspector が見る派生添字へ追従させる
        CaptureSelectionFromGizmo();

        // ギズモ変形の結果を live → model で ObjectInstance へ反映する。 begin/commit はこの model を基準にする
        SyncFreeObjectTransforms();
        // Player 選択中は実プレイヤー Transform ↔ spawn を同期する
        SyncPlayerSpawnFromTransform();

        // ドラッグ開始で baseline 退避、 終了で TransformCommand を 1 つ確定する。 grid undo と同じ履歴
        const bool nowDragging = m_gizmo.IsDragging();
        if (!wasDragging && nowDragging)
            BeginTransformEdit();
        else if (wasDragging && !nowDragging)
            CommitTransformEdit();
        m_gizmoWasDragging = nowDragging;
    }
    else if (m_transformEditing)
    {
        // Object モードを抜けても未確定の変形があれば確定し、 記録されない変更を残さない
        CommitTransformEdit();
        m_gizmoWasDragging = false;
    }

    m_editor.Tick();
    if (m_editor.IsLevelDirty())
    {
        m_scene->RebuildWorld();
        // ファイル読込で cameraVolumes が差し替わった場合に area camera を追従させる
        m_scene->RebuildAreaCameras();
        // undo / redo / ロードは objects を作り直す。 ロードは id が振り直され旧 id が別物に化けるため、
        // ここで選択 id を解除する。 候補 span と gizmo の貼り直しは次フレーム頭の解決に委ねる
        m_selectedObjectId = NS::Game::Level::kInvalidObjectId;
        m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
        m_gizmo.ClearSelection();
        m_lastGizmoSelected = nullptr;
        m_editor.ClearLevelDirty();
    }
}

void LevelEditorController::Render()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;

    // Debug provenance パネルの入力を毎フレーム退避する。出所は has_value の突き合わせで逆算するので
    // Resolve のホットパスに追跡を入れず、 scene の解決値と代表 object override をそのまま保持する
    m_debugResolvedSettings = m_scene->m_lastResolvedSettings;
    m_debugSceneOverride = m_scene->BuildSceneOverride();
    if (m_scene->m_player)
        m_debugPlayerObjectOverride = m_scene->m_player->MeshComp().RenderOverride();

    if (m_mode != Mode::Edit)
        return;

    m_editor.RenderCursorPreview();
    RenderAreaCameraGizmos();
    RenderColliderWireframes();
    // 蓄積した DebugDraw 線をシーン描画後・ ImGui 前にまとめて 1 描画する
    if (m_scene->m_brain)
        NS::Graphics::DebugDraw::Flush(app->Renderer(), m_scene->m_brain->ViewProjection());
    // Toolbar UI を ImGui 経由で描画する。 Debug / Development build のみ実機能
    m_editor.Palette().Render();
    // Object モードのギズモは最前面の drawlist に重ねる
    if (m_gizmo.IsActive() && m_scene->m_brain)
        m_gizmo.Render(m_scene->m_brain->ViewProjection(), app->Window().Size());
}

void LevelEditorController::SetObjectToolActive(bool active) noexcept
{
    const EditorToolMode next = active ? EditorToolMode::Object : EditorToolMode::Build;
    if (next == m_editorToolMode)
        return;
    m_editorToolMode = next;
    if (!active)
    {
        m_gizmo.ClearSelection();
        m_selectedObjectId = NS::Game::Level::kInvalidObjectId;
        m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
        m_lastGizmoSelected = nullptr;
    }
}

bool LevelEditorController::HasInspectableSelection() const noexcept
{
    return m_selectedObjectIndex < m_scene->Level().objects.size();
}

bool LevelEditorController::SelectedIsGridAligned() const noexcept
{
    return HasInspectableSelection() &&
           (m_scene->Level().objects[m_selectedObjectIndex].flags & NS::Game::Level::kObjectFlagGridAligned) != 0;
}

NS::Game::Level::ObjectInstance LevelEditorController::SelectedObjectSnapshot() const noexcept
{
    if (m_selectedObjectIndex < m_scene->Level().objects.size())
        return m_scene->Level().objects[m_selectedObjectIndex];
    return NS::Game::Level::ObjectInstance{};
}

NS::Scene::GameObject* LevelEditorController::SelectedObjectGameObject() noexcept
{
    if (m_selectedObjectIndex >= m_scene->Level().objects.size())
        return nullptr;

    // objects 添字 → runtime インスタンスの逆引き。 free / grid の別は ObjectInstance が握り runtime list は 1 本
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
        if (m_scene->World().SourceIndices()[i] == m_selectedObjectIndex)
            return m_scene->World().Objects()[i].get();
    return nullptr;
}

NS::Scene::GameObject* LevelEditorController::PlayerObject() noexcept
{
    return m_scene->m_player.get();
}

void LevelEditorController::SyncSelectedObjectComponentsFromComponent()
{
    if (m_selectedObjectIndex >= m_scene->Level().objects.size())
        return;

    NS::Game::Level::ObjectInstance& object = m_scene->Level().objects[m_selectedObjectIndex];
    // grid は cell 固定で編集しない。 自由配置物のみ書き戻す
    if ((object.flags & NS::Game::Level::kObjectFlagGridAligned) != 0)
        return;

    // 反射編集で更新済の runtime コンポーネントを components データへ書き戻す。 BuildFromComponents が読むのは
    // components 側で、 ここを更新しないと次の rebuild で編集が失われる
    if (NS::Scene::GameObject* go = SelectedObjectGameObject())
        NS::Editor::WriteBackComponentEdits(*go, object);
}

NS::Scene::GameObject* LevelEditorController::CameraBrainObject() noexcept
{
    return m_scene->m_brain ? m_scene->m_brain->Owner() : nullptr;
}

NS::Scene::GameObject* LevelEditorController::ActiveVirtualCameraObject() noexcept
{
    if (m_scene->m_brain == nullptr)
        return nullptr;
    NS::Scene::VirtualCameraComponent* active = m_scene->m_brain->ActiveVirtualCamera();
    return active ? active->Owner() : nullptr;
}

bool LevelEditorController::HasCameraSelection() const noexcept
{
    return m_selectedCameraIndex < m_scene->Level().cameraVolumes.size();
}

void LevelEditorController::RefreshGizmoSelectables()
{
    m_selectablePtrs.clear();
    m_selectableHalfExtents.clear();
    m_selectablePtrs.reserve(m_scene->World().Objects().size());
    m_selectableHalfExtents.reserve(m_scene->World().Objects().size());

    // free / grid の別は ObjectInstance の flags で決まる。 runtime list は 1 本
    // 自由配置物を先に積む。 pick OBB は Root().WorldMatrix() が scale 込みで持ち、 判定は
    // 逆変換した unit ローカル空間で行う。 ここで halfExtents に scale を乗せると二重適用になり、
    // 拡大した配置物の判定箱が scale^2 に膨らんで近くの grid クリックを先に奪うので unit のまま渡す
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        const NS::Game::Level::ObjectInstance& entry = m_scene->Level().objects[m_scene->World().SourceIndices()[i]];
        if ((entry.flags & NS::Game::Level::kObjectFlagGridAligned) != 0)
            continue;
        m_selectablePtrs.push_back(m_scene->World().Objects()[i].get());
        m_selectableHalfExtents.push_back(kCellHalfExtents);
    }

    // grid solid も掴める。 掴むと PromoteGridBlockToFree で自由オブジェクトに変わる。 slope 等は対象外
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        const NS::Game::Level::ObjectInstance& entry = m_scene->Level().objects[m_scene->World().SourceIndices()[i]];
        if (!NS::Game::Blocks::IsGridSolidObject(entry))
            continue;
        m_selectablePtrs.push_back(m_scene->World().Objects()[i].get());
        m_selectableHalfExtents.push_back(kCellHalfExtents);
    }

    // 実プレイヤーも掴める。 objects には属さないが、 ビューポート直クリックを Player 選択へ流すため候補に積む
    // pick OBB は player の cube mesh と同じ unit 半径。 world scale 0.8/1.8/0.8 は Root().WorldMatrix() が持つ
    if (m_scene->m_player)
    {
        m_selectablePtrs.push_back(m_scene->m_player.get());
        m_selectableHalfExtents.push_back(kCellHalfExtents);
    }

    m_gizmo.SetSelectableObjects(m_selectablePtrs, m_selectableHalfExtents);
}

void LevelEditorController::SyncFreeObjectTransforms()
{
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        const std::size_t objectIndex = m_scene->World().SourceIndices()[i];
        if (objectIndex >= m_scene->Level().objects.size())
            continue;
        NS::Game::Level::ObjectInstance& object = m_scene->Level().objects[objectIndex];
        // grid は cell 固定なので Transform を ObjectInstance へ書き戻さない。 自由配置物のみ
        if ((object.flags & NS::Game::Level::kObjectFlagGridAligned) != 0)
            continue;

        const NS::Scene::Transform& root = m_scene->World().Objects()[i]->Root();
        const NS::Math::Vector3 position = root.Position();
        const NS::Math::Quaternion rotation = root.Rotation();
        const NS::Math::Vector3 scale = root.Scale();

        object.positionX = position.x;
        object.positionY = position.y;
        object.positionZ = position.z;
        object.rotationX = rotation.x;
        object.rotationY = rotation.y;
        object.rotationZ = rotation.z;
        object.rotationW = rotation.w;
        object.scaleX = scale.x;
        object.scaleY = scale.y;
        object.scaleZ = scale.z;
    }
}

NS::Editor::SetSpawnCommand::SpawnState LevelEditorController::CurrentSpawnState() const noexcept
{
    const NS::Game::Level::LevelData& level = m_scene->Level();
    return NS::Editor::SetSpawnCommand::SpawnState{level.spawnX,
                                                   level.spawnY,
                                                   level.spawnZ,
                                                   level.spawnRotationX,
                                                   level.spawnRotationY,
                                                   level.spawnRotationZ,
                                                   level.spawnRotationW};
}

void LevelEditorController::SyncPlayerSpawnFromTransform() noexcept
{
    if (m_specialSelection != SpecialSelection::Player || !m_scene->m_player)
        return;

    NS::Scene::Transform& root = m_scene->m_player->Root();
    const bool drivingPlayer = m_gizmo.IsDragging() && m_gizmo.Selected() == &root;
    if (drivingPlayer)
    {
        // ギズモが実プレイヤーを掴んでいる間は Transform を真実として spawn を追従させる
        const NS::Math::Vector3 p = root.Position();
        const NS::Math::Quaternion r = root.Rotation();
        m_scene->Level().spawnX = p.x;
        m_scene->Level().spawnY = p.y;
        m_scene->Level().spawnZ = p.z;
        m_scene->Level().spawnRotationX = r.x;
        m_scene->Level().spawnRotationY = r.y;
        m_scene->Level().spawnRotationZ = r.z;
        m_scene->Level().spawnRotationW = r.w;
    }
    else
    {
        // 非ドラッグ時は spawn を真実として実プレイヤーをそこへ置く。 undo / redo の spawn 変化もこれで反映する
        const NS::Game::Level::LevelData& level = m_scene->Level();
        root.SetPosition({level.spawnX, level.spawnY, level.spawnZ});
        root.SetRotation(NS::Math::Quaternion{
            level.spawnRotationX, level.spawnRotationY, level.spawnRotationZ, level.spawnRotationW});
    }
    // edit 中の実プレイヤーは scene が Snapshot しないため、 ここで previous=current に揃え補間ジッタを消す
    root.Snapshot();
}

void LevelEditorController::SelectObjectByIndex(std::size_t index) noexcept
{
    // オブジェクトとカメラ / Player / Camera の選択は排他。 オブジェクトを選んだら他を解除する
    m_selectedCameraIndex = NS::Game::Level::kNoObjectIndex;
    m_specialSelection = SpecialSelection::None;

    if (index >= m_scene->Level().objects.size())
    {
        m_selectedObjectId = NS::Game::Level::kInvalidObjectId;
        m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
        return;
    }
    // 選択の真実は id。 索引が動いても id から引き直せる
    m_selectedObjectId = m_scene->World().EditIds()[index];
    m_selectedObjectIndex = index;

    // ハンドルを出すため Object ツールへ切替える。 Build のままだとギズモが描かれない
    SetObjectToolActive(true);

    // 自由オブジェクトだけギズモへ貼る。 solid 含む grid 配置物は一覧クリックでは昇格させず、
    // Inspector の Promote で明示的に自由化し、 一覧での選択を非破壊に保つ
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        if (m_scene->World().SourceIndices()[i] != index)
            continue;
        if ((m_scene->Level().objects[index].flags & NS::Game::Level::kObjectFlagGridAligned) != 0)
            break;
        m_gizmo.SetSelected(&m_scene->World().Objects()[i]->Root());
        m_lastGizmoSelected = m_gizmo.Selected();
        return;
    }
    m_gizmo.ClearSelection();
    m_lastGizmoSelected = nullptr;
}

void LevelEditorController::SelectCameraByIndex(std::size_t index) noexcept
{
    m_specialSelection = SpecialSelection::None;
    if (index >= m_scene->Level().cameraVolumes.size())
    {
        m_selectedCameraIndex = NS::Game::Level::kNoObjectIndex;
        return;
    }
    m_selectedCameraIndex = index;

    // カメラ選択中はオブジェクト / ギズモ選択を外す。 Inspector はカメラを表示する
    m_selectedObjectId = NS::Game::Level::kInvalidObjectId;
    m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
    m_gizmo.ClearSelection();
    m_lastGizmoSelected = nullptr;
}

void LevelEditorController::SelectPlayer() noexcept
{
    // 添字 / カメラ選択を外して特殊選択へ移す
    m_selectedObjectId = NS::Game::Level::kInvalidObjectId;
    m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
    m_selectedCameraIndex = NS::Game::Level::kNoObjectIndex;
    m_specialSelection = SpecialSelection::Player;

    // 実プレイヤーを掴んで動かせるよう、 ギズモを player Transform へ貼り Object ツールへ切替える
    SetObjectToolActive(true);
    if (m_scene->m_player)
    {
        m_gizmo.SetSelected(&m_scene->m_player->Root());
        m_lastGizmoSelected = m_gizmo.Selected();
    }
    else
    {
        m_gizmo.ClearSelection();
        m_lastGizmoSelected = nullptr;
    }
}

void LevelEditorController::SelectCamera() noexcept
{
    m_selectedObjectId = NS::Game::Level::kInvalidObjectId;
    m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
    m_selectedCameraIndex = NS::Game::Level::kNoObjectIndex;
    m_gizmo.ClearSelection();
    m_lastGizmoSelected = nullptr;
    m_specialSelection = SpecialSelection::Camera;
}

NS::Scene::PlacedVirtualCamera* LevelEditorController::SelectedAreaCamera() noexcept
{
    if (m_selectedCameraIndex >= m_scene->m_areaCameras.size())
        return nullptr;
    return m_scene->m_areaCameras[m_selectedCameraIndex].cam;
}

void LevelEditorController::SyncSelectedCameraVolumeFromComponent() noexcept
{
    if (m_selectedCameraIndex >= m_scene->Level().cameraVolumes.size() ||
        m_selectedCameraIndex >= m_scene->m_areaCameras.size())
        return;
    NS::Scene::PlacedVirtualCamera* cam = m_scene->m_areaCameras[m_selectedCameraIndex].cam;
    if (cam == nullptr)
        return;

    // トリガ半径が 0 以下だと進入判定が常に外れるので最小正値に clamp し、 component 側へ反映する
    const NS::Math::Vector3 extent = cam->TriggerExtent();
    const NS::Math::Vector3 clamped{
        extent.x > 0.01f ? extent.x : 0.01f, extent.y > 0.01f ? extent.y : 0.01f, extent.z > 0.01f ? extent.z : 0.01f};
    if (clamped.x != extent.x || clamped.y != extent.y || clamped.z != extent.z)
        cam->SetTrigger(cam->TriggerCenter(), clamped);

    NS::Game::Level::CameraVolume& volume = m_scene->Level().cameraVolumes[m_selectedCameraIndex];
    const NS::Math::Vector3& position = cam->ViewPosition();
    const NS::Math::Vector3& target = cam->ViewTarget();
    const NS::Math::Vector3& center = cam->TriggerCenter();
    const NS::Math::Vector3& finalExtent = cam->TriggerExtent();
    volume.cameraPositionX = position.x;
    volume.cameraPositionY = position.y;
    volume.cameraPositionZ = position.z;
    volume.lookTargetX = target.x;
    volume.lookTargetY = target.y;
    volume.lookTargetZ = target.z;
    volume.triggerCenterX = center.x;
    volume.triggerCenterY = center.y;
    volume.triggerCenterZ = center.z;
    volume.triggerExtentX = finalExtent.x;
    volume.triggerExtentY = finalExtent.y;
    volume.triggerExtentZ = finalExtent.z;
    volume.lookAtPlayer = cam->LooksAtPlayer() ? 1u : 0u;
    volume.priority = cam->VcamPriority();
}

void LevelEditorController::AddCameraVolume() noexcept
{
    // 新規カメラは編集視点の中心あたりに置き、 そこから少し引いた位置から中心を見るデフォルトにする
    NS::Math::Vector3 center{static_cast<float>(m_scene->Level().spawnX),
                             static_cast<float>(m_scene->Level().spawnY),
                             static_cast<float>(m_scene->Level().spawnZ)};
    if (m_editorCameraRig)
        center = m_editorCameraRig->EditorCam().Center();

    NS::Game::Level::CameraVolume volume{};
    volume.cameraPositionX = center.x;
    volume.cameraPositionY = center.y + 5.0f;
    volume.cameraPositionZ = center.z - 10.0f;
    volume.lookTargetX = center.x;
    volume.lookTargetY = center.y;
    volume.lookTargetZ = center.z;
    volume.triggerCenterX = center.x;
    volume.triggerCenterY = center.y;
    volume.triggerCenterZ = center.z;
    volume.triggerExtentX = 3.0f;
    volume.triggerExtentY = 3.0f;
    volume.triggerExtentZ = 3.0f;
    volume.priority = 10;

    m_scene->Level().cameraVolumes.push_back(volume);
    m_scene->RebuildAreaCameras();
    SelectCameraByIndex(m_scene->Level().cameraVolumes.size() - 1);
}

void LevelEditorController::DeleteSelectedCamera() noexcept
{
    if (m_selectedCameraIndex >= m_scene->Level().cameraVolumes.size())
        return;
    m_scene->Level().cameraVolumes.erase(m_scene->Level().cameraVolumes.begin() +
                                         static_cast<std::ptrdiff_t>(m_selectedCameraIndex));
    m_selectedCameraIndex = NS::Game::Level::kNoObjectIndex;
    m_scene->RebuildAreaCameras();
}

void LevelEditorController::RenderAreaCameraGizmos() noexcept
{
    // edit 中、 各 area camera のトリガ範囲 AABB とカメラ位置 → 注視点を線で可視化する
    // 選択中のカメラは強調色にする
    for (std::size_t i = 0; i < m_scene->Level().cameraVolumes.size(); ++i)
    {
        const NS::Game::Level::CameraVolume& v = m_scene->Level().cameraVolumes[i];
        const bool selected = (i == m_selectedCameraIndex);

        const NS::Math::Color triggerColor =
            selected ? NS::Math::Color{1.0f, 0.55f, 0.10f, 1.0f} : NS::Math::Color{0.20f, 0.70f, 1.0f, 1.0f};
        const NS::Math::AABB trigger{NS::Math::Vector3{v.triggerCenterX, v.triggerCenterY, v.triggerCenterZ},
                                     NS::Math::Vector3{v.triggerExtentX, v.triggerExtentY, v.triggerExtentZ}};
        NS::Graphics::DebugDraw::AABB(trigger, triggerColor);

        const NS::Math::Vector3 camPos{v.cameraPositionX, v.cameraPositionY, v.cameraPositionZ};
        const NS::Math::Vector3 lookAt{v.lookTargetX, v.lookTargetY, v.lookTargetZ};
        const NS::Math::Color camColor{1.0f, 0.85f, 0.10f, 1.0f};
        const NS::Math::AABB camMarker{camPos, NS::Math::Vector3{0.3f, 0.3f, 0.3f}};
        NS::Graphics::DebugDraw::AABB(camMarker, camColor);
        NS::Graphics::DebugDraw::Line(camPos, lookAt, camColor);
    }
}

void LevelEditorController::RenderColliderWireframes() noexcept
{
    // 自由配置物は回転・スケール込みの OBB、 grid solid は AABB で当たり形状を可視化する
    // 両者を緑系で出し、 自由配置=明るい緑 / grid=濃い緑 で区別する
    const NS::Math::Color freeColor{0.35f, 1.0f, 0.45f, 1.0f};
    const NS::Math::Color gridColor{0.15f, 0.70f, 0.30f, 1.0f};

    // free / grid の別は ObjectInstance の flags で決まる。 runtime list は 1 本
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        const NS::Game::Level::ObjectInstance& entry = m_scene->Level().objects[m_scene->World().SourceIndices()[i]];
        if ((entry.flags & NS::Game::Level::kObjectFlagGridAligned) == 0)
        {
            // 自由配置物は Box があれば回転込み OBB、 球 / カプセルは collider 由来の AABB で出す
            if (auto* box = NS::Game::Blocks::FindComponent<NS::Scene::BoxColliderComponent>(*m_scene->World().Objects()[i]))
            {
                const NS::Physics::OBB obb = box->WorldOBB();
                NS::Graphics::DebugDraw::OBB(obb.center, obb.axisX, obb.axisY, obb.axisZ, obb.halfExtents, freeColor);
            }
            else if (auto aabb = NS::Game::Blocks::ColliderWorldAABB(*m_scene->World().Objects()[i]))
            {
                NS::Graphics::DebugDraw::AABB(*aabb, freeColor);
            }
        }
        else if (NS::Game::Blocks::IsGridSolidObject(entry))
        {
            if (auto* box = NS::Game::Blocks::FindComponent<NS::Scene::BoxColliderComponent>(*m_scene->World().Objects()[i]))
                NS::Graphics::DebugDraw::AABB(box->WorldAABB(), gridColor);
        }
        // hazard 等の grid の非 solid は当たり形状を出さない
    }
}

void LevelEditorController::CaptureSelectionFromGizmo() noexcept
{
    NS::Scene::Transform* selected = m_gizmo.Selected();
    // Hierarchy で選んだ非 gizmo 選択を毎フレーム潰さないため前フレームと同じなら据え置き
    if (selected == m_lastGizmoSelected)
        return;
    m_lastGizmoSelected = selected;
    if (selected == nullptr)
    {
        m_selectedObjectId = NS::Game::Level::kInvalidObjectId;
        m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
        // Player のギズモが外れた (空クリック等) ら特殊選択も解除し、 再貼り付けで掴み続けないようにする
        m_specialSelection = SpecialSelection::None;
        return;
    }
    // ビューポートでプレイヤーをピックしたら Player 特殊選択へ流す。 移動 / 回転 / undo は同じ経路に乗る
    if (m_scene->m_player && selected == &m_scene->m_player->Root())
    {
        m_specialSelection = SpecialSelection::Player;
        m_selectedObjectId = NS::Game::Level::kInvalidObjectId;
        m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
        m_selectedCameraIndex = NS::Game::Level::kNoObjectIndex;
        return;
    }
    // ビューポートでのオブジェクト実ピックは Player / Camera の特殊選択より優先する
    m_specialSelection = SpecialSelection::None;
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        if (&m_scene->World().Objects()[i]->Root() == selected)
        {
            m_selectedObjectIndex = m_scene->World().SourceIndices()[i];
            m_selectedObjectId = m_scene->World().EditIds()[m_selectedObjectIndex];
            return;
        }
    }
    m_selectedObjectId = NS::Game::Level::kInvalidObjectId;
    m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
}

void LevelEditorController::ResolveSelectionFromId() noexcept
{
    // id → 現在の objects 添字。 delete / undo で添字はズレるので毎フレーム引き直す
    m_selectedObjectIndex = (m_selectedObjectId == NS::Game::Level::kInvalidObjectId)
                                ? NS::Game::Level::kNoObjectIndex
                                : NS::Game::Level::IndexOfId(SceneEditTarget(), m_selectedObjectId);

    // SetSelected が進行中ドラッグを切ってしまうのでドラッグ中は gizmo の選択を貼り直さない
    if (m_gizmo.IsDragging())
        return;

    // 選択 id が自由オブジェクトを指すなら gizmo に貼り直す。 grid / 不在 / 特殊選択は gizmo を外す
    if (m_specialSelection == SpecialSelection::None && m_selectedObjectIndex != NS::Game::Level::kNoObjectIndex)
    {
        for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
        {
            if (m_scene->World().SourceIndices()[i] != m_selectedObjectIndex)
                continue;
            // grid solid は掴むと自由化されるため gizmo に貼り直さない
            if ((m_scene->Level().objects[m_selectedObjectIndex].flags & NS::Game::Level::kObjectFlagGridAligned) != 0)
                break;
            NS::Scene::Transform* root = &m_scene->World().Objects()[i]->Root();
            if (m_gizmo.Selected() != root)
                m_gizmo.SetSelected(root);
            m_lastGizmoSelected = root;
            return;
        }
    }
    // Player 選択中はギズモを実プレイヤーへ貼り直す。 rebuild / mode 切替を跨いでも掴める状態を保つ
    if (m_specialSelection == SpecialSelection::Player && m_scene->m_player)
    {
        NS::Scene::Transform* root = &m_scene->m_player->Root();
        if (m_gizmo.Selected() != root)
            m_gizmo.SetSelected(root);
        m_lastGizmoSelected = root;
        return;
    }
    if (m_gizmo.Selected() != nullptr)
        m_gizmo.ClearSelection();
    m_lastGizmoSelected = nullptr;
}

void LevelEditorController::SetSelectedFreePosition(NS::Math::Vector3 position) noexcept
{
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        if (m_scene->World().SourceIndices()[i] != m_selectedObjectIndex)
            continue;
        if ((m_scene->Level().objects[m_selectedObjectIndex].flags & NS::Game::Level::kObjectFlagGridAligned) != 0)
            return;
        m_scene->World().Objects()[i]->Root().SetPosition(position);
        return;
    }
}

void LevelEditorController::SetSelectedFreeRotation(NS::Math::Quaternion rotation) noexcept
{
    // 永続化は gizmo R と同じく SyncFreeObjectTransforms 経由で、 runtime Transform を真実の源にする
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        if (m_scene->World().SourceIndices()[i] != m_selectedObjectIndex)
            continue;
        if ((m_scene->Level().objects[m_selectedObjectIndex].flags & NS::Game::Level::kObjectFlagGridAligned) != 0)
            return;
        m_scene->World().Objects()[i]->Root().SetRotation(rotation);
        return;
    }
}

void LevelEditorController::SetSelectedFreeScale(NS::Math::Vector3 scale) noexcept
{
    // ImGui の入力で 0 / 負になると描画と当たり判定が壊れるため最小正値で止める
    constexpr float kMinScale = 0.01f;
    scale.x = scale.x < kMinScale ? kMinScale : scale.x;
    scale.y = scale.y < kMinScale ? kMinScale : scale.y;
    scale.z = scale.z < kMinScale ? kMinScale : scale.z;
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        if (m_scene->World().SourceIndices()[i] != m_selectedObjectIndex)
            continue;
        if ((m_scene->Level().objects[m_selectedObjectIndex].flags & NS::Game::Level::kObjectFlagGridAligned) != 0)
            return;
        m_scene->World().Objects()[i]->Root().SetScale(scale);
        return;
    }
}

void LevelEditorController::PromoteSelectedToFree() noexcept
{
    if (m_selectedObjectIndex >= m_scene->Level().objects.size())
        return;
    const NS::Game::Level::ObjectInstance& entry = m_scene->Level().objects[m_selectedObjectIndex];
    if (NS::Game::Blocks::IsGridSolidObject(entry))
        PromoteGridBlockToFree(m_selectedObjectIndex);
}

void LevelEditorController::AddObject()
{
    // 新規オブジェクトは編集視点の中心あたりへ置く。 Add Camera と同じ基準点
    NS::Math::Vector3 center{static_cast<float>(m_scene->Level().spawnX),
                             static_cast<float>(m_scene->Level().spawnY),
                             static_cast<float>(m_scene->Level().spawnZ)};
    if (m_editorCameraRig)
        center = m_editorCameraRig->EditorCam().Center();

    // flags は 0 のまま = 非 gridAligned の自由配置物。 scale / rotation / collider は既定値
    NS::Game::Level::ObjectInstance object{};
    object.positionX = center.x;
    object.positionY = center.y;
    object.positionZ = center.z;
    // 既定の素の cube を自由配置物として実 component で起こす
    object.components = NS::Game::Blocks::MakeFreeCubeComponents(object);

    // grid 設置と同じ undo 履歴へ載せる。 Do が objects / ids 末尾へ append する
    NS::Game::Level::EditTarget target = SceneEditTarget();
    m_editor.Undo().Push(std::make_unique<NS::Editor::AddObjectCommand>(object), target);

    // 追加した自由オブジェクトの runtime 実体を作り、 選択候補を貼り直して末尾の新規を選択する
    m_scene->RebuildWorld();
    RefreshGizmoSelectables();
    SelectObjectByIndex(m_scene->Level().objects.size() - 1);
}

void LevelEditorController::AddComponentToSelected(std::string_view typeName)
{
    const std::uint32_t id = m_selectedObjectId;
    if (id == NS::Game::Level::kInvalidObjectId)
        return;
    NS::Game::Level::EditTarget target = SceneEditTarget();
    if (NS::Game::Level::IndexOfId(target, id) == NS::Game::Level::kNoObjectIndex)
        return;

    NS::Game::Level::ComponentData payload;
    payload.typeName = std::string(typeName);
    m_editor.Undo().Push(std::make_unique<NS::Editor::AddComponentCommand>(id, std::move(payload)), target);

    // components が変わったので runtime を組み直し、 同じ id の選択を貼り直す
    m_scene->RebuildWorld();
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::RemoveComponentFromSelected(std::size_t componentIndex)
{
    const std::uint32_t id = m_selectedObjectId;
    if (id == NS::Game::Level::kInvalidObjectId)
        return;
    NS::Game::Level::EditTarget target = SceneEditTarget();
    const std::size_t objectIndex = NS::Game::Level::IndexOfId(target, id);
    if (objectIndex == NS::Game::Level::kNoObjectIndex)
        return;

    // 空構成は build で消えるゴーストになるので最後の 1 個 / 範囲外は消さない。 no-op command を積まず undo
    // 履歴も汚さない
    const std::vector<NS::Game::Level::ComponentData>& components = m_scene->Level().objects[objectIndex].components;
    if (componentIndex >= components.size() || components.size() <= 1)
        return;

    m_editor.Undo().Push(std::make_unique<NS::Editor::RemoveComponentCommand>(id, componentIndex), target);

    m_scene->RebuildWorld();
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::DuplicateSelectedObject()
{
    const std::uint32_t id = m_selectedObjectId;
    if (id == NS::Game::Level::kInvalidObjectId)
        return;
    NS::Game::Level::EditTarget target = SceneEditTarget();
    if (NS::Game::Level::IndexOfId(target, id) == NS::Game::Level::kNoObjectIndex)
        return;

    // 複製は objects 末尾へ積まれる。 組み直してから末尾を新しい選択にする
    m_editor.Undo().Push(std::make_unique<NS::Editor::DuplicateObjectCommand>(id), target);

    m_scene->RebuildWorld();
    RefreshGizmoSelectables();
    if (!m_scene->Level().objects.empty())
        SelectObjectByIndex(m_scene->Level().objects.size() - 1);
}

void LevelEditorController::CopyComponentToClipboard(std::size_t componentIndex)
{
    const std::uint32_t id = m_selectedObjectId;
    if (id == NS::Game::Level::kInvalidObjectId)
        return;
    NS::Game::Level::EditTarget target = SceneEditTarget();
    const std::size_t objectIndex = NS::Game::Level::IndexOfId(target, id);
    if (objectIndex == NS::Game::Level::kNoObjectIndex)
        return;
    const std::vector<NS::Game::Level::ComponentData>& dataComponents =
        m_scene->Level().objects[objectIndex].components;
    if (componentIndex >= dataComponents.size())
        return;

    // runtime の同添字コンポが同型なら Inspector でライブ編集した値ごと写す
    // 未登録型が混じり runtime と data の添字がずれた時は data モデルの値で写して取り違えを防ぐ
    if (NS::Scene::GameObject* go = SelectedObjectGameObject())
    {
        const std::vector<NS::Scene::Component*>& runtime = go->Components();
        if (componentIndex < runtime.size() && runtime[componentIndex] != nullptr)
        {
            NS::Game::Level::ComponentData captured = NS::Editor::CaptureComponentData(*runtime[componentIndex]);
            if (captured.typeName == dataComponents[componentIndex].typeName)
            {
                m_componentClipboard = std::move(captured);
                return;
            }
        }
    }
    m_componentClipboard = dataComponents[componentIndex];
}

void LevelEditorController::PasteClipboardComponentToSelected()
{
    if (!m_componentClipboard)
        return;
    const std::uint32_t id = m_selectedObjectId;
    if (id == NS::Game::Level::kInvalidObjectId)
        return;
    NS::Game::Level::EditTarget target = SceneEditTarget();
    if (NS::Game::Level::IndexOfId(target, id) == NS::Game::Level::kNoObjectIndex)
        return;

    // 同型がすでにあっても末尾へ重ねて貼り、 上書きはしない
    m_editor.Undo().Push(std::make_unique<NS::Editor::AddComponentCommand>(id, *m_componentClipboard), target);

    m_scene->RebuildWorld();
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::PromoteGridBlockToFree(std::size_t objectIndex)
{
    if (objectIndex >= m_scene->Level().objects.size())
        return;

    // gridAligned を落とす昇格を TransformCommand 1 つとして積み、 grid undo と同じ履歴へ載せる
    const std::uint32_t id = m_scene->World().EditIds()[objectIndex];
    const NS::Game::Level::ObjectInstance before = m_scene->Level().objects[objectIndex];
    NS::Game::Level::ObjectInstance after = before;
    after.flags &= static_cast<std::uint8_t>(~NS::Game::Level::kObjectFlagGridAligned);
    NS::Game::Level::EditTarget target = SceneEditTarget();
    m_editor.Undo().Push(std::make_unique<NS::Editor::TransformCommand>(id, before, after), target);

    // 作り直すと自由化した object は非 gridAligned として組み直る。 選択候補 span を貼り直し、 選択 id も追従させる
    m_scene->RebuildWorld();
    RefreshGizmoSelectables();
    m_selectedObjectId = id;
    ReselectFreeObjectById(id);
}

NS::Game::Level::EditTarget LevelEditorController::SceneEditTarget() noexcept
{
    return NS::Game::Level::EditTarget{m_scene->Level(), m_scene->World().EditIds(), m_scene->World().NextEditId()};
}

void LevelEditorController::BeginTransformEdit() noexcept
{
    if (m_transformEditing)
        return;
    // Player 選択中は spawn を 1 undo 単位として baseline 退避する
    if (m_specialSelection == SpecialSelection::Player)
    {
        m_spawnEditBaseline = CurrentSpawnState();
        m_editingSpawn = true;
        m_transformEditing = true;
        return;
    }
    if (m_selectedObjectIndex >= m_scene->Level().objects.size())
        return;
    m_editBaseline = m_scene->Level().objects[m_selectedObjectIndex];
    m_editBaselineId = m_scene->World().EditIds()[m_selectedObjectIndex];
    m_editingSpawn = false;
    m_transformEditing = true;
}

void LevelEditorController::CommitTransformEdit() noexcept
{
    if (!m_transformEditing)
        return;
    m_transformEditing = false;

    // Player の spawn 変形は SetSpawnCommand 1 つで確定する
    if (m_editingSpawn)
    {
        m_editingSpawn = false;
        const NS::Editor::SetSpawnCommand::SpawnState after = CurrentSpawnState();
        if (after == m_spawnEditBaseline)
            return;
        NS::Game::Level::EditTarget spawnTarget = SceneEditTarget();
        // spawn は live sync で既に after。 Push の Do は idempotent で履歴記録のみ
        m_editor.Undo().Push(std::make_unique<NS::Editor::SetSpawnCommand>(m_spawnEditBaseline, after), spawnTarget);
        return;
    }

    NS::Game::Level::EditTarget target = SceneEditTarget();
    const std::size_t index = NS::Game::Level::IndexOfId(target, m_editBaselineId);
    if (index == NS::Game::Level::kNoObjectIndex)
        return;

    // 非 PRS の flags / material は model から、 PRS は live Transform から取る
    // パネル編集は Sync が 1 フレーム遅れるため model 直読みだと取りこぼす
    NS::Game::Level::ObjectInstance after = m_scene->Level().objects[index];
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        if (m_scene->World().SourceIndices()[i] != index)
            continue;
        // grid は cell 固定で live Transform を持たない。 自由配置物のみ PRS を live から取る
        if ((m_scene->Level().objects[index].flags & NS::Game::Level::kObjectFlagGridAligned) != 0)
            break;
        const NS::Scene::Transform& root = m_scene->World().Objects()[i]->Root();
        const NS::Math::Vector3 p = root.Position();
        const NS::Math::Quaternion r = root.Rotation();
        const NS::Math::Vector3 s = root.Scale();
        after.positionX = p.x;
        after.positionY = p.y;
        after.positionZ = p.z;
        after.rotationX = r.x;
        after.rotationY = r.y;
        after.rotationZ = r.z;
        after.rotationW = r.w;
        after.scaleX = s.x;
        after.scaleY = s.y;
        after.scaleZ = s.z;
        break;
    }

    if (after == m_editBaseline)
        return;

    // model を after に確定してから push する。 Push の Do は model == after なので何もせず履歴記録のみ
    m_scene->Level().objects[index] = after;
    m_editor.Undo().Push(std::make_unique<NS::Editor::TransformCommand>(m_editBaselineId, m_editBaseline, after),
                         target);
}

void LevelEditorController::ReselectFreeObjectById(std::uint32_t id) noexcept
{
    NS::Game::Level::EditTarget target = SceneEditTarget();
    const std::size_t index = NS::Game::Level::IndexOfId(target, id);
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        if (m_scene->World().SourceIndices()[i] != index)
            continue;
        // 掴むと自由化されるため grid solid は gizmo に貼らない
        if ((m_scene->Level().objects[index].flags & NS::Game::Level::kObjectFlagGridAligned) != 0)
            break;
        m_gizmo.SetSelected(&m_scene->World().Objects()[i]->Root());
        return;
    }
    m_gizmo.ClearSelection();
}

bool LevelEditorController::ApplyMaterialToSelected(const std::filesystem::path& matPath)
{
    auto* app = NS::App::Application::Get();
    if (m_editorToolMode != EditorToolMode::Object || app == nullptr)
        return false;

    NS::Scene::Transform* selected = m_gizmo.Selected();
    if (selected == nullptr)
        return false;

    // 選択中の Transform を持つ自由配置物を探す。 ギズモ選択は自由配置物を指す。 free / grid は flags で決まる
    std::size_t slot = m_scene->World().Objects().size();
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        if (&m_scene->World().Objects()[i]->Root() != selected)
            continue;
        if ((m_scene->Level().objects[m_scene->World().SourceIndices()[i]].flags &
             NS::Game::Level::kObjectFlagGridAligned) == 0)
            slot = i;
        break;
    }
    if (slot >= m_scene->World().Objects().size())
        return false;

    auto* mesh = NS::Game::Blocks::FindComponent<NS::Scene::MeshRendererComponent>(*m_scene->World().Objects()[slot]);
    if (mesh == nullptr)
        return false;

    const auto loaded = app->Assets().LoadMaterial(matPath);
    if (loaded.material == nullptr)
        return false;

    // .mat パスを ContentRoot 相対で材質表に登録して重複は再利用し、 ObjectInstance.materialIndex を更新して永続化する
    const auto exeDir = NS::Core::FileSystem::ContentRoot();
    const std::filesystem::path relative = matPath.lexically_relative(exeDir);
    const std::string stored = relative.empty() ? matPath.generic_string() : relative.generic_string();

    int materialIndex = -1;
    for (std::size_t k = 0; k < m_scene->Level().materialPaths.size(); ++k)
    {
        if (m_scene->Level().materialPaths[k] == stored)
        {
            materialIndex = static_cast<int>(k);
            break;
        }
    }
    if (materialIndex < 0)
    {
        m_scene->Level().materialPaths.push_back(stored);
        materialIndex = static_cast<int>(m_scene->Level().materialPaths.size() - 1);
    }
    m_scene->Level().objects[m_scene->World().SourceIndices()[slot]].materialIndex =
        static_cast<std::int16_t>(materialIndex);

    mesh->SetMaterial(loaded.material);
    mesh->SetBaseColor(loaded.baseColor);
    return true;
}
