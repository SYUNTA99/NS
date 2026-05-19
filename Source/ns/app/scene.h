#pragma once

namespace ns::app
{

    /// レベル / ステージのライフサイクル責務。
    /// Application が unique_ptr で所有し、Run() 開始時に OnStart、フレーム毎に
    /// OnUpdate(dt) と OnRender を呼び出し、終了時に OnShutdown を呼ぶ。
    /// 派生クラス側で必要なものだけ override する想定 (defaulted noop)。
    ///  で SceneManager (push/pop/replace) 拡張予定。
    class Scene
    {
    public:
        Scene() = default;
        virtual ~Scene() = default;

        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;
        Scene(Scene&&) = delete;
        Scene& operator=(Scene&&) = delete;

        /// Application::Run() 開始時に 1 回呼ばれる。Window/Renderer/Input は既に有効。
        virtual void OnStart() {}

        /// 固定タイムステップ Update。dt は ApplicationDesc::fixedDelta 固定 (default 1/60)。
        /// 物理 / 入力判定はここで行い、Render 側は補間描画のみに留める。
        virtual void OnUpdate(float dt) { (void)dt; }

        /// 可変フレーム Render。Application::Alpha() で fixed 補間係数を取得可。
        virtual void OnRender() {}

        /// MainLoop 終了後に 1 回呼ばれる。Window/Renderer はまだ有効、Shutdown 後に解放。
        virtual void OnShutdown() {}
    };

} // namespace ns::app
