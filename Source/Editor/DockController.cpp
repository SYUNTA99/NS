#include "Editor/DockController.h"

#include "Editor/PanelIds.h"
#include "NSlib/Core/Windows/Filesystem.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>
#include <span>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#include <imgui_internal.h>
#endif

namespace NS::Editor
{
#if NS_EDITOR_ENABLED
        namespace
        {
            const char* const k_Panels[] = {k_PanelScene, k_PanelGame, k_PanelActorList, k_PanelInspector,
                                             k_PanelConsole, k_PanelAssets, k_PanelEditMode, k_PanelActionTimeline};
            const ImGuiID k_DockspaceId = ImHashStr("EditorDockSpaceV12");

            void SelectDefaultTab(ImGuiID nodeId, const char* windowName)
            {
                ImGuiDockNode* node = ImGui::DockBuilderGetNode(nodeId);
                if (node == nullptr)
                {
                    return;
                }
                node->SelectedTabId = ImHashStr("#TAB", 4, ImHashStr(windowName));
            }

            // ホスト窓の位置 / サイズは呼び出し側 (帯の高さを差し引いた領域) が決める
            void BuildDefaultDockLayout(ImGuiID dockspaceId, ImVec2 hostPos, ImVec2 hostSize)
            {
                ImGui::DockBuilderRemoveNode(dockspaceId);
                ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodePos(dockspaceId, hostPos);
                // SplitNode の前に必須、後だと分割比が信頼できない
                ImGui::DockBuilderSetNodeSize(dockspaceId, hostSize);

                // 割る順番で形が決まる。右列を縦いっぱいにするため先に切り出す
                ImGuiID centerId = dockspaceId;
                ImGuiID rightId = 0;
                ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, 0.25f, &rightId, &centerId);
                // 残りの下段は個体一覧と Scene の両方にまたがる帯にする
                ImGuiID bottomId = 0;
                ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Down, 0.28f, &bottomId, &centerId);
                ImGuiID leftId = 0;
                ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, 0.24f, &leftId, &centerId);

                // Edit Mode と個体一覧は左列でタブに重ねる。前面タブは下の SelectDefaultTab が決める
                ImGui::DockBuilderDockWindow(k_PanelEditMode, leftId);
                ImGui::DockBuilderDockWindow(k_PanelActorList, leftId);
                ImGui::DockBuilderDockWindow(k_PanelInspector, rightId);
                ImGui::DockBuilderDockWindow(k_PanelConsole, bottomId);
                ImGui::DockBuilderDockWindow(k_PanelAssets, bottomId);
                ImGui::DockBuilderDockWindow(k_PanelActionTimeline, bottomId);
                // Scene と Game の初期配置は同じタブ群にする。分割後は両方の映像を描ける
                ImGui::DockBuilderDockWindow(k_PanelScene, centerId);
                ImGui::DockBuilderDockWindow(k_PanelGame, centerId);

                // ドック順や発行順に前面タブを委ねると Edit Mode が出てくるので明示する
                SelectDefaultTab(leftId, k_PanelActorList);
                SelectDefaultTab(bottomId, k_PanelAssets);
                SelectDefaultTab(centerId, k_PanelScene);

                ImGui::DockBuilderFinish(dockspaceId);
            }

        } // namespace
