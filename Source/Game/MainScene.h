#pragma once

#include "ns/app/scene.h"
#include "ns/core/math.h"
#include "ns/scene/components/camera_component.h"
#include "ns/scene/components/third_person_follow_component.h"
#include "ns/scene/game_object.h"

#include <memory>
#include <vector>

namespace ns::graphics
{
    class Material;
    class Mesh;
    class ShaderProgram;
    class Texture;
} // namespace ns::graphics

namespace ns::scene
{
    class IRenderable;
} // namespace ns::scene

class Block;
class Player;

///  のテストレベルを構築する Scene。
/// 床 + 壁 + ジャンプ台のブロック群と、追従カメラ付き Player を配置する。
///  (push 型 RenderRegistry) /  (Snapshot 一括) /  (resize callback) /
///  (baseColor 色分け) /  (BeginFrame は Application 内部) を実装する。
class MainScene : public ns::app::Scene
{
public:
    MainScene();
    ~MainScene() override;

    MainScene(const MainScene&) = delete;
    MainScene& operator=(const MainScene&) = delete;
    MainScene(MainScene&&) = delete;
    MainScene& operator=(MainScene&&) = delete;

    void OnStart() override;
    void OnUpdate(float dt) override;
    void OnRender() override;
    void OnShutdown() override;

    void RegisterRenderable(ns::scene::IRenderable* renderable) override;
    void UnregisterRenderable(ns::scene::IRenderable* renderable) override;

private:
    std::unique_ptr<ns::graphics::Mesh> m_cubeMesh;
    std::unique_ptr<ns::graphics::Texture> m_texture;
    std::unique_ptr<ns::graphics::ShaderProgram> m_shader;
    std::unique_ptr<ns::graphics::Material> m_playerMaterial;
    std::unique_ptr<ns::graphics::Material> m_blockMaterial;

    std::unique_ptr<Player> m_player;
    std::vector<std::unique_ptr<Block>> m_blocks;

    std::unique_ptr<ns::scene::GameObject> m_cameraActor;
    ns::scene::CameraComponent m_camera;
    ns::scene::ThirdPersonFollowComponent m_follow;

    std::vector<ns::scene::IRenderable*> m_renderList;
    std::vector<ns::core::AABB> m_collisionWorld;
};
