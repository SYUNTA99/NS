#pragma once

#include "ns/app/scene.h"
#include "ns/graphics/camera.h"

#include <memory>

namespace ns::graphics
{
    class Material;
    class Mesh;
    class ShaderProgram;
    class Texture;
} // namespace ns::graphics

/// 回転するテクスチャ付きキューブを描画する検証 Scene。
/// OnStart で Mesh / Texture / Shader / Material / Camera を構築し、
/// OnUpdate で Y 軸回転と ESC 終了、OnRender で Material::Bind + Mesh::Draw を行う。
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
    float m_rotationY = 0.0f;
};
