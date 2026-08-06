#pragma once

#include "Runtime/Core/NonCopyable.h"

#include <memory>

namespace NS::Graphics
{
    class Renderer;
}

namespace NS::Object
{

    class AssetManager;
    class Scene;
    struct SceneData;

    /// @brief シーンデータから scene を立てて寿命を握る単一 scene ホルダ
    /// @details ロード中の scene 1 つを保持し、OnStart / OnUpdate / OnRender / OnShutdown を
    /// Application から現 scene へ取り次ぐ
    /// 受け取るのは型でなくデータで、立てる実体は常に Scene
    /// scene を重ねる要件が出たら stack へ広げる
    class SceneManager : public NS::Core::NonCopyable
    {
    public:
        SceneManager();
        ~SceneManager();

        /// 立てる scene へ引き継ぐ AssetManager を非所有で差す。以降 LoadScene する scene が受け取る
        void SetAssets(AssetManager* assets) noexcept;

        /// 立てる scene へ引き継ぐレンダラーを非所有で差す。以降 LoadScene する scene が受け取る
        void SetRenderer(NS::Graphics::Renderer* renderer) noexcept;

        /// データから scene を立てる。現 scene は破棄してから作り直す
        /// サブシステムの生成は OnStart より先、world の組み立ては OnStart より後に行う
        /// データは取込後に用済みになる一時データで、以降の出所は live 実体になる
        Scene& LoadScene(SceneData&& data);

        /// 現 scene を破棄して scene 無し状態にする。未ロードなら何もしない
        void UnloadScene();

        /// 現在有効な scene。 未ロードなら nullptr
        [[nodiscard]] Scene* Current() noexcept;
        [[nodiscard]] const Scene* Current() const noexcept;

        /// 現在有効な scene があるか
        [[nodiscard]] bool HasScene() const noexcept;

        /// 現 scene の OnUpdate へ取り次ぐ。 未ロードなら何もしない
        void Update();
        /// 現 scene の OnRender へ取り次ぐ。 未ロードなら何もしない
        void Render();

    private:
        std::unique_ptr<Scene> m_current;
        /// 立てる scene へ引き継ぐ参照で非所有。未設定なら scene 側が未設定のまま動く
        AssetManager* m_assets = nullptr;
        NS::Graphics::Renderer* m_renderer = nullptr;
    };

} // namespace NS::Object
