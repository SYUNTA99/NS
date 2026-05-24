#pragma once

#include "Framework/Core/Math.h"
#include "Framework/Scene/RootScene.h"
#include "Game/CameraRig.h"

#include <memory>
#include <vector>

namespace NS::Graphics
{
    class Material;
    class Mesh;
    class ShaderProgram;
    class Texture;
} // namespace NS::Graphics

namespace NS::Scene
{
    class IRenderable;
} // namespace NS::Scene

class Block;
class Player;

///  のテストレベルを構築する Scene。
/// 床 + 壁 + ジャンプ台のブロック群と、追従カメラ付き Player を配置する。
///  (push 型 RenderRegistry) /  (Snapshot 一括) /  (resize callback) /
///  (baseColor 色分け) /  (BeginFrame は Application 内部) を実装する。
class MainScene : public NS::Scene::RootScene
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

    void RegisterRenderable(NS::Scene::IRenderable* renderable) override;
    void UnregisterRenderable(NS::Scene::IRenderable* renderable) override;

private:
    std::unique_ptr<NS::Graphics::Mesh> m_cubeMesh;
    std::unique_ptr<NS::Graphics::Texture> m_texture;
    std::unique_ptr<NS::Graphics::ShaderProgram> m_shader;
    std::unique_ptr<NS::Graphics::Material> m_playerMaterial;
    std::unique_ptr<NS::Graphics::Material> m_blockMaterial;

    std::unique_ptr<Player> m_player;
    std::vector<std::unique_ptr<Block>> m_blocks;

    std::unique_ptr<CameraRig> m_cameraRig;

    std::vector<NS::Scene::IRenderable*> m_renderList;
    std::vector<NS::Core::AABB> m_collisionWorld;
};
