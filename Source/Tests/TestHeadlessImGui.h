#pragma once

#include <imgui.h>

//! ImGui へ送った入力イベントの届き方
enum class HeadlessImGuiInput
{
    Trickle, //!< ImGui の既定。同じフレームに送った押下と解放を別のフレームへ分ける
    Instant, //!< 送ったイベントを次のフレームへまとめて届ける
};

//! 画面の無い試し用の ImGui を作り、フォントの画像まで組む。消すのは呼び出し元
//! @param[in] displaySize 画面の大きさ
//! @param[in] input 入力イベントの届き方
//! @return 作った ImGui の文脈。現在の文脈にもなっている
inline ImGuiContext* CreateHeadlessImGui(const ImVec2& displaySize,
                                         HeadlessImGuiInput input = HeadlessImGuiInput::Trickle)
{
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = displaySize;
    io.DeltaTime = 1.0f / 60.0f;
    if (input == HeadlessImGuiInput::Instant)
    {
        io.ConfigInputTrickleEventQueue = false;
    }
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    return context;
}
