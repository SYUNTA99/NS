#pragma once

#include "ns/app/scene.h"
#include "ns/graphics/camera.h"
#include "ns/scene/game_object.h"

#include <memory>

namespace ns::graphics
{
    class Material;
    class Mesh;
    class ShaderProgram;
    class Texture;
} // namespace ns::graphics

namespace ns::scene
{
    class MeshComponent;
} // namespace ns::scene

/// 回転するテクスチャ付きキューブを描画する検証 Scene。
/// GameObject + MeshComponent 構成に refactor 済。OnStart で Mesh / Texture / Shader /
/// Material / Camera を構築して MeshComponent に注入、OnUpdate で Y 軸回転 + ESC 終了 +
/// Transform::Snapshot、OnRender で Application::Alpha() による補間描画。
class CubeScene : public ns::app::Scene
{
public:
    CubeScene();
    ~CubeScene() override;

    CubeScene(const CubeScene&) = delete;
    CubeScene& operator=(const CubeScene&) = delete;
    CubeScene(CubeScene&&) = delete;
    CubeScene& operator=(CubeScene&&) = delete;

    void OnStart() override;
    void OnUpdate(float dt) override;
    void OnRender() override;
    void OnShutdown() override;

private:
    std::unique_ptr<ns::graphics::Mesh> m_mesh;
    std::unique_ptr<ns::graphics::Texture> m_texture;
    std::unique_ptr<ns::graphics::ShaderProgram> m_shader;
    std::unique_ptr<ns::graphics::Material> m_material;
    ns::graphics::Camera m_camera;
    ns::scene::GameObject m_cubeActor;
    std::unique_ptr<ns::scene::MeshComponent> m_meshComponent;
    float m_rotationY = 0.0f;
};
