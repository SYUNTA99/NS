#include "Editor/DockController.h"

#include "Editor/PanelIds.h"

#include <cstddef>
#include <cstdio>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#include <imgui_internal.h>
#endif

namespace NS::Editor
{
#if NS_EDITOR_ENABLED
    namespace
    {
        // 右上に浮かべる全面化ボタンの共通フラグ
        constexpr ImGuiWindowFlags k_FloatingButtonFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

        void SelectDefaultTab(ImGuiID nodeId, const char* windowName)
        {
            ImGuiDockNode* node = ImGui::DockBuilderGetNode(nodeId);
            if (node == nullptr)
                return;
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
            // 残りの下段は Hierarchy と Scene の両方にまたがる帯にする
            ImGuiID bottomId = 0;
            ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Down, 0.28f, &bottomId, &centerId);
            ImGuiID leftId = 0;
            ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, 0.24f, &leftId, &centerId);

            // Edit Mode と Hierarchy は左列でタブに重ねる。前面タブは下の SelectDefaultTab が決める
            ImGui::DockBuilderDockWindow(k_PanelEditMode, leftId);
            ImGui::DockBuilderDockWindow(k_PanelHierarchy, leftId);
            ImGui::DockBuilderDockWindow(k_PanelInspector, rightId);
            // Assets と Console は下段でタブに重ねる
            ImGui::DockBuilderDockWindow(k_PanelConsole, bottomId);
            ImGui::DockBuilderDockWindow(k_PanelAssets, bottomId);
            // 中央ノードは窓を割り当てず、Scene / Game の両方をドックする。映像を持つのは前面の側だけ
            ImGui::DockBuilderDockWindow(k_PanelScene, centerId);
            ImGui::DockBuilderDockWindow(k_PanelGame, centerId);

            // ドック順や発行順に前面タブを委ねると Edit Mode が出てくるので明示する
            SelectDefaultTab(leftId, k_PanelHierarchy);
            SelectDefaultTab(bottomId, k_PanelAssets);
            SelectDefaultTab(centerId, k_PanelScene);

            ImGui::DockBuilderFinish(dockspaceId);
        }

        // タブバーが畳まれた単独タブのパネルへ、右上に浮かせる全面化ボタンを出す。押下で true
        bool FloatingMaximizeButton(const char* buttonId, const ImGuiWindow* panel)
        {
            constexpr float k_Btn = 22.0f;
            constexpr float k_PadX = 3.0f;
            constexpr float k_PadY = -3.0f; // タブ行へ乗せ、 少し上へ
            // タブバーのある枠はタブ行の高さに合わせたいので、 ノードがあればノード上端・右端を使う
            ImVec2 topRight{panel->Pos.x + panel->Size.x, panel->Pos.y};
            if (panel->DockNode != nullptr)
                topRight = ImVec2{panel->DockNode->Pos.x + panel->DockNode->Size.x, panel->DockNode->Pos.y};
            const ImVec2 anchor{topRight.x - k_Btn - k_PadX, topRight.y + k_PadY};
            ImGui::SetNextWindowPos(anchor, ImGuiCond_Always);
            // 浮いた小窓に見せないよう枠と余白を消し、 アイコン 1 個分だけの当たりにする
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            // 普段は半透明の下地、 触れた時だけ明るくする
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.0f, 0.0f, 0.0f, 0.45f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{1.0f, 1.0f, 1.0f, 0.22f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{1.0f, 1.0f, 1.0f, 0.35f});
            bool pressed = false;
            if (ImGui::Begin(buttonId, nullptr, k_FloatingButtonFlags | ImGuiWindowFlags_NoBackground))
            {
                // ドック内パネルの上に出す時、 そのパネルへ焦点が移ると背後へ潜るので毎フレーム前面へ出す
                ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
                if (ImGui::Button("□", ImVec2{k_Btn, k_Btn}))
                    pressed = true;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("このパネルを全面化");
            }
            ImGui::End();
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(3);
            return pressed;
        }
    } // namespace
