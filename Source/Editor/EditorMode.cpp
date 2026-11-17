#include "Editor/EditorMode.h"
#include "NSlib/Core/Types.h"

#include "Editor/EditorActors.h"
#include "Editor/EditorUi.h"
#include "Editor/LevelFilePaths.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/Input.h"
#include "NSlib/Core/Windows/Keyboard.h"

#include <imgui.h>

#include <memory>
#include <optional>
#include <span>
#include <unordered_map>

namespace NS
{
    namespace Editor
    {
        namespace
        {
            constexpr float k_StatusToastSeconds = 2.5f;
        } // namespace

        bool EditorMode::IsSceneEditable() const noexcept { return !m_sceneEditable || m_sceneEditable(); }

        void EditorMode::Tick(bool editShortcutsBlocked, const OS::InputPresses& presses) noexcept
        {
            if (!IsSceneEditable() || editShortcutsBlocked)
            {
                return;
            }
            HandleSaveLoadInput(presses);
        }

        void EditorMode::HandleSaveLoadInput(const OS::InputPresses& presses) noexcept
        {
            if (m_input == nullptr)
            {
                return;
            }

            const OS::Keyboard& kb = m_input->Keyboard();
            if (!kb.IsHeld(OS::Key::Ctrl))
            {
                return;
            }

            const bool shift = kb.IsHeld(OS::Key::Shift);
            if (presses.WasPressed(OS::Key::S))
            {
                // Shift 付きは常に名前を付けて保存、素の Ctrl+S は上書き (名前が無ければ付けて保存へ落ちる)
                if (shift)
                {
                    OpenSaveModal();
                }
                else
                {
                    RequestSave();
                }
            }
        }

        void EditorMode::RequestSave() noexcept
        {
            if (m_currentLevelName.empty())
            {
                OpenSaveModal();
            }
            else
            {
                OverwriteCurrentLevel();
            }
        }

        bool EditorMode::SaveLevelToName(std::string_view name) noexcept
        {
            // プレイ中の実体はプレイで動いた後の姿で、編集の内容ではないので書かない
            if (!IsSceneEditable() || !m_captureLevel)
            {
                return false;
            }

            // 素の名前は Scenes/ 配下に直してから控える。保存先と現在名の指すファイルを一致させる
            const std::string safe = QualifyLevelPath(SanitizeLevelPath(name));
            const std::optional<std::string> path = BuildLevelPath(safe);
            if (safe.empty() || !path)
            {
                return false;
            }

            (void)EnsureScenesDirectoryExists();

            // 保存の出所は実体。捕捉関数で実体からシーンの JSON 文書を作って書く
            const nlohmann::json snapshot = m_captureLevel();
            const bool ok = SaveSceneToJsonFile(snapshot, *path);
            if (ok)
            {
                m_currentLevelName = safe;
                m_savedEditVersion = EditVersion();
                RememberCurrentLevel();
            }
            return ok;
        }

        void EditorMode::OverwriteCurrentLevel() noexcept
        {
            const bool ok = SaveLevelToName(m_currentLevelName);
            const char* prefix = "保存失敗: ";
            if (ok)
            {
                prefix = "上書き保存: ";
            }

            m_statusMessage = prefix + m_currentLevelName;
            m_statusError = !ok;
            m_statusTimer = k_StatusToastSeconds;
        }

        std::string EditorMode::QuitSaveLevelName() const noexcept
        {
            if (!m_currentLevelName.empty())
            {
                return m_currentLevelName;
            }

            // 起動レベルが実在するのに読めず seed で立ち上がった時は、元ファイルを潰さないよう退避名へ逃がす
            if (m_bootLevelLoadFailed)
            {
                return "Scenes/recovered_level";
            }

            // 通常起動 / 新規は同梱の起動シーンへ書き戻す
            return StartLevelName();
        }

        bool EditorMode::SaveForQuit() noexcept
        {
            const bool recovering = m_currentLevelName.empty() && m_bootLevelLoadFailed;
            const std::string name = QuitSaveLevelName();
            const bool ok = SaveLevelToName(name);
            if (recovering)
            {
                const char* prefix = "退避保存失敗: ";
                if (ok)
                {
                    prefix = "退避保存: ";
                }
                m_statusMessage = std::string(prefix) + name;
                m_statusError = !ok;
                m_statusTimer = k_StatusToastSeconds;
            }
            return ok;
        }

        CreateLevelResult EditorMode::CreateLevel(std::string_view name) noexcept
        {
            if (!IsSceneEditable())
            {
                return CreateLevelResult::NotEditable;
            }
            const std::optional<std::string> path = BuildLevelPath(name);
            if (!path)
            {
                return CreateLevelResult::BadPath;
            }
            if (OS::FileSystem::Exists(*path))
            {
                return CreateLevelResult::AlreadyExists;
            }
            if (!SaveSceneToJsonFile(MakeSceneJson(), *path))
            {
                return CreateLevelResult::WriteFailed;
            }
            return CreateLevelResult::Created;
        }

