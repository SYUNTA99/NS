#include "Editor/LevelEditorController.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"

#include "Editor/AssetPicker.h"
#include "Editor/EditorActors.h"
#include "Editor/GridMath.h"
#include "Editor/HitZoneColors.h"
#include "Editor/LevelFilePaths.h"
#include "Editor/Undo/ActorSnapshotCommand.h"
#include "Editor/Undo/CompositeCommand.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/HitZones.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/PropActor.h"
#include "Game/Level/SlamAim.h"
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
#include "NSlib/CoreObject/ClassObjs/ThirdPersonFollow.h"
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

            // プレイ開始の凍結と編集へ戻る時の凍結を 1 体ずつ比べ、違う物を戻せる記録にする
            // 凍結へ入るのはプレイ中にインスペクタで写した欄だけなので、プレイで動いた値は比べに出ない
            std::vector<std::unique_ptr<ICommand>> MakePlayEditCommands(Scene& scene,
                                                                        const nlohmann::json& playStart,
                                                                        const nlohmann::json& playEnd)
            {
                std::vector<std::unique_ptr<ICommand>> commands;
                const nlohmann::json& startObjects = SceneJsonActors(playStart);
                for (const nlohmann::json& after : SceneJsonActors(playEnd))
                {
                    const uint32 id = ActorJsonId(after);
                    const std::size_t index = FindActorIndexById(playStart, id);
                    if (index == k_NoActorIndex)
                    {
                        continue;
                    }
                    const nlohmann::json& before = startObjects[index];
                    if (before == after)
                    {
                        continue;
                    }
                    commands.push_back(std::make_unique<ActorSnapshotCommand>(
                        scene, id, std::optional<nlohmann::json>{before}, std::optional<nlohmann::json>{after}));
                }
                return commands;
            }
        } // namespace

        LevelEditorController::LevelEditorController(Scene& scene) noexcept : m_scene(scene)
        {
            // シーンだけで決まる物は Setup の前から使えるよう、ここで差す。入力は Application が要るので Setup で差す
            m_editor.SetActors(&m_scene.Actors());
            m_editor.SetSceneCamera(&MainCamera());
            m_editor.SetCaptureLevelFn([this]() { return m_scene.ToJson(); });
            m_editor.SetScene(&m_scene);
            m_editor.SetSceneEditableFn([this]() { return IsSceneEditable(); });
            m_editor.SetLoadLevelFn([this](nlohmann::json&& fresh) {
                CommitTransformEdit();
                CommitClassObjEdit();
                ClearAssetSelection();
                m_editor.Undo().Clear();
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

        namespace
        {
            class EnvironmentCommand final : public ICommand
            {
            public:
                EnvironmentCommand(Scene& scene, SceneEnvironment before, SceneEnvironment after)
                    : m_scene(scene), m_before(std::move(before)), m_after(std::move(after))
                {}

                void Do() noexcept override { m_scene.SetEnvironment(m_after); }
                void Undo() noexcept override { m_scene.SetEnvironment(m_before); }
                [[nodiscard]] std::size_t EstimatedBytes() const noexcept override
                {
                    return sizeof(*this) + m_before.skybox.size() + m_after.skybox.size() + m_before.music.name.size() +
                           m_after.music.name.size();
                }

            private:
                Scene& m_scene;
                SceneEnvironment m_before;
                SceneEnvironment m_after;
            };

            // actor の全部の部品の全部の欄の値。シーンの JSON と同じく classObjs の下に部品の鍵で並べる
            nlohmann::json AllFieldValues(const SceneActor& actor)
            {
                nlohmann::json values = nlohmann::json::object();
                nlohmann::json& parts = ActorJsonClassObjs(values);
                ForEachReflectedClassObj(&actor, [&](const ClassObjInfo& row, const Object* part) {
                    parts[std::string{row.key}] = SerializeClassObjFields(part);
                });
                return values;
            }

            // before (AllFieldValues の形) から値が変わった、シーンに書く項目を actor の上書きの一覧に足す
            // 参照の欄はいつも保存するので足さない。.actor へ書く項目は変わっていても触らない
            void AddChangedSceneOverrides(SceneActor& actor, const nlohmann::json& before)
            {
                ForEachReflectedClassObj(&actor, [&](const ClassObjInfo& row, const Object* part) {
                    const nlohmann::json now = SerializeClassObjFields(part);
                    const nlohmann::json* was = ClassObjFields(before, row.key);
                    for (nlohmann::json::const_iterator entry = now.begin(); entry != now.end(); ++entry)
                    {
                        // 触ったかを見る比べなので、値の写しどうしを == で比べる
                        if (was != nullptr && was->contains(entry.key()) && was->at(entry.key()) == entry.value())
                        {
                            continue;
                        }
                        const FieldDesc* field = FindField(part->GetReflection(), entry.key());
                        if (field == nullptr || field->type == FieldType::ActorRef ||
                            FieldDestinationOf(&actor, *field) != FieldDestination::Scene)
                        {
                            continue;
                        }
                        actor.AddSceneOverride(row.key, entry.key());
                    }
                });
            }
        } // namespace

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
                m_editor.Undo().Push(
                    std::make_unique<EnvironmentCommand>(m_scene, m_scene.Environment(), std::move(after)));
            }
            return true;
        }

        const SoundName& LevelEditorController::SceneMusic() const noexcept
        {
            return m_scene.Environment().music;
        }

        float LevelEditorController::SceneMusicVolume() const noexcept
        {
            return m_scene.Environment().musicVolume;
        }

        bool LevelEditorController::SetSceneMusic(const SoundName& music, float volume)
        {
            if (!IsSceneEditable() || !std::isfinite(volume))
            {
                return false;
            }
            SceneEnvironment after = m_scene.Environment();
            after.music = music;
            after.musicVolume = ClampMusicVolume(volume);
            if (after.music.name != m_scene.Environment().music.name ||
                after.musicVolume != m_scene.Environment().musicVolume)
            {
                m_editor.Undo().Push(
                    std::make_unique<EnvironmentCommand>(m_scene, m_scene.Environment(), std::move(after)));
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
                startCenter = focusActor->Root().Position();
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
            m_dragFollowers.clear();
            m_gizmoWasDragging = false;
            m_gizmo.SetActive(false);

            // 実体が唯一の出所なので組み直しは要らない。編集で動いた当たりは、世界を止めた歩が揃えている
            // 編集へ戻る時の姿として、コースを始めてメッセージが値を動かす前のシーンを JSON に書き出す
            // 1 回だけ書き出して写すので、組み直す JSON と比べる写しはずれない
            m_playBaseline = m_scene.ToJson();
            m_playStartBaseline = m_playBaseline;
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
            return m_editor.HasUnsavedChanges() || (m_mode == Mode::Play && m_playBaseline && m_playStartBaseline &&
                                                    *m_playBaseline != *m_playStartBaseline);
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
                // プレイで動いた分を編集の変更として履歴に積まない
                baseline = m_scene.ToJson();
                m_playStartBaseline.reset();
            }
            std::vector<std::unique_ptr<ICommand>> playEdits;
            if (m_playStartBaseline)
            {
                playEdits = MakePlayEditCommands(m_scene, *m_playStartBaseline, baseline);
                m_playStartBaseline.reset();
            }
            // 履歴は残す。持っているアクターは組み直しが捨てる管理クラスを指すので、先に解放して JSON から戻す形にする
            m_editor.Undo().ReleaseHeldActors();
            m_scene.ResetFromJson(std::move(baseline));
            // プレイ中の変更は 1 つずつ積まず、戻る時に 1 まとまりで積む。実体は組み直しで既に変えた後の姿
            if (!playEdits.empty())
            {
                m_editor.Undo().Record(MakeUndoUnit(std::move(playEdits)));
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
            // 編集入力は表示矩形から光線を飛ばす。カーソルが重なっていない間は配置位置を表示しない
            m_editor.SetViewRect(m_gameViewRect);
            m_editor.SetViewHovered(hovered);
        }

        void LevelEditorController::ClearGameView() noexcept
        {
            // 全画面描画では予備の矩形を使い、カーソルが重なっている状態にする
            m_gameViewRectValid = false;
            m_gameViewHovered = true;
            m_gameViewHidden = false;
            m_editor.SetViewRect(CurrentViewRect());
            m_editor.SetViewHovered(true);
        }

        void LevelEditorController::HideGameView() noexcept
        {
            // 前面のパネルが裏へ隠れた。配置カーソルと編集オーバーレイを止める
            m_gameViewHidden = true;
            m_gameViewHovered = false;
            m_editor.SetViewHovered(false);
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
                if (m_editTool == EditTool::StageBuild)
                {
                    if (!m_editShortcutsBlocked)
                    {
                        SetEditTool(EditTool::Gizmo);
                    }
                    return;
                }
                if (GizmoToolActive() && m_gizmo.Selected() != nullptr)
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

            // 同時に1つの道具だけがクリックと R を使う。グリッド入力はグリッド配置中だけ受ける。
            // モード切替は EditorLayer の UI ボタンが SetGizmoToolActive で入れる。Tab は Edit↔Play 専用
            const bool objectMode = GizmoToolActive();
            m_gizmo.SetActive(objectMode);
            m_editor.SetInputSuppressed(m_editTool != EditTool::Grid);
            TickStageBuild(presses);

            if (objectMode)
            {
                // 追従カメラの Root を実プレイ視点位置へ寄せてからギズモを回す。判定箱とギズモがその位置に出る
                SyncFollowCameraPoses();

                // 組み直しで破棄されたアクターを参照しないよう、選択を現在の実体へ引き直す
                ResolveSelectionFromId();

                const Matrix vp = Cameras().ViewProjection();
                const bool wasDragging = m_gizmoWasDragging;
                m_gizmo.Tick(
                    vp, CurrentViewRect(), m_gameViewHovered, m_editShortcutsBlocked, presses, m_scene.Actors());

                // ビューポートでのギズモ選択変化を選択 id と Inspector が見る派生添字へ追従させる
                CaptureSelectionFromGizmo();

                // 追従カメラを掴んでいたら Root 位置を初期姿勢へ逆算し ThirdPersonFollow
                // へ書き戻す。位置の書き戻しはこちら
                ApplyFollowCameraGizmoDrag();

                // ドラッグ開始で baseline を控え、終了で変わった分のスナップショットを履歴へ積む。grid undo と同じ経路
                // 追従カメラの引きは初期姿勢の欄を変える。控えは部品の値まで写すので同じ道で積める
                const bool nowDragging = m_gizmo.IsDragging();
                if (!wasDragging && nowDragging)
                {
                    BeginTransformEdit();
                    // 掴んだ瞬間はまだ動いていない。ここで控えれば差分の基準が揃う
                    CaptureDragFollowers();
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
            }
            else if (m_transformEditing)
            {
                // Object モードを抜けても未確定の変形があれば確定し、記録されない変更を残さない
                CommitTransformEdit();
                m_gizmoWasDragging = false;
            }

            // 掴んでいる間は選択の貼り直しを飛ばすので、undo が旧 Transform を指す
            m_editor.SetUndoRedoSuppressed(m_gizmo.IsDragging());
            m_editor.Tick(m_editShortcutsBlocked, presses);
            if (m_editor.IsLevelDirty())
            {
                // grid 編集・undo・読込はコマンド / reload がアクターを組み直し済。gizmo を今の実体へ貼り直す
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

            m_editor.RenderCursorPreview();
            // Object モードはブラシを置かないので Build モードの時だけ出す
            // Game ビュー前面などで編集ビューが隠れているフレームは、ゲーム画面へ被せないよう出さない
            if (m_editTool == EditTool::Grid && !m_gameViewHidden)
            {
                m_editor.Palette().Render(CurrentViewRect());
            }
            // Object モードのギズモは最前面の drawlist に重ねる
            if (m_gizmo.IsActive())
            {
                m_gizmo.Render(Cameras().ViewProjection(), CurrentViewRect());
            }
        }

        void LevelEditorController::SetGizmoToolActive(bool active) noexcept
        {
            if (active)
            {
                SetEditTool(EditTool::Gizmo);
                return;
            }
            SetEditTool(EditTool::Grid);
        }

        void LevelEditorController::SetEditTool(EditTool tool) noexcept
        {
            if (tool == m_editTool)
            {
                return;
            }
            CommitTransformEdit();
            m_editTool = tool;
            m_stagePlacement.reset();
            if (tool != EditTool::Gizmo)
            {
                SelectActorById(k_NoActorId);
            }
        }

        void LevelEditorController::TickStageBuild(const OS::InputPresses& presses)
        {
            m_stagePlacement.reset();
            if (m_editTool != EditTool::StageBuild || m_editShortcutsBlocked || !m_gameViewHovered || m_gameViewHidden)
            {
                return;
            }
            Application* app = Application::Get();
            if (app == nullptr)
            {
                return;
            }
            OS::Mouse& mouse = app->Input().Mouse();
            if (mouse.IsHeld(OS::MouseButton::Right) || mouse.IsHeld(OS::MouseButton::Middle))
            {
                return;
            }
            if (presses.WasPressed(OS::Key::R))
            {
                m_stageBuilder.YawDegrees() += 90.0f;
            }
            const ViewRect view = CurrentViewRect();
            int mouseX = 0;
            int mouseY = 0;
            WindowMouseToViewSpace(mouse.GetX(), mouse.GetY(), mouseX, mouseY);
            if (!ViewRectContains(view, mouseX, mouseY))
            {
                return;
            }
            int localX = 0;
            int localY = 0;
            ViewRectToLocal(view, mouseX, mouseY, localX, localY);
            const Ray ray = ScreenToWorldRay(MainCamera().ViewProjection(), ViewRectSize(view), localX, localY);
            m_stagePlacement = m_stageBuilder.Placement(ray, m_scene.Physics());
            if (m_stagePlacement && presses.WasPressed(OS::MouseButton::Left))
            {
                (void)PlaceStagePart(ray);
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

        ThirdPersonFollow* LevelEditorController::SelectedFollowCamera() noexcept
        {
            if (GL::Level::FollowCameraActor* camera = Cast<GL::Level::FollowCameraActor>(SelectedActor()))
            {
                return &camera->Vcam();
            }
            return nullptr;
        }

        void LevelEditorController::SyncFollowCameraPoses()
        {
            // 追従カメラは位置を持たないので、edit 中は実プレイの視点位置へ Root を寄せて frustum / pick / ギズモを出す
            // ドラッグ中の選択カメラだけは gizmo が Root を握るため触らず、その位置を初期姿勢へ逆算する側に任せる
            const ActorList& objects = m_scene.Actors();
            for (SceneActor* object : objects)
            {
                GL::Level::FollowCameraActor* camera = Cast<GL::Level::FollowCameraActor>(object);
                if (camera == nullptr)
                {
                    continue;
                }
                ThirdPersonFollow* follow = &camera->Vcam();
                if (m_gizmo.IsDragging() && object->Id() == m_selectedActorId)
                {
                    continue;
                }
                object->Root().SetPosition(follow->EvaluatePose(1.0f).position);
            }
        }

        void LevelEditorController::ApplyFollowCameraGizmoDrag()
        {
            // ドラッグ中の追従カメラは、gizmo が動かした Root 位置から初期姿勢の方位角・仰角/距離を逆算して
            // 実体の部品へ書き戻す。Root 位置は初期姿勢由来なので保存対象は部品側になる
            if (!m_gizmo.IsDragging())
            {
                return;
            }
            ThirdPersonFollow* follow = SelectedFollowCamera();
            if (follow == nullptr)
            {
                return;
            }
            SceneActor* go = SelectedActor();
            if (go == nullptr)
            {
                return;
            }
            follow->SetInitialPoseFromCameraPosition(go->Root().Position());
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

            // ハンドルを出すため Object ツールへ切替える。Build のままだとギズモが描かれない
            SetGizmoToolActive(true);

            // player も含め全アクターが ActorList に居る。選択した Root を id で引いてギズモへ貼る
            if (SceneActor* go = m_scene.Actors().FindByActorId(id))
            {
                m_gizmo.SetSelected(&go->Root());
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
                if (&object->Root() == selected)
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
                    Transform* root = &go->Root();
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

        uint32 LevelEditorController::PlaceStagePart(const Ray& ray)
        {
            if (!IsSceneEditable())
            {
                return k_NoActorId;
            }
            const std::optional<Vector3> center = m_stageBuilder.Placement(ray, m_scene.Physics());
            if (!center)
            {
                return k_NoActorId;
            }
            std::vector<nlohmann::json> objects;
            objects.push_back(m_stageBuilder.MakeActor(*center));
            const std::vector<uint32> created = m_editor.CreateActors(std::move(objects));
            if (created.empty())
            {
                return k_NoActorId;
            }
            return created.front();
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
            SetGizmoToolActive(true);
            ResolveSelectionFromId();
        }

        void LevelEditorController::RenameActor(uint32 id, std::string_view name)
        {
            if (!IsSceneEditable() || id == k_NoActorId)
            {
                return;
            }
            SceneActor* object = m_scene.Actors().FindByActorId(id);
            std::optional<nlohmann::json> before = CaptureActor(m_scene.Actors(), id);
            if (object == nullptr || !before)
            {
                return;
            }

            // 実体の名前を直接変える。他のアクターと重なれば番号が付く。参照は id で持つので切れない
            const std::string previous = object->Name();
            m_scene.Actors().RenameActor(object, name);
            if (object->Name() == previous)
            {
                return; // 同じ名前で履歴を汚さない
            }

            std::optional<nlohmann::json> after = CaptureActor(m_scene.Actors(), id);
            // 実体は既に after なので Do を呼ばず履歴だけ積む
            m_editor.Undo().Record(
                std::make_unique<ActorSnapshotCommand>(m_scene, id, std::move(before), std::move(after)));

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

            std::vector<std::unique_ptr<ICommand>> commands;
            commands.reserve(m_selectionIds.size());
            for (const uint32 victim : m_selectionIds)
            {
                std::optional<nlohmann::json> before = CaptureActor(m_scene.Actors(), victim);
                if (!before)
                {
                    continue;
                }
                commands.push_back(
                    std::make_unique<ActorSnapshotCommand>(m_scene, victim, std::move(before), std::nullopt));
            }
            if (commands.empty())
            {
                return;
            }

            m_editor.Undo().Push(MakeUndoUnit(std::move(commands)));

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
                const Matrix world = object->Root().WorldMatrix();
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

        void LevelEditorController::CaptureDragFollowers() noexcept
        {
            m_dragFollowers.clear();
            if (m_selectionIds.size() < 2)
            {
                return;
            }

            SceneActor* primary = m_scene.Actors().FindByActorId(m_selectedActorId);
            if (primary == nullptr)
            {
                return;
            }
            m_dragPrimaryWorld = primary->Root().WorldMatrix();

            for (const uint32 id : m_selectionIds)
            {
                if (id == m_selectedActorId)
                {
                    continue;
                }
                SceneActor* object = m_scene.Actors().FindByActorId(id);
                if (object == nullptr)
                {
                    continue;
                }

                m_dragFollowers.push_back(DragFollower{id, object->Root().WorldMatrix()});
            }
        }

        void LevelEditorController::ApplyDragToFollowers() noexcept
        {
            if (m_dragFollowers.empty())
            {
                return;
            }
            SceneActor* primary = m_scene.Actors().FindByActorId(m_selectedActorId);
            if (primary == nullptr)
            {
                return;
            }

            // 主対象が動いた分をワールド空間の差分として取り、残りへ同じだけ効かせる
            const Matrix delta = m_dragPrimaryWorld.Invert() * primary->Root().WorldMatrix();
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
                object->Root().SetPosition(position);
                object->Root().SetRotation(rotation);
                object->Root().SetScale(scale);
            }
        }

        void LevelEditorController::BeginTransformEdit() noexcept
        {
            if (m_transformEditing)
            {
                return;
            }
            m_editBaselines.clear();
            // 選択している分をまとめて控える。動かなかった物は確定時に落ちる
            for (const uint32 id : m_selectionIds)
            {
                std::optional<nlohmann::json> baseline = CaptureActor(m_scene.Actors(), id);
                if (!baseline)
                {
                    continue;
                }
                // 控えが取れたので、m_scene にこのアクターが居る
                const SceneActor* actor = m_scene.Actors().FindByActorId(id);
                if (actor != nullptr)
                {
                    m_editBaselines.push_back(TransformEditBaseline{id, std::move(*baseline), AllFieldValues(*actor)});
                }
            }
            if (m_editBaselines.empty())
            {
                return;
            }
            m_transformEditing = true;
        }

        void LevelEditorController::CommitTransformEdit() noexcept
        {
            if (!m_transformEditing)
            {
                return;
            }
            m_transformEditing = false;

            // ドラッグは実体の Root を既に動かしている。after は実体の忠実な写し。baseline と同じなら履歴に積まない
            std::vector<std::unique_ptr<ICommand>> commands;
            for (TransformEditBaseline& entry : m_editBaselines)
            {
                const uint32 id = entry.id;
                nlohmann::json& baseline = entry.snapshot;
                // 保存は上書きの一覧の項目だけを書くので、動かした姿勢の項目を一覧に足してから後の姿を控える
                if (SceneActor* actor = m_scene.Actors().FindByActorId(id))
                {
                    AddChangedSceneOverrides(*actor, entry.fields);
                }
                std::optional<nlohmann::json> after = CaptureActor(m_scene.Actors(), id);
                if (!after || *after == baseline)
                {
                    continue;
                }
                commands.push_back(std::make_unique<ActorSnapshotCommand>(m_scene, id, baseline, std::move(*after)));
            }
            m_editBaselines.clear();
            // プレイ中の控えはプレイの姿。編集へ戻った後に戻すと、プレイの位置を編集の物へ書く
            if (!IsSceneEditable() || commands.empty())
            {
                return;
            }

            // 実体は既に after なので Do を呼ばず履歴だけ積む。取り消し で baseline へ、やり直し で after へ戻す
            m_editor.Undo().Record(MakeUndoUnit(std::move(commands)));
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
        }

        void LevelEditorController::CommitClassObjEdit() noexcept
        {
            CommitActorFileEdits();
            if (!m_classObjEditing)
            {
                return;
            }
            m_classObjEditing = false;

            // リフレクション編集は実体の部品へ直接入っている。after は実体の忠実な写しで、baseline と同じなら積まない
            // プレイ中の控えはプレイの姿なので積まない
            if (!IsSceneEditable())
            {
                return;
            }
            std::optional<nlohmann::json> after = CaptureActor(m_scene.Actors(), m_classObjEditBaselineId);
            if (!after || *after == m_classObjEditBaseline)
            {
                return;
            }

            // 実体は既に after なので Do を呼ばず履歴だけ積む
            m_editor.Undo().Record(std::make_unique<ActorSnapshotCommand>(
                m_scene, m_classObjEditBaselineId, m_classObjEditBaseline, std::move(*after)));
        }
    } // namespace Editor
} // namespace NS