#endif

    void DockController::RenderDockSpaceHost(float topOffset) noexcept
    {
#if NS_EDITOR_ENABLED
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        if (vp == nullptr)
            return;
        const ImVec2 hostPos{vp->WorkPos.x, vp->WorkPos.y + topOffset};
        const ImVec2 hostSize{vp->WorkSize.x, vp->WorkSize.y - topOffset};

        ImGui::SetNextWindowPos(hostPos);
        ImGui::SetNextWindowSize(hostSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        constexpr ImGuiWindowFlags k_HostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground |
                                                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin("##EditorDockHost", nullptr, k_HostFlags);
        ImGui::PopStyleVar();

        // 0 だと DockSpaceOverViewport 相当の自動 id 判定ができないので明示 id にする
        const ImGuiID dockspaceId = ImHashStr("EditorDockSpaceV11");
        if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr)
        {
            // ini に保存済みのユーザー配置が無い時だけ既定レイアウトを組む
            BuildDefaultDockLayout(dockspaceId, hostPos, hostSize);
            // このフレームは組むだけ。焦点を渡すのは全パネルが発行された次のフレーム
            m_dockFocusPending = 2;
        }
        ImGui::DockSpace(dockspaceId, ImVec2{0.0f, 0.0f}, ImGuiDockNodeFlags_None);
        ImGui::End();
#else
        (void)topOffset;
#endif
    }

    void DockController::EnterMaximize(const char* name) noexcept
    {
#if NS_EDITOR_ENABLED
        // 復元用に現在のドックレイアウトを控えてから全面化対象を記録する
        std::size_t iniSize = 0;
        const char* ini = ImGui::SaveIniSettingsToMemory(&iniSize);
        if (ini != nullptr)
            m_savedImGuiIni.assign(ini, iniSize);
        m_maximizedPanel = name;
#else
        (void)name;
#endif
    }

    void DockController::ExitMaximize() noexcept
    {
#if NS_EDITOR_ENABLED
        // 控えていたドックレイアウトへ丸ごと書き戻す
        if (!m_savedImGuiIni.empty())
        {
            ImGui::LoadIniSettingsFromMemory(m_savedImGuiIni.data(), m_savedImGuiIni.size());
            m_savedImGuiIni.clear();
        }
#endif
        m_maximizedPanel.clear();
    }

    void DockController::RenderMaximizeButton() noexcept
    {
#if NS_EDITOR_ENABLED
        // 全面化中はパネルがドックを外れてタブが無いので、 右上に復元ボタンを浮かべる
        if (!m_maximizedPanel.empty())
        {
            ImGuiWindow* w = ImGui::FindWindowByName(m_maximizedPanel.c_str());
            if (w == nullptr || !w->WasActive)
                return;

            constexpr float k_Btn = 24.0f;
            constexpr float k_Pad = 6.0f;
            const ImVec2 anchor{w->Pos.x + w->Size.x - k_Btn - k_Pad, w->Pos.y + k_Pad};
            ImGui::SetNextWindowPos(anchor, ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.65f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{2.0f, 2.0f});
            if (ImGui::Begin("##PanelRestoreButton", nullptr, k_FloatingButtonFlags))
            {
                if (ImGui::Button("戻", ImVec2{k_Btn, k_Btn}))
                    ExitMaximize();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("元のレイアウトに戻す");
            }
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }

        // 各パネルの右上へ全面化ボタンを浮かせる。タブバーの状態に依らず出せるよう埋め込みは使わない
        static const char* const k_Panels[] = {k_PanelScene,
                                               k_PanelGame,
                                               k_PanelHierarchy,
                                               k_PanelInspector,
                                               k_PanelConsole,
                                               k_PanelAssets,
                                               k_PanelEditMode};
        for (int i = 0; i < IM_ARRAYSIZE(k_Panels); ++i)
        {
            const char* name = k_Panels[i];
            ImGuiWindow* w = ImGui::FindWindowByName(name);
            if (w == nullptr || !w->WasActive)
                continue;
            // ドック内で今表に出ている窓にだけ出す。 裏に隠れたタブは飛ばす
            // 単独枠や浮き窓はタブが畳まれても表なので出す
            if (w->DockNode != nullptr && w->DockNode->VisibleWindow != w)
                continue;
            char btnId[32];
            std::snprintf(btnId, sizeof(btnId), "##PanelMaxBtn%d", i);
            if (FloatingMaximizeButton(btnId, w))
                EnterMaximize(name);
        }
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
                ImGui::SetWindowFocus(k_PanelHierarchy);
                ImGui::SetWindowFocus(k_PanelInspector);
                ImGui::SetWindowFocus(k_PanelAssets);
                ImGui::SetWindowFocus(k_PanelScene);
            }
        }
#endif
    }
} // namespace NS::Editor
