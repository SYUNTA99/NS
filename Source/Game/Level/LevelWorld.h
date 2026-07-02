#pragma once

/// @file LevelWorld.h
/// @brief LevelWorld — LevelData から組む runtime world 表現の所有と構築
///
/// @details 配置物 GameObject / 衝突プリミティブ / instanced 描画キャッシュ / hazard 走査 view /
/// コヨーテ縁を LevelData から一括で組み直す。 runtime も editor も同じ Rebuild 経路を通り、
/// scene は公開読み口からの観測と描画だけを行う
/// 依存: NS::Scene::GameObject, NS::Physics::PhysicsWorld, NS::Graphics::InstanceBatcher

#include "Game/Blocks/LedgeEdges.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace NS::Graphics
{
    class InstanceBatcher;
}

namespace NS::Physics
{
    class PhysicsWorld;
}

namespace NS::Scene
{
    class AssetManager;
    class GameObject;
    class PlacedVirtualCamera;
    class SceneBase;
} // namespace NS::Scene

namespace NS::Game::Level
{
    struct LevelData;

    /// LevelData から組まれる runtime world。 所有と構築を一手に担う
    class LevelWorld
    {
    public:
        LevelWorld();
        ~LevelWorld();

        LevelWorld(const LevelWorld&) = delete;
        LevelWorld& operator=(const LevelWorld&) = delete;
        LevelWorld(LevelWorld&&) = delete;
        LevelWorld& operator=(LevelWorld&&) = delete;

        /// instanced 描画する grid solid block の静的属性。 instanceable 判定 / 近傍マスク / texture slice は
        /// level + theme が変わらない限り不変なので Rebuild で 1 度だけ焼く。 描画ループは
        /// world matrix だけを毎フレーム読む。 theme は load 時のみ変わり必ず rebuild を伴うので古びない
        struct InstancedBlock
        {
            std::size_t objectIndex = 0; // Objects() への添字。 補間 world matrix の取得に使う
            float textureSlice = 0.0f;
        };

        /// level の objects から全 runtime 表現を組み直す。 既存の配置物と衝突 world は必ず先に空へ戻す
        /// assets が nullptr の起動前 / テストでは物を組まず、 衝突 world も空のまま返る
        void Rebuild(const LevelData& level,
                     NS::Scene::SceneBase& scene,
                     NS::Physics::PhysicsWorld& physics,
                     NS::Scene::AssetManager* assets);

        /// 配置物を逆順に畳んで所有物を空へ戻す。 scene の OnShutdown と Rebuild 冒頭が呼ぶ
        void Clear();

        /// block instanced 描画装置を生成する。 device 不在で無効でも block 描画スキップで続行できる
        void CreateBatcher();

        /// 描画装置を破棄する。 Renderer より先に死ぬ順序は scene の OnShutdown が保証する
        void ResetBatcher() noexcept;

        /// 配置物の単一所有リスト。 grid / slope / hazard / water / deco / 自由配置物すべてを generic に持つ
        [[nodiscard]] std::vector<std::unique_ptr<NS::Scene::GameObject>>& Objects() noexcept { return m_objects; }
        [[nodiscard]] const std::vector<std::unique_ptr<NS::Scene::GameObject>>& Objects() const noexcept
        {
            return m_objects;
        }

        /// Objects()[i] に対応する level.objects の添字。 Objects() と同長・ 1:1
        [[nodiscard]] const std::vector<std::size_t>& SourceIndices() const noexcept { return m_objectSourceIndices; }

        /// instanced 描画対象の焼き済み静的属性
        [[nodiscard]] const std::vector<InstancedBlock>& InstancedBlocks() const noexcept { return m_instancedBlocks; }

        /// hazard の damage 走査 view。 所有は Objects() 側でここは観測のみ
        [[nodiscard]] const std::vector<NS::Scene::GameObject*>& HazardView() const noexcept { return m_hazardView; }

        /// 据え置きカメラの走査 view。 所有は Objects() 側で、 Brain 登録と進入判定の駆動が読む
        [[nodiscard]] const std::vector<NS::Scene::PlacedVirtualCamera*>& PlacedCameras() const noexcept
        {
            return m_placedCameraView;
        }

        /// コヨーテ debug 用に焼いた踏み外せる縁の world 線分。 出荷では焼かれず常に空
        [[nodiscard]] const std::vector<NS::Game::Blocks::LedgeEdge>& LedgeEdges() const noexcept
        {
            return m_ledgeEdges;
        }

        /// block instanced 描画装置。 CreateBatcher 前は nullptr
        [[nodiscard]] NS::Graphics::InstanceBatcher* Batcher() noexcept { return m_instanceBatcher.get(); }

        /// level.objects と 1:1 の編集セッション識別子。 undo 履歴が free オブジェクトを再特定するために持つ
        /// 非シリアライズで、 objects 全置換時は呼び手が連番へ戻す
        [[nodiscard]] std::vector<std::uint32_t>& EditIds() noexcept { return m_editIds; }

        /// 次に振る編集 id。 採番する編集操作が可変参照で束ねる
        [[nodiscard]] std::uint32_t& NextEditId() noexcept { return m_nextEditId; }

    private:
        std::vector<std::unique_ptr<NS::Scene::GameObject>> m_objects;
        std::vector<std::size_t> m_objectSourceIndices;
        std::vector<InstancedBlock> m_instancedBlocks;
        std::vector<NS::Scene::GameObject*> m_hazardView;
        std::vector<NS::Scene::PlacedVirtualCamera*> m_placedCameraView;
        std::vector<NS::Game::Blocks::LedgeEdge> m_ledgeEdges;
        std::unique_ptr<NS::Graphics::InstanceBatcher> m_instanceBatcher;
        std::vector<std::uint32_t> m_editIds;
        std::uint32_t m_nextEditId = 0;
    };

} // namespace NS::Game::Level
