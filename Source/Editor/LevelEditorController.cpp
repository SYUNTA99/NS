#include "Editor/LevelEditorController.h"
#include "NSlib/Audio/SoundScene.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"

#include "Editor/AssetPicker.h"
#include "Editor/EditorActors.h"
#include "Editor/GridMath.h"
#include "Editor/LevelFilePaths.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/ThirdPersonFollow.h"
#include "Game/PlayerActor.h"
#include "NSlib/App/Application.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Sphere.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/Input.h"
#include "NSlib/Core/Windows/Keyboard.h"
#include "NSlib/Core/Windows/Mouse.h"
#include "NSlib/CoreObject/ActorInitInfo.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/Asset/Texture.h"
#include "NSlib/CoreObject/ClassObjs/BoxCollision.h"
#include "NSlib/CoreObject/ClassObjs/CapsuleCollision.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/MeshCollision.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/ClassObjs/SphereCollision.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/ClassObjs/VirtualCamera.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneCamera.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/Gfx/Render/DebugDraw.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace NS
{
    namespace Editor
    {
        namespace
        {
            // 編集復帰の視点ブレンド秒。CameraManager の vcam 切替の既定 0.35
            // 秒と揃え、モード切替の繋ぎを同じ感触にする
            constexpr float k_EditBlendSeconds = 0.35f;

        } // namespace

        LevelEditorController::LevelEditorController(Scene& scene) noexcept : m_scene(scene)
        {
            // シーンだけで決まる物は Setup の前から使えるよう、ここで差す。入力は Application が要るので Setup で差す
            m_editor.SetCaptureLevelFn([this]() { return m_scene.ToJson(); });
            m_editor.SetScene(&m_scene);
            m_editor.SetSceneEditableFn([this]() { return IsSceneEditable(); });
            m_editor.SetLoadLevelFn([this](nlohmann::json&& fresh) {
                CommitTransformEdit();
                CommitClassObjEdit();
                ClearAssetSelection();
                SelectActorById(k_NoActorId);
                m_dragFollowers.clear();
                m_gizmoWasDragging = false;
                m_scene.ResetFromJson(std::move(fresh));
                m_scene.SetSimulationEnabled(false);
                FocusLoadedScene();
            });
        }

        bool LevelEditorController::IsSceneEditable() const noexcept
        {
            return m_mode == Mode::Edit;
        }

        void LevelEditorController::SelectSceneSettings()
        {
            ClearAssetSelection();
            SelectActorById(k_NoActorId);
        }

        const std::string& LevelEditorController::SkyboxPath() const noexcept
        {
            return m_scene.SkyboxPath();
        }

        bool LevelEditorController::SetSkyboxPath(std::string_view path)
        {
            if (!IsSceneEditable())
            {
                return false;
            }
            std::string normalized;
            if (!path.empty())
            {
                const std::string root = OS::FileSystem::ContentRoot();
                const std::optional<std::string> resolved = OS::FileSystem::ResolveUnder(root, path);
                if (!resolved || !IsSkyboxAssetPath(*resolved))
                {
                    return false;
                }
                std::optional<std::string> relative = OS::FileSystem::RelativeUnder(root, *resolved);
                if (!relative)
                {
                    NS_LOG_ERROR(App, "空の素材 {} を ContentRoot からの道にできないので変えない", *resolved);
                    return false;
                }
                normalized = std::move(*relative);
            }
            if (normalized != m_scene.SkyboxPath())
            {
                SceneEnvironment after = m_scene.Environment();
                after.skybox = std::move(normalized);
                m_scene.SetEnvironment(after);
                m_editor.MarkEdited();
            }
            return true;
        }

        const std::string& LevelEditorController::SceneMusic() const noexcept
        {
            return AssetFieldName(m_scene.Environment().music);
        }

        float LevelEditorController::SceneMusicVolume() const noexcept
        {
            return m_scene.Environment().musicVolume;
        }

        float LevelEditorController::SceneSoundFullVolumeDistance() const noexcept
        {
            return m_scene.Environment().soundFullVolumeDistance;
        }

        bool LevelEditorController::SetSceneMusic(std::string_view music, float volume, float soundFullVolumeDistance)
        {
            if (!IsSceneEditable() || !std::isfinite(volume) ||
                !Audio::SoundScene::IsUsableFullVolumeDistance(soundFullVolumeDistance))
            {
                return false;
            }
            SceneEnvironment after = m_scene.Environment();
            LoadAssetField(after.music, music);
            after.musicVolume = ClampMusicVolume(volume);
            after.soundFullVolumeDistance = soundFullVolumeDistance;
            // 触ったかを見る比べなので、値のコピーどうしを != で比べる
            if (AssetFieldName(after.music) != AssetFieldName(m_scene.Environment().music) ||
                after.musicVolume != m_scene.Environment().musicVolume ||
                after.soundFullVolumeDistance != m_scene.Environment().soundFullVolumeDistance)
            {
                m_scene.SetEnvironment(after);
                m_editor.MarkEdited();
            }
            return true;
        }

        LevelEditorController::~LevelEditorController() = default;

        bool LevelEditorController::PlayPaused() const noexcept
        {
            return m_scene.IsSimulationPaused();
        }

        void LevelEditorController::TogglePlayPause() noexcept
        {
            m_scene.SetSimulationPaused(!m_scene.IsSimulationPaused());
            // 止めている間の押し (パネルのクリック・値の打ち込み) を、動かし直した世界へ渡さない
            if (!m_scene.IsSimulationPaused())
            {
                DiscardPlayerPresses();
            }
            // 固定したままだと Inspector を触れず、プレイ中に値を調整する動線が消える。止めている間は解く
            // 再開は Game が映っている時だけ固定へ戻す。Scene を見ている時はタブを押せるよう出したまま
            if (m_mode == Mode::Play)
            {
                ApplyPlayCursor(CursorAfterPauseToggle(
                    {.paused = m_scene.IsSimulationPaused(), .gameViewInFront = !m_gameViewHidden}));
            }
        }

        void LevelEditorController::RecaptureCursorOnGameClick(bool gameViewClicked) noexcept
        {
            Application* app = Application::Get();
            if (app == nullptr)
            {
                return;
            }
            const bool recapture = ShouldRecaptureCursor({.playMode = m_mode == Mode::Play,
                                                          .paused = PlayPaused(),
                                                          .cursorReleased = app->Window().IsCursorVisible(),
                                                          .gameViewClicked = gameViewClicked});
            if (recapture)
            {
                ApplyPlayCursor(PlayCursor::Captured);
            }
        }

        void LevelEditorController::ApplyPlayCursor(PlayCursor cursor) noexcept
        {
            Application* app = Application::Get();
            if (app == nullptr)
            {
                return;
            }
            app->SetCursorCaptured(cursor == PlayCursor::Captured);
        }

        const ActorList& LevelEditorController::Actors() const noexcept
        {
            return m_scene.Actors();
        }

        CameraManager& LevelEditorController::Cameras() const noexcept
        {
            return *m_scene.GetCameraManager();
        }

        SceneCamera& LevelEditorController::MainCamera() const noexcept
        {
            return *m_scene.MainCamera();
        }

        void LevelEditorController::Setup(std::string_view startLevelName)
        {
            Application* app = Application::Get();
            if (app == nullptr)
            {
                return;
            }

            // 編集の投影設定で、遠景を 5000 まで見せ near 0.1 は既定
            // far は EditorCamera の k_MaxDistance より広く取り、最大ズームアウトでも地形を映す
            m_editorCamera.SetNearPlane(0.1f);
            m_editorCamera.SetFarPlane(5000.0f);
            m_editorCamera.SetFovY(ToRadians(Degrees{60.0f}));

            m_editor.SetInput(&app->Input());

            m_gizmo.SetInput(&app->Input());

            // Game が立てた空のシーンはプレイが回っている。プレイを終えて編集モードへ切替えてから起動シーンを開く
            // 先に開くと、プレイ開始の写しへ戻す組み直しが、開く時に補ったプレイヤーを消す
            // Game はプレイを始める前のシーンを JSON に残さないので、今のシーンを書き出して戻る姿にする
            m_playBaseline = m_scene.ToJson();
            LeavePlayForEdit();
            m_mode = Mode::Edit;
            bool opened = false;
            if (!startLevelName.empty())
            {
                opened = m_editor.OpenStartupLevel(startLevelName);
                if (!opened)
                {
                    NS_LOG_ERROR(App, "起動シーンを開けなかった。 空のシーンで編集を始める: {}", startLevelName);
                }
            }
            // 選択候補は読み込みの関数かこの後に今の実体から集めるので、初回 Tick の貼り直しを省く
            m_editor.ClearLevelDirty();
            // 開けた時は、読み込みの関数が視点を合わせて選択候補を集め終えている
            if (!opened)
            {
                FocusLoadedScene();
            }
        }

        void LevelEditorController::FocusLoadedScene() noexcept
        {
            SceneActor* focusActor = FindPlayer(m_scene.Actors());
            if (focusActor == nullptr && m_scene.Actors().ActorCount() > 0)
            {
                focusActor = *m_scene.Actors().begin();
            }
            Vector3 startCenter{0.0f, 0.0f, 0.0f};
            if (focusActor != nullptr)
            {
                startCenter = focusActor->TransformObj().Position();
            }
            m_editorCamera.SetCenter(startCenter);
            m_editorCamera.SetDistance(5.0f);
            m_editBlending = false;
            MainCamera().ApplyPose(m_editorCamera.Pose());
        }

        void LevelEditorController::Teardown()
        {
            // ギズモはアクターの Transform を非所有参照するので、scene 破棄前に選択を外す
            m_gizmo.ClearSelection();
        }

        void LevelEditorController::EnterPlay() noexcept
        {
            if (m_mode == Mode::Play)
            {
                return;
            }
            m_mode = Mode::Play;
            // モード遷移へ押しっぱなしを持ち越さないよう消す
            if (Application* app = Application::Get())
            {
                app->Input().Keyboard().ClearState();
                app->Input().Mouse().ClearState();
            }
            // 引いている途中の変形を確定してギズモを休ませる。引きを持ち越すと、編集へ戻る組み直しで消えた物を読む
            CommitTransformEdit();
            // Inspector の編集が子孫の控えを持っていたら閉じる。持ち越すと、プレイ中の次の変形が編集の控えを確定する
            CommitClassObjEdit();
            m_dragFollowers.clear();
            m_gizmoWasDragging = false;
            m_gizmo.SetActive(false);

            // 実体が唯一の出所なので組み直しは要らない。編集で動いた当たりは、世界を止めた歩が揃えている
            // 編集へ戻る時の姿として、コースを始めてメッセージが値を動かす前のシーンを JSON に書き出す
            m_playBaseline = m_scene.ToJson();
            m_playEdited = false;
            // 始める手順は出荷と同じく StartPlay が行う。曲は編集へ戻る時に止まるので、プレイを始めるたびに始まる
            m_scene.StartPlay();
            // 編集の自由視点からプレイ視点へ、vcam 切替と同じブレンドで繋ぐ
            Cameras().BeginBlendFrom(m_editorCamera.Pose());
            m_scene.SetSimulationEnabled(true);
            // 編集の間の押し (配置のクリック・ショートカット) を、プレイの最初の歩へ渡さない
            DiscardPlayerPresses();

            // プレイ突入はカーソルを消し、マウスを相対モードにして視点操作をカーソル位置から切り離す
            ApplyPlayCursor(PlayCursor::Captured);

            ResolveSelectionFromId();
        }

        void LevelEditorController::EnterEdit() noexcept
        {
            if (m_mode == Mode::Edit)
            {
                return;
            }
            m_mode = Mode::Edit;
            // モード遷移へ押しっぱなしを持ち越さないよう消す
            if (Application* app = Application::Get())
            {
                app->Input().Keyboard().ClearState();
                app->Input().Mouse().ClearState();
            }
            // プレイを終え、プレイを始めた時の JSON から編集の姿へ組み直す
            LeavePlayForEdit();
            // プレイ視点から自由視点へ繋ぐ。始点は直前まで実カメラに書かれていた pose
            m_editBlendFrom = Cameras().LastPose();
            m_editBlendElapsed = 0.0f;
            m_editBlending = true;
            // 編集復帰の組み直しを跨いだ選択を、id から現在のオブジェクトへ貼り直してから編集へ戻る
            ResolveSelectionFromId();
        }

        bool LevelEditorController::HasUnsavedSceneChanges() const noexcept
        {
            return m_editor.HasUnsavedChanges() || (m_mode == Mode::Play && m_playEdited);
        }

        void LevelEditorController::RequestStepFrame() noexcept
        {
            if (m_mode != Mode::Play)
            {
                return;
            }
            // 止めている間の押しを、コマ送りの 1 歩へ渡さない
            DiscardPlayerPresses();
            m_scene.StepSimulation();
        }

        void LevelEditorController::DiscardPlayerPresses() noexcept
        {
            if (PlayerActor* player = FindPlayer(m_scene.Actors()))
            {
                player->Input().DiscardUnreadPresses();
            }
        }

        void LevelEditorController::LeavePlayForEdit()
        {
            m_scene.SetSimulationEnabled(false);

            // プレイで変わった配置を編集へ持ち込まないため、プレイを始めた時の JSON から復元する
            NS_ASSERT(App, m_playBaseline.has_value(), "LeavePlayForEdit: プレイを始めた時の JSON が無い");
            nlohmann::json baseline{};
            if (m_playBaseline)
            {
                baseline = std::move(*m_playBaseline);
                m_playBaseline.reset();
            }
            else
            {
                // 戻す姿が無くても、プレイを終えるのは組み直しだけなので、今の姿で組み直す。
                // プレイで動いた分を編集の変更にしない
                baseline = m_scene.ToJson();
            }
            m_scene.ResetFromJson(std::move(baseline));
            // プレイ中にインスペクタで変えた値は、組み直しの姿に入っている。書いていればシーンを未保存にする
            // プレイで動いた値は baseline へ書かないので数えない
            if (m_playEdited)
            {
                m_editor.MarkEdited();
                m_playEdited = false;
            }

            if (Application* app = Application::Get())
            {
                app->SetCursorCaptured(false);
            }
        }

        void LevelEditorController::SetGameView(int x, int y, int width, int height, bool hovered) noexcept
        {
            m_gameViewRect = ViewRect{x, y, width, height};
            m_gameViewRectValid = true;
            m_gameViewHovered = hovered;
            m_gameViewHidden = false;
            // 窓の中心だと別のパネルの上へ乗ることがある。プレイ中は Game ビューの中心へ留める
            if (m_mode == Mode::Play)
            {
                if (Application* app = Application::Get())
                {
                    app->Window().SetCursorLockPoint(x + width / 2, y + height / 2);
                }
            }
        }

        void LevelEditorController::ClearGameView() noexcept
        {
            // 全画面描画では予備の矩形を使い、カーソルが重なっている状態にする
            m_gameViewRectValid = false;
            m_gameViewHovered = true;
            m_gameViewHidden = false;
        }

        void LevelEditorController::HideGameView() noexcept
        {
            // 前面のパネルが裏へ隠れた。配置カーソルと編集オーバーレイを止める
            m_gameViewHidden = true;
            m_gameViewHovered = false;
        }

        ViewRect LevelEditorController::CurrentViewRect() const noexcept
        {
            if (m_gameViewRectValid)
            {
                return m_gameViewRect;
            }
            // 未設定時は全画面をフォールバックとする。ウィンドウ不在は 0 サイズ
            ViewRect full{};
            if (Application* app = Application::Get())
            {
                const Size2D size = app->Window().Size();
                full.width = size.width;
                full.height = size.height;
            }
            return full;
        }

        void LevelEditorController::TickPlaySceneView(const EditorCameraInput& input) noexcept
        {
            // プレイ中に Scene タブへ自由視点を映すフレームだけ効かせる。編集モード中は実カメラを触らない
            // 描画視点は SceneViewPose が自由視点の pose を渡すので、ここは入力適用だけでよい
            if (m_mode != Mode::Play)
            {
                return;
            }
            m_editorCamera.ApplyInput(input);
        }

        std::optional<CameraPose> LevelEditorController::SceneViewPose() noexcept
        {
            // 編集中は実カメラ (TickEdit が自由視点の pose を書く) 任せ。プレイ中だけ自由視点を上書きする
            if (m_mode != Mode::Play)
            {
                return std::nullopt;
            }
            return m_editorCamera.Pose();
        }

        std::optional<CameraPose> LevelEditorController::GameViewPose() noexcept
        {
            // プレイ中は CameraManager (follow・ブレンド維持) 任せ。編集中だけゲームカメラを上書きする
            if (m_mode == Mode::Play)
            {
                return std::nullopt;
            }
            return Cameras().EvaluateTopPose(1.0f);
        }

        nlohmann::json LevelEditorController::SceneSnapshot() const
        {
            return m_scene.ToJson();
        }

        void LevelEditorController::SetSceneViews(std::vector<SceneView> views)
        {
            m_scene.GetSceneRenderer()->SetSceneViews(std::move(views));
        }

        void LevelEditorController::Tick()
        {
            // 編集とプレイのどちらの歩も読む。読まない歩を挟むと、その間の押しが次に読んだ歩へ持ち越される
            const OS::InputPresses presses = m_inputReader.Read();
            // プレイ中のクリア / 死亡は応答部品が出荷と同じ手順で完結させる
            // editor は割り込まず、編集へ戻るのは Tab / Pause modal の明示操作だけ
            if (m_mode == Mode::Edit)
            {
                TickEdit(presses);
                return;
            }

            // Esc はカーソルの解放に使い、パネルを操作する途中でプレイを終了させない
            Application* app = Application::Get();
            if (app == nullptr)
            {
                return;
            }
            if (presses.WasPressed(OS::Key::Escape) && !app->Window().IsCursorVisible())
            {
                ApplyPlayCursor(PlayCursor::Released);
            }
        }

        void LevelEditorController::TickEdit(const OS::InputPresses& presses)
        {
            Application* app = Application::Get();
            if (app == nullptr)
            {
                return;
            }

            // F5 のシェーダ再読込は編集時だけ。プレイ時は UI 表示の切替に使う
            if (!m_editShortcutsBlocked && presses.WasPressed(OS::Key::F5))
            {
                app->Assets().ReloadAllShaders();
            }

            // Esc: Object モードで選択中ならまず選択解除に使い終了させない
            if (presses.WasPressed(OS::Key::Escape))
            {
                if (m_gizmo.Selected() != nullptr)
                {
                    // 選択の id ごと外す。ギズモだけ外すと、次のフレームに id から選び直される
                    SelectActorById(k_NoActorId);
                    return;
                }
                Application::Quit();
                return;
            }

            m_editorCamera.Tick();

            // 自由視点を更新して実カメラへ反映し、ギズモと編集の光線判定に同じ視点を使う
            // 編集中は世界が止まり CameraManager が実カメラを書かないので、この書き込みが上書きされずに残る
            CameraPose pose = m_editorCamera.Pose();
            if (m_editBlending)
            {
                m_editBlendElapsed += OS::FrameTimer::FixedDelta();
                const float t = std::min(m_editBlendElapsed / k_EditBlendSeconds, 1.0f);
                const float eased = SmoothStep(t);
                pose = CameraPose::Lerp(m_editBlendFrom, pose, eased);
                if (t >= 1.0f)
                {
                    m_editBlending = false;
                }
            }
            MainCamera().ApplyPose(pose);

            // 編集の道具はギズモだけなので、編集中はいつもクリックと Q/W/E/R をギズモが受ける。Tab は Edit↔Play 専用
            m_gizmo.SetActive(true);

            // 追従カメラの Transform を実プレイ視点位置へ寄せてからギズモを回す。判定箱とギズモがその位置に出る
            SyncFollowCameraPoses();

            // 組み直しで破棄されたアクターを参照しないよう、選択を現在の実体へ引き直す
            ResolveSelectionFromId();

            const Matrix vp = Cameras().ViewProjection();
            const bool wasDragging = m_gizmoWasDragging;
            m_gizmo.Tick(
                vp, CurrentViewRect(), m_gameViewHovered, m_editShortcutsBlocked, presses, m_scene.Actors());

            // ビューポートでのギズモ選択変化を選択 id と Inspector が見る派生添字へ追従させる
            CaptureSelectionFromGizmo();

            // 追従カメラを掴んでいたら Transform の位置を初期姿勢へ逆算し ThirdPersonFollow
            // へ書き戻す。位置の書き戻しはこちら
            ApplyFollowCameraGizmoDrag();

            // ドラッグ開始で baseline を控え、終了で変わっていればシーンを未保存にする
            // 追従カメラの引きは初期姿勢の欄を変える。控えは部品の値まで写すので同じ道で変化を見つけられる
            const bool nowDragging = m_gizmo.IsDragging();
            if (!wasDragging && nowDragging)
            {
                BeginTransformEdit();
                // 掴んだ瞬間はまだ動いていない。ここで控えれば差分の基準が揃う
                CaptureDragFollowers(m_selectionIds);
            }
            else if (wasDragging && !nowDragging)
            {
                CommitTransformEdit();
                m_dragFollowers.clear();
            }
            // ギズモは主対象しか動かさないので、残りの選択はここで追わせる
            if (nowDragging)
            {
                ApplyDragToFollowers();
            }
            m_gizmoWasDragging = nowDragging;

            m_editor.Tick(m_editShortcutsBlocked, presses);
            if (m_editor.IsLevelDirty())
            {
                // 作る・読込はアクターを組み直し済。gizmo を今の実体へ貼り直す
                // 消えた選択や読込での id 振り直しは解決が自然に外す
                ResolveSelectionFromId();
                m_editor.ClearLevelDirty();
            }
        }

        void LevelEditorController::Render()
        {
            // プレイ中に Scene タブへ出す図形は DrawSceneViewShapes が積む。ImGui へ重ねる物は無い
            if (m_mode != Mode::Edit)
            {
                return;
            }

            // ギズモは最前面の drawlist に重ねる
            if (m_gizmo.IsActive())
            {
                m_gizmo.Render(Cameras().ViewProjection(), CurrentViewRect());
            }
        }

        bool LevelEditorController::HasInspectableSelection() const noexcept
        {
            return m_selectedActorId != k_NoActorId && m_scene.Actors().FindByActorId(m_selectedActorId) != nullptr;
        }

        SceneActor* LevelEditorController::SelectedActor() noexcept
        {
            // 選択の真実は永続 id。player も含め全アクターが ActorList に居るので id で引く
            if (m_selectedActorId == k_NoActorId)
            {
                return nullptr;
            }
            return m_scene.Actors().FindByActorId(m_selectedActorId);
        }

        bool LevelEditorController::SelectedIsPlayerActor() const noexcept
        {
            SceneActor* player = FindPlayer(m_scene.Actors());
            return player != nullptr && m_selectedActorId != k_NoActorId && m_selectedActorId == player->Id();
        }

        GL::Level::ThirdPersonFollow* LevelEditorController::SelectedFollowCamera() noexcept
        {
            if (GL::Level::FollowCameraActor* camera = Cast<GL::Level::FollowCameraActor>(SelectedActor()))
            {
                return &camera->Vcam();
            }
            return nullptr;
        }

        void LevelEditorController::SyncFollowCameraPoses()
        {
            // 追従カメラは位置を持たないので、edit 中は実プレイの視点位置へ Transform を寄せて frustum / pick / ギズモを出す
            // ドラッグ中の選択カメラだけは gizmo が Transform を握るため触らず、その位置を初期姿勢へ逆算する側に任せる
            const ActorList& objects = m_scene.Actors();
            for (SceneActor* object : objects)
            {
                GL::Level::FollowCameraActor* camera = Cast<GL::Level::FollowCameraActor>(object);
                if (camera == nullptr)
                {
                    continue;
                }
                GL::Level::ThirdPersonFollow* follow = &camera->Vcam();
                if (m_gizmo.IsDragging() && object->Id() == m_selectedActorId)
                {
                    continue;
                }
                object->TransformObj().SetPosition(follow->EvaluatePose(1.0f).position);
            }
        }

        void LevelEditorController::ApplyFollowCameraGizmoDrag()
        {
            // ドラッグ中の追従カメラは、gizmo が動かした Transform の位置から初期姿勢の方位角・仰角/距離を逆算して
            // 実体の部品へ書き戻す。Transform の位置は初期姿勢由来なので保存対象は部品側になる
            if (!m_gizmo.IsDragging())
            {
                return;
            }
            GL::Level::ThirdPersonFollow* follow = SelectedFollowCamera();
            if (follow == nullptr)
            {
                return;
            }
            SceneActor* go = SelectedActor();
            if (go == nullptr)
            {
                return;
            }
            follow->SetInitialPoseFromCameraPosition(go->TransformObj().Position());
        }

        void LevelEditorController::SelectActorById(uint32 id) noexcept
        {
            m_selectionIds.clear();
            if (id != k_NoActorId)
            {
                m_selectionIds.push_back(id);
            }
            SetPrimarySelection(id);
        }

        bool LevelEditorController::IsActorSelected(uint32 id) const noexcept
        {
            if (id == k_NoActorId)
            {
                return false;
            }
            return std::find(m_selectionIds.begin(), m_selectionIds.end(), id) != m_selectionIds.end();
        }

        void LevelEditorController::ToggleActorSelection(uint32 id) noexcept
        {
            if (id == k_NoActorId)
            {
                return;
            }

            const std::vector<uint32>::iterator it = std::find(m_selectionIds.begin(), m_selectionIds.end(), id);
            if (it != m_selectionIds.end())
            {
                m_selectionIds.erase(it);
                if (m_selectedActorId != id)
                {
                    return;
                }
                // 主対象を外したので、残っている中の直近へ譲る
                uint32 next = k_NoActorId;
                if (!m_selectionIds.empty())
                {
                    next = m_selectionIds.back();
                }
                SetPrimarySelection(next);
                return;
            }

            m_selectionIds.push_back(id);
            SetPrimarySelection(id);
        }

        void LevelEditorController::SelectActors(std::vector<uint32> ids, uint32 primary) noexcept
        {
            m_selectionIds = std::move(ids);
            if (primary != k_NoActorId && !IsActorSelected(primary))
            {
                m_selectionIds.push_back(primary);
            }
            SetPrimarySelection(primary);
        }

        void LevelEditorController::SetPrimarySelection(uint32 id) noexcept
        {
            m_selectedActorId = id;

            if (id == k_NoActorId)
            {
                m_gizmo.ClearSelection();
                m_lastGizmoSelected = nullptr;
                return;
            }

            // アクターの選択と素材の選択は排他
            ClearAssetSelection();

            // player も含め全アクターが ActorList に居る。選択したアクターを id で引いて、その Transform をギズモへ貼る
            if (SceneActor* go = m_scene.Actors().FindByActorId(id))
            {
                m_gizmo.SetSelected(&go->TransformObj());
                m_lastGizmoSelected = m_gizmo.Selected();
                return;
            }
            m_gizmo.ClearSelection();
            m_lastGizmoSelected = nullptr;
        }

        void LevelEditorController::CaptureSelectionFromGizmo() noexcept
        {
            Transform* selected = m_gizmo.Selected();
            // 一覧で選んだ非 gizmo 選択を毎フレーム潰さないため前フレームと同じなら据え置き
            if (selected == m_lastGizmoSelected)
            {
                return;
            }
            m_lastGizmoSelected = selected;
            if (selected == nullptr)
            {
                m_selectionIds.clear();
                m_selectedActorId = k_NoActorId;
                return;
            }
            // player も ActorList に居るので、ビューポートのピックと同じ経路で引ける
            for (SceneActor* object : m_scene.Actors())
            {
                if (&object->TransformObj() == selected)
                {
                    // ビューポートのクリックは 1 体に絞る。複数選択はアクター一覧の Ctrl / Shift で組む
                    m_selectedActorId = object->Id();
                    m_selectionIds.assign(1, m_selectedActorId);
                    ClearAssetSelection();
                    return;
                }
            }
            m_selectionIds.clear();
            m_selectedActorId = k_NoActorId;
        }

        void LevelEditorController::ResolveSelectionFromId() noexcept
        {
            // SetSelected が進行中ドラッグを切ってしまうのでドラッグ中は gizmo の選択を貼り直さない
            if (m_gizmo.IsDragging())
            {
                return;
            }

            // 選択 id が現存するアクターを指すなら gizmo に貼り直す。不在なら gizmo を外す
            // player も ActorList に居るので id で引ける。rebuild を跨いでも掴める状態を保つ
            if (m_selectedActorId != k_NoActorId)
            {
                if (SceneActor* go = m_scene.Actors().FindByActorId(m_selectedActorId))
                {
                    Transform* root = &go->TransformObj();
                    if (m_gizmo.Selected() != root)
                    {
                        m_gizmo.SetSelected(root);
                    }
                    m_lastGizmoSelected = root;
                    return;
                }
            }
            if (m_gizmo.Selected() != nullptr)
            {
                m_gizmo.ClearSelection();
            }
            m_lastGizmoSelected = nullptr;
        }

        void LevelEditorController::PlaceItem(const PlacementItem& item)
        {
            nlohmann::json object = item.prototype;
            SetActorJsonPosition(object, m_editorCamera.Center());
            CreateActorAndSelect(std::move(object));
        }

        void LevelEditorController::AddMeshParts(std::string_view meshPath)
        {
            // 参照は ContentRoot 相対で持つ。build 時にこの文字列から実体を引く
            const std::optional<std::string> meshRef =
                OS::FileSystem::RelativeUnder(OS::FileSystem::ContentRoot(), meshPath);
            if (!meshRef)
            {
                NS_LOG_ERROR(App, "メッシュ {} が ContentRoot の外にあるので置かない", meshPath);
                return;
            }

            nlohmann::json object = MakeMeshPartsPrototype(*meshRef);
            SetActorJsonPosition(object, m_editorCamera.Center());
            SetActorJsonName(object, OS::FileSystem::Stem(meshPath));
            CreateActorAndSelect(std::move(object));
        }

        void LevelEditorController::CreateActorAndSelect(nlohmann::json object)
        {
            std::vector<nlohmann::json> objects;
            objects.push_back(std::move(object));
            const std::vector<uint32> created = m_editor.CreateActors(std::move(objects));
            if (created.empty())
            {
                return;
            }

            // 組み直し後の新規を選択する。選択候補も貼り直す
            m_selectedActorId = created.front();
            m_selectionIds.assign(1, created.front());
            ClearAssetSelection();
            ResolveSelectionFromId();
        }

        void LevelEditorController::RenameActor(uint32 id, std::string_view name)
        {
            if (!IsSceneEditable() || id == k_NoActorId)
            {
                return;
            }
            SceneActor* object = m_scene.Actors().FindByActorId(id);
            if (object == nullptr)
            {
                return;
            }

            // 実体の名前を直接変える。他のアクターと重なれば番号が付く。参照は id で持つので切れない
            const std::string previous = object->Name();
            m_scene.Actors().RenameActor(object, name);
            if (object->Name() == previous)
            {
                return; // 同じ名前なら未保存にしない
            }
            m_editor.MarkEdited();

            ResolveSelectionFromId();
        }

        void LevelEditorController::DuplicateSelectedActor()
        {
            if (m_selectionIds.empty())
            {
                return;
            }

            const SceneActor* player = FindPlayer(m_scene.Actors());

            // 選択物の忠実コピーを作る。新しい id と、写した物どうしの参照の付け替えは CreateActors が行う
            std::vector<nlohmann::json> copies;
            copies.reserve(m_selectionIds.size());
            for (const uint32 id : m_selectionIds)
            {
                // プレイヤーは必ず 1 体。複製で 2 体目を作らせない
                if (player != nullptr && id == player->Id())
                {
                    continue;
                }
                std::optional<nlohmann::json> source = CaptureActor(m_scene.Actors(), id);
                if (!source)
                {
                    continue;
                }
                copies.push_back(std::move(*source));
            }

            std::vector<uint32> created = m_editor.CreateActors(std::move(copies));
            if (created.empty())
            {
                return;
            }

            const uint32 primary = created.back();
            SelectActors(std::move(created), primary);
            ResolveSelectionFromId();
        }

        void LevelEditorController::DeleteSelectedActor()
        {
            if (!IsSceneEditable() || m_selectionIds.empty())
            {
                return;
            }

            const SceneActor* player = FindPlayer(m_scene.Actors());
            if (player != nullptr &&
                std::find(m_selectionIds.begin(), m_selectionIds.end(), player->Id()) != m_selectionIds.end())
            {
                NS_LOG_WARN(App, "プレイヤーを含むため削除しなかった");
                return;
            }

            bool deleted = false;
            for (const uint32 victim : m_selectionIds)
            {
                if (m_scene.Actors().FindByActorId(victim) == nullptr)
                {
                    continue;
                }
                m_scene.RemoveActor(victim);
                deleted = true;
            }
            if (!deleted)
            {
                return;
            }
            m_editor.MarkEdited();

            SelectActorById(k_NoActorId);
            ResolveSelectionFromId();
        }

        void LevelEditorController::FocusSelectedInView() noexcept
        {
            if (m_selectionIds.empty())
            {
                return;
            }

            // 選んだ分を全部収める。中心は重心、距離は一番外側までの広がりで決める
            Vector3 sum{0.0f, 0.0f, 0.0f};
            std::vector<Vector3> centers;
            float extent = 0.0f;
            centers.reserve(m_selectionIds.size());
            for (const uint32 id : m_selectionIds)
            {
                SceneActor* object = m_scene.Actors().FindByActorId(id);
                if (object == nullptr)
                {
                    continue;
                }
                const Matrix world = object->TransformObj().WorldMatrix();
                const Vector3 center{world._41, world._42, world._43};
                centers.push_back(center);
                sum += center;

                const Vector3 scale = DecomposeAffine(world).scale;
                // 1m 立方の cube mesh の半サイズ 0.5 に拡縮を掛ける
                const float half = std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)}) * 0.5f;
                extent = std::max(extent, half);
            }
            if (centers.empty())
            {
                return;
            }

            const Vector3 center = sum / static_cast<float>(centers.size());
            for (const Vector3& each : centers)
            {
                extent = std::max(extent, (each - center).Length());
            }

            const float distance = std::max(extent * 4.0f, EditorCamera::k_MinDistance);

            m_editorCamera.SetCenter(center);
            m_editorCamera.SetDistance(distance);
        }

        void LevelEditorController::CaptureDragFollowers(const std::vector<uint32>& roots) noexcept
        {
            m_dragFollowers.clear();
            m_dragPrimaryId = m_selectedActorId;
            SceneActor* primary = m_scene.Actors().FindByActorId(m_dragPrimaryId);
            if (primary == nullptr)
            {
                return;
            }
            m_dragPrimaryWorld = primary->TransformObj().WorldMatrix();

            // 子孫も主対象に付いて動く。各 id は 1 回だけ並ぶので、親と子を両方選んでも差分は 1 回だけかかる
            for (const uint32 id : CollectWithDescendants(m_scene.Actors(), roots))
            {
                if (id == m_dragPrimaryId)
                {
                    continue;
                }
                SceneActor* object = m_scene.Actors().FindByActorId(id);
                if (object == nullptr)
                {
                    continue;
                }
                const Transform& pose = object->TransformObj();
                m_dragFollowers.push_back(
                    DragFollower{id, pose.WorldMatrix(), pose.Position(), pose.Rotation(), pose.Scale()});
            }
        }

        void LevelEditorController::ApplyDragToFollowers() noexcept
        {
            if (m_dragFollowers.empty())
            {
                return;
            }
            SceneActor* primary = m_scene.Actors().FindByActorId(m_dragPrimaryId);
            if (primary == nullptr)
            {
                return;
            }
            // 主対象が始めの姿へ戻ったら、残りも控えた値へそのまま戻す。分解し直すと誤差で変わったと記録される
            // 控えた行列のコピーと比べて戻ったかを見るので == でよい
            if (primary->TransformObj().WorldMatrix() == m_dragPrimaryWorld)
            {
                for (const DragFollower& follower : m_dragFollowers)
                {
                    if (SceneActor* object = m_scene.Actors().FindByActorId(follower.id))
                    {
                        object->TransformObj().SetPosition(follower.position);
                        object->TransformObj().SetRotation(follower.rotation);
                        object->TransformObj().SetScale(follower.scale);
                    }
                }
                return;
            }

            // 主対象が動いた分をワールド空間の差分として取り、残りへ同じだけ効かせる
            const Matrix delta = m_dragPrimaryWorld.Invert() * primary->TransformObj().WorldMatrix();
            for (const DragFollower& follower : m_dragFollowers)
            {
                SceneActor* object = m_scene.Actors().FindByActorId(follower.id);
                if (object == nullptr)
                {
                    continue;
                }

                Matrix local = follower.world * delta;

                Vector3 scale{};
                Quaternion rotation{};
                Vector3 position{};
                if (!local.Decompose(scale, rotation, position))
                {
                    continue;
                }
                object->TransformObj().SetPosition(position);
                object->TransformObj().SetRotation(rotation);
                object->TransformObj().SetScale(scale);
            }
        }

        void LevelEditorController::BeginTransformEdit() noexcept
        {
            if (m_transformEditing)
            {
                return;
            }
            // Inspector の編集が子孫の控えを使っていたら、先に確定して控えを空ける
            if (m_classObjFollowing)
            {
                CommitClassObjEdit();
            }
            // 選択している分と、その子孫をまとめて控える。動かなかった物は確定時に落ちる
            CaptureEditBaselines(CollectWithDescendants(m_scene.Actors(), m_selectionIds));
            if (m_editBaselines.empty())
            {
                return;
            }
            m_transformEditing = true;
        }

        void LevelEditorController::CaptureEditBaselines(const std::vector<uint32>& ids)
        {
            m_editBaselines.clear();
            for (const uint32 id : ids)
            {
                const SceneActor* actor = m_scene.Actors().FindByActorId(id);
                if (actor != nullptr)
                {
                    m_editBaselines.push_back(TransformEditBaseline{id, AllFieldValues(actor)});
                }
            }
        }

        void LevelEditorController::CommitTransformEdit() noexcept
        {
            if (!m_transformEditing)
            {
                return;
            }
            m_transformEditing = false;

            const bool changed = FinishEditBaselines();
            // プレイ中に動いた姿勢は、編集へ戻る組み直しで消えるので未保存にしない
            if (!IsSceneEditable() || !changed)
            {
                return;
            }
            m_editor.MarkEdited();
        }

        bool LevelEditorController::FinishEditBaselines()
        {
            // 実体の Transform は既に動いている。シーンに書く欄の値が控えと同じなら変わっていない
            // .actor へ書く欄は、.actor を書き直して値が変わってもシーンの変更にしない
            bool changed = false;
            for (const TransformEditBaseline& entry : m_editBaselines)
            {
                SceneActor* actor = m_scene.Actors().FindByActorId(entry.id);
                if (actor == nullptr)
                {
                    continue;
                }
                // 保存は上書きの一覧の項目だけを書くので、動かした姿勢の項目を一覧に足す
                if (AddChangedSceneOverrides(actor, entry.fields))
                {
                    changed = true;
                }
            }
            m_editBaselines.clear();
            return changed;
        }

        void LevelEditorController::BeginClassObjEdit() noexcept
        {
            if (m_classObjEditing)
            {
                return;
            }
            std::optional<nlohmann::json> baseline = CaptureActor(m_scene.Actors(), m_selectedActorId);
            if (!baseline)
            {
                return;
            }
            m_classObjEditBaseline = std::move(*baseline);
            m_classObjEditBaselineId = m_selectedActorId;
            m_classObjEditing = true;

            // 主対象の子孫は、ギズモと同じ控えと動かす処理で主対象に付いて動く。ほかに選んだ物は動かさない
            // ギズモのドラッグ中は控えをギズモが使っているので触らない。プレイ中は子孫の値を編集へ持ち帰れないので動かさない
            std::vector<uint32> descendants = CollectWithDescendants(m_scene.Actors(), {m_selectedActorId});
            descendants.erase(descendants.begin());
            if (m_transformEditing || !IsSceneEditable() || descendants.empty())
            {
                return;
            }
            CaptureEditBaselines(descendants);
            CaptureDragFollowers({m_selectedActorId});
            m_classObjFollowing = true;
        }

        void LevelEditorController::CommitClassObjEdit() noexcept
        {
            CommitActorFileEdits();
            if (!m_classObjEditing)
            {
                return;
            }
            m_classObjEditing = false;

            // リフレクション編集は実体の部品へ直接入っている。今の姿が控えと違えば変わった
            const std::optional<nlohmann::json> after = CaptureActor(m_scene.Actors(), m_classObjEditBaselineId);
            bool changed = after && *after != m_classObjEditBaseline;
            if (m_classObjFollowing)
            {
                m_classObjFollowing = false;
                m_dragFollowers.clear();
                if (FinishEditBaselines())
                {
                    changed = true;
                }
            }

            // プレイ中の値は、MirrorPlayEditToBaseline が数え、編集へ戻る時に LeavePlayForEdit が未保存にする
            if (!IsSceneEditable() || !changed)
            {
                return;
            }
            m_editor.MarkEdited();
        }

        bool LevelEditorController::ReparentActor(uint32 child, uint32 parent)
        {
            if (!IsSceneEditable() || !CanReparent(m_scene.Actors(), child, parent))
            {
                return false;
            }
            SceneActor* actor = m_scene.Actors().FindByActorId(child);
            if (actor == nullptr || actor->TransformObj().Parent().id == parent)
            {
                return false;
            }
            if (parent != k_NoActorId && m_scene.Actors().FindByActorId(parent) == nullptr)
            {
                return false;
            }
            // 位置は世界座標のままなので、親を替えても子は動かない
            actor->TransformObj().SetParent(ActorRef{parent});
            FinishFieldWrite(actor, FieldType::ActorRef);
            m_editor.MarkEdited();
            return true;
        }
    } // namespace Editor
} // namespace NS