        bool EditorMode::OpenLevel(std::string_view name) noexcept
        {
            // プレイ中に組み直すと、編集へ戻る組み直しがプレイ開始の写しで上書きし、名前だけが新しいレベルを指す
            if (!IsSceneEditable())
            {
                return false;
            }
            const std::optional<std::string> path = BuildLevelPath(name);
            if (!path || !m_loadLevel)
            {
                return false;
            }

            nlohmann::json fresh;
            if (!LoadSceneFromJsonFile(fresh, *path))
            {
                // 起動レベルが実在するのに読めない時は、終了の保存が元のファイルを潰さないよう退避の名前へ逃がす
                if (m_currentLevelName.empty() && OS::FileSystem::Exists(*path))
                {
                    m_bootLevelLoadFailed = true;
                }
                return false;
            }

            m_loadLevel(std::move(fresh));
            m_savedEditVersion = EditVersion();
            m_levelDirty = true;
            m_currentLevelName = QualifyLevelPath(SanitizeLevelPath(name));
            RememberCurrentLevel();
            return true;
        }

        bool EditorMode::OpenStartupLevel(std::string_view fallback) noexcept
        {
            if (!m_sessionPath.empty())
            {
                const std::optional<std::string> text = OS::FileSystem::ReadAllText(m_sessionPath);
                if (text)
                {
                    const nlohmann::json session = nlohmann::json::parse(*text, nullptr, false);
                    if (session.is_object() && session.contains("level") && session["level"].is_string())
                    {
                        const std::string name = session["level"].get<std::string>();
                        if (!name.empty() && OpenLevel(name))
                        {
                            return true;
                        }
                    }
                    NS_LOG_WARN(App, "前回のシーンを開けないため通常の起動シーンを開く: {}", m_sessionPath);
                }
            }
            return OpenLevel(fallback);
        }

        void EditorMode::RememberCurrentLevel() const noexcept
        {
            if (m_sessionPath.empty() || m_currentLevelName.empty())
            {
                return;
            }
            const std::string text = nlohmann::json{{"level", m_currentLevelName}}.dump(2) + "\n";
            if (!OS::FileSystem::ReplaceAllBytes(m_sessionPath,
                                                 std::as_bytes(std::span{text.data(), text.size()})))
            {
                NS_LOG_WARN(App, "開いているシーンを次回起動用に記録できない: {}", m_sessionPath);
            }
        }

        void EditorMode::RenderFileBrowser() noexcept
        {
            const LevelFileBrowser::Result result = m_fileBrowser.Render();
            switch (result.action)
            {
            case LevelFileBrowser::Action::RequestSave:
            {
                const bool ok = SaveLevelToName(result.targetName);
                const char* message = "保存失敗";
                if (ok)
                {
                    message = "保存成功";
                }
                m_fileBrowser.NotifySaveResult(ok, message);
                break;
            }
            case LevelFileBrowser::Action::None:
            default:
                break;
            }

            // 上書き保存などモーダルを開かずに終わった操作をトーストで知らせる
            if (m_statusTimer > 0.0f)
            {
                m_statusTimer -= OS::FrameTimer::DeltaSeconds();
                ImGuiViewport* const vp = ImGui::GetMainViewport();
                if (vp != nullptr)
                {
                    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 12.0f),
                                            ImGuiCond_Always,
                                            ImVec2(0.5f, 0.0f));
                    ImGui::SetNextWindowBgAlpha(0.75f);

                    constexpr ImGuiWindowFlags k_SaveToastFlags =
                        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings;

                    if (ImGui::Begin("##save_toast", nullptr, k_SaveToastFlags))
                    {
                        ImVec4 color = k_MsgOkColor;
                        if (m_statusError)
                        {
                            color = k_MsgErrorColor;
                        }
                        ImGui::TextColored(color, "%s", m_statusMessage.c_str());
                    }
                    ImGui::End();
                }
            }
        }

        std::vector<uint32> EditorMode::CreateActors(std::vector<nlohmann::json> objects)
        {
            // プレイ中に作った物は、編集へ戻る組み直しで消えるので作らない
            std::vector<uint32> created;
            if (!IsSceneEditable() || m_scene == nullptr || objects.empty())
            {
                return created;
            }

            // 写した元の id から新しい id への表。一緒に作る物どうしの参照を付け替える
            std::unordered_map<uint32, uint32> idMap;
            created.reserve(objects.size());
            for (nlohmann::json& object : objects)
            {
                const uint32 sourceId = ActorJsonId(object);
                const uint32 id = m_scene->Actors().AllocateActorId();
                if (sourceId != k_NoActorId)
                {
                    idMap.emplace(sourceId, id);
                }
                SetActorJsonId(object, id);
                created.push_back(id);
            }

            for (std::size_t i = 0; i < objects.size(); ++i)
            {
                RemapActorRefs(objects[i], idMap);
                // 組めない姿は Scene が JSON のまま持って保存に残すので、作れたかは確かめない
                (void)m_scene->SpawnFromJson(objects[i]);
            }
            MarkEdited();
            m_levelDirty = true;
            return created;
        }
    } // namespace Editor
} // namespace NS