#endif

        void DockController::RenderDockSpaceHost(float topOffset, bool keepAliveOnly) noexcept
        {
#if NS_EDITOR_ENABLED
            ImGui::GetIO().ConfigDockingWithWideTargets = true;
            ImGui::GetIO().ConfigDockingTransparentPayload = true;
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            if (vp == nullptr)
            {
                return;
            }
            const ImVec2 hostPos{vp->WorkPos.x, vp->WorkPos.y + topOffset};
            const ImVec2 hostSize{std::max(1.0f, vp->WorkSize.x), std::max(1.0f, vp->WorkSize.y - topOffset)};

            ImGui::SetNextWindowViewport(vp->ID);
            ImGui::SetNextWindowPos(hostPos);
            ImGui::SetNextWindowSize(hostSize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
            constexpr ImGuiWindowFlags k_HostFlags =
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground |
                ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings;
            ImGui::Begin("##EditorDockHost", nullptr, k_HostFlags);
            ImGui::PopStyleVar();

            // 0 だと DockSpaceOverViewport 相当の自動 id 判定ができないので明示 id にする
            const ImGuiID dockspaceId = k_DockspaceId;
            if (!keepAliveOnly && !IsMaximized() &&
                (m_resetPending || ImGui::DockBuilderGetNode(dockspaceId) == nullptr))
            {
                // 保存済みの配置が無い場合と明示的なリセット時だけ組み直す
                BuildDefaultDockLayout(dockspaceId, hostPos, hostSize);
                m_resetPending = false;
                // このフレームは組むだけ。焦点を渡すのは全パネルが発行された次のフレーム
                m_dockFocusPending = 2;
            }
            ImGuiDockNodeFlags flags = ImGuiDockNodeFlags_None;
            if (keepAliveOnly || IsMaximized())
            {
                // 非表示のドックを発行しないと、次のフレームで所属するパネルが外れる
                flags = ImGuiDockNodeFlags_KeepAliveOnly;
            }
            ImGui::DockSpace(dockspaceId, ImVec2{0.0f, 0.0f}, flags);
            ImGui::End();
#else
            (void)topOffset;
            (void)keepAliveOnly;
#endif
        }

        void DockController::EnterMaximize(const char* name) noexcept
        {
#if NS_EDITOR_ENABLED
            if (IsMaximized())
            {
                ExitMaximize();
            }
            std::size_t iniSize = 0;
            const char* ini = ImGui::SaveIniSettingsToMemory(&iniSize);
            if (ini != nullptr)
            {
                m_savedImGuiIni.assign(ini, iniSize);
            }
            m_normalIniFilename = ImGui::GetIO().IniFilename;
            // 一時的な全面配置が通常の配置を上書きしないよう、自動保存を復元まで止める
            ImGui::GetIO().IniFilename = nullptr;
            m_dockFocusPending = 0;
            m_maximizedPanel = name;
#else
            (void)name;
#endif
        }

        void DockController::ExitMaximize() noexcept
        {
#if NS_EDITOR_ENABLED
            if (!IsMaximized())
            {
                return;
            }
            ImGui::GetIO().IniFilename = m_normalIniFilename;
            m_normalIniFilename = nullptr;
            if (!m_savedImGuiIni.empty())
            {
                ImGui::LoadIniSettingsFromMemory(m_savedImGuiIni.data(), m_savedImGuiIni.size());
                m_savedImGuiIni.clear();
            }
#endif
            m_maximizedPanel.clear();
        }

        void DockController::RenderMaximizeButton(bool shortcutsBlocked) noexcept
        {
#if NS_EDITOR_ENABLED
            if (shortcutsBlocked || !ImGui::GetIO().KeyShift ||
                !ImGui::IsKeyPressed(ImGuiKey_Space, false))
            {
                return;
            }
            if (IsMaximized())
            {
                ExitMaximize();
                return;
            }
            const ImGuiWindow* hovered = GImGui->HoveredWindow;
            if (hovered == nullptr)
            {
                return;
            }
            for (const char* panel : k_Panels)
            {
                const ImGuiWindow* window = ImGui::FindWindowByName(panel);
                if (window != nullptr && window == hovered->RootWindow)
                {
                    EnterMaximize(panel);
                    break;
                }
            }
#else
            (void)shortcutsBlocked;
#endif
        }

        void DockController::MovePanel(const char* panel, const char* target, Placement placement) noexcept
        {
#if NS_EDITOR_ENABLED
            ExitMaximize();
            ImGuiWindow* window = ImGui::FindWindowByName(panel);
            if (window == nullptr)
            {
                return;
            }
            if (placement == Placement::Floating)
            {
                ImGui::DockContextProcessUndockWindow(GImGui, window);
                const ImGuiViewport* viewport = ImGui::GetMainViewport();
                ImGui::SetWindowPos(panel, ImVec2{viewport->WorkPos.x + 60.0f, viewport->WorkPos.y + 60.0f});
                ImGui::SetWindowSize(panel, ImVec2{viewport->WorkSize.x * 0.5f, viewport->WorkSize.y * 0.6f});
            }
            else
            {
                ImGuiWindow* destination = ImGui::FindWindowByName(target);
                if (destination == nullptr || destination == window)
                {
                    return;
                }
                // 最後のタブを外すと隣の領域が統合されるため、配置先の識別子は外した後で読む
                ImGui::DockContextProcessUndockWindow(GImGui, window);
                ImGuiID nodeId = destination->DockId;
                if (nodeId == 0)
                {
                    nodeId = ImGui::DockBuilderAddNode();
                    ImGui::DockBuilderSetNodePos(nodeId, destination->Pos);
                    ImGui::DockBuilderSetNodeSize(nodeId, destination->Size);
                    ImGui::DockBuilderDockWindow(target, nodeId);
                }
                if (placement != Placement::Tab)
                {
                    ImGuiDir direction = ImGuiDir_Left;
                    switch (placement)
                    {
                    case Placement::Right:
                        direction = ImGuiDir_Right;
                        break;
                    case Placement::Above:
                        direction = ImGuiDir_Up;
                        break;
                    case Placement::Below:
                        direction = ImGuiDir_Down;
                        break;
                    default:
                        break;
                    }
                    nodeId = ImGui::DockBuilderSplitNode(nodeId, direction, 0.5f, nullptr, nullptr);
                }
                ImGui::DockBuilderDockWindow(panel, nodeId);
                ImGui::DockBuilderFinish(ImGui::DockNodeGetRootNode(ImGui::DockBuilderGetNode(nodeId))->ID);
            }
            m_dockFocusPending = 0;
            ImGui::SetWindowFocus(panel);
#endif
        }

        void DockController::ResetLayout() noexcept
        {
            ExitMaximize();
            m_resetPending = true;
        }

        bool DockController::SaveLayout(const char* path) const
        {
#if NS_EDITOR_ENABLED
            std::size_t size = 0;
            const char* data = ImGui::SaveIniSettingsToMemory(&size);
            if (IsMaximized())
            {
                data = m_savedImGuiIni.data();
                size = m_savedImGuiIni.size();
            }
            return OS::FileSystem::WriteAllBytes(path, std::as_bytes(std::span{data, size}));
#else
            return false;
#endif
        }

        bool DockController::LoadLayout(const char* path)
        {
#if NS_EDITOR_ENABLED
            const std::optional<std::string> data = OS::FileSystem::ReadAllText(path);
            if (!data.has_value() || data->empty() ||
                data->find("[Docking][Data]") == std::string::npos ||
                data->find("[Window][Scene]") == std::string::npos)
            {
                return false;
            }
            ExitMaximize();
            ImGui::LoadIniSettingsFromMemory(data->data(), data->size());
            m_resetPending = false;
            m_dockFocusPending = 0;
            return true;
#else
            return false;
#endif
        }

        void DockController::RenderPanelMenu(const char* panel) noexcept
        {
#if NS_EDITOR_ENABLED
            if (ImGui::MenuItem("前面に表示"))
            {
                ExitMaximize();
                ImGui::SetWindowFocus(panel);
            }
            if (ImGui::MenuItem("最大化", "Shift+Space"))
            {
                EnterMaximize(panel);
            }
            if (ImGui::MenuItem("独立ウィンドウにする"))
            {
                MovePanel(panel, nullptr, Placement::Floating);
            }
            if (ImGui::BeginMenu("配置先"))
            {
                for (const char* target : k_Panels)
                {
                    if (std::strcmp(panel, target) == 0 || !ImGui::BeginMenu(target))
                    {
                        continue;
                    }
                    const char* labels[] = {"同じタブにまとめる", "左に分割", "右に分割", "上に分割", "下に分割"};
                    const Placement placements[] = {Placement::Tab, Placement::Left, Placement::Right,
                                                    Placement::Above, Placement::Below};
                    for (int i = 0; i < IM_ARRAYSIZE(labels); ++i)
                    {
                        if (ImGui::MenuItem(labels[i]))
                        {
                            MovePanel(panel, target, placements[i]);
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
#endif
        }

        void DockController::RenderWindowMenu() noexcept
        {
#if NS_EDITOR_ENABLED
            if (!ImGui::BeginMenu("Window"))
            {
                return;
            }
            if (ImGui::MenuItem("最大化を解除", "Shift+Space", false, IsMaximized()))
            {
                ExitMaximize();
            }
            for (const char* panel : k_Panels)
            {
                if (ImGui::BeginMenu(panel))
                {
                    RenderPanelMenu(panel);
                    ImGui::EndMenu();
                }
            }
            ImGui::Separator();
            const std::string path = OS::FileSystem::Combine(OS::FileSystem::ContentRoot(), "build/editor-layout.ini");
            if (ImGui::MenuItem("現在の配置を保存"))
            {
                if (SaveLayout(path.c_str()))
                {
                    m_layoutStatus = "配置を保存しました";
                }
                else
                {
                    m_layoutStatus = "配置を保存できませんでした";
                }
            }
            if (ImGui::MenuItem("保存した配置を復元", nullptr, false, OS::FileSystem::Exists(path)))
            {
                if (LoadLayout(path.c_str()))
                {
                    m_layoutStatus = "配置を復元しました";
                }
                else
                {
                    m_layoutStatus = "配置を読み込めませんでした";
                }
            }
            if (ImGui::MenuItem("初期配置に戻す"))
            {
                ResetLayout();
            }
            ImGui::Separator();
            ImGui::TextDisabled("タブをドラッグ: 移動・分割・合流");
            ImGui::TextDisabled("境界をドラッグ: サイズ変更");
            if (!m_layoutStatus.empty())
            {
                ImGui::TextUnformatted(m_layoutStatus.c_str());
            }
            ImGui::EndMenu();
#endif
        }

        void DockController::TickTabFocus() noexcept
        {
#if NS_EDITOR_ENABLED
            // レイアウトを組んだフレームに焦点を渡しても DockBuilder の既定選択に負けるので、
            // 全パネルが 1 度発行された次のフレームで前面タブを確定させる
            if (m_dockFocusPending > 0)
            {
                --m_dockFocusPending;
                if (m_dockFocusPending == 0)
                {
                    ImGui::SetWindowFocus(k_PanelActorList);
                    ImGui::SetWindowFocus(k_PanelInspector);
                    ImGui::SetWindowFocus(k_PanelAssets);
                    ImGui::SetWindowFocus(k_PanelScene);
                }
            }
#endif
        }
} // namespace NS::Editor
