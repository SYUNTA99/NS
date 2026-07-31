#include "Editor/RenderSettingsPanel.h"

#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Runtime/Object/Scene/SceneData.h"

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
    void RenderSettingsPanel::Render(LevelEditorController& editor) noexcept
    {
#if NS_EDITOR_ENABLED
        const NS::Graphics::RenderSettings& resolved = editor.DebugResolvedSettings();
        const NS::Graphics::RenderSettingsOverride& sceneOver = editor.DebugSceneOverride();
        const NS::Graphics::RenderSettingsOverride& objOver = editor.DebugPlayerObjectOverride();

        // 出所は has_value の突き合わせで逆算する。 Resolve のホットパスに追跡を入れない
        auto provenance = [](bool sceneHas, bool objectHas) -> const char* {
            if (objectHas)
                return "object";
            if (sceneHas)
                return "scene";
            return "default";
        };

        if (ImGui::Begin(k_PanelRenderSettings))
        {
            ImGui::Text("lightDir   : (%.2f,%.2f,%.2f) [%s]",
                        static_cast<double>(resolved.lightDir.x),
                        static_cast<double>(resolved.lightDir.y),
                        static_cast<double>(resolved.lightDir.z),
                        provenance(sceneOver.lightDir.has_value(), objOver.lightDir.has_value()));
            ImGui::Text("lightColor : (%.2f,%.2f,%.2f) [%s]",
                        static_cast<double>(resolved.lightColor.x),
                        static_cast<double>(resolved.lightColor.y),
                        static_cast<double>(resolved.lightColor.z),
                        provenance(sceneOver.lightColor.has_value(), objOver.lightColor.has_value()));
            ImGui::Text("ambient    : (%.2f,%.2f,%.2f) [%s]",
                        static_cast<double>(resolved.ambientColor.x),
                        static_cast<double>(resolved.ambientColor.y),
                        static_cast<double>(resolved.ambientColor.z),
                        provenance(sceneOver.ambientColor.has_value(), objOver.ambientColor.has_value()));
            ImGui::Text("ground     : (%.2f,%.2f,%.2f) [%s]",
                        static_cast<double>(resolved.groundColor.x),
                        static_cast<double>(resolved.groundColor.y),
                        static_cast<double>(resolved.groundColor.z),
                        provenance(sceneOver.groundColor.has_value(), objOver.groundColor.has_value()));
            ImGui::Text("exposure   : %.2f [%s]",
                        static_cast<double>(resolved.exposure),
                        provenance(sceneOver.exposure.has_value(), objOver.exposure.has_value()));
            ImGui::Separator();
            ImGui::Text("clearColor : (%.2f,%.2f,%.2f,%.2f) [%s]",
                        static_cast<double>(resolved.clearColor.R()),
                        static_cast<double>(resolved.clearColor.G()),
                        static_cast<double>(resolved.clearColor.B()),
                        static_cast<double>(resolved.clearColor.A()),
                        provenance(sceneOver.clearColor.has_value(), objOver.clearColor.has_value()));
            ImGui::TextDisabled("clearColor / vsync のシーン上書きは非対応 (lighting 4 種のみ階層対応)");
            ImGui::Separator();

            // 照明は太陽オブジェクトの DirectionalLightComponent が持つのでここでは編集しない
            // このシーンが所有する環境値のうち、 ここに残るのは skybox だけ
            const NS::Object::SceneEnvironment& env = editor.Environment();
            const char* skyboxLabel = "(なし)";
            if (!env.skyboxCubemapPath.empty())
                skyboxLabel = env.skyboxCubemapPath.c_str();
            ImGui::Text("skybox: %s", skyboxLabel);
        }
        ImGui::End();
#else
        (void)editor;
#endif
    }
} // namespace NS::Editor
