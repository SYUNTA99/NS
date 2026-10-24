#pragma once

namespace NS
{
    class DirectionalLight;
    class IOverlay;
    class IRenderable;
    class IUseCollision;
    class Model;
    class SceneRenderer;
    class Shadow;
    class Transform;

    //! 所属シーンの描く物の管理クラスを取得するインターフェース
    class IUseRenderer
    {
    public:
        //! 描く物の管理クラス。シーンに居ない間は nullptr
        [[nodiscard]] virtual SceneRenderer* GetSceneRenderer() const noexcept = 0;

    protected:
        ~IUseRenderer() = default;
    };

    // 描く物は、アクターを作る時 (OnInit) に部品を作る行の隣で InitDraw を 1 回呼んで入れ、
    // アクターが終わる時 (OnEndPlay) に EndDraw で外す。出る・消える時は ShowModel・HideModel で切り替え、
    // 入れたままにするので描く順は変わらない。管理クラスが無ければ (シーンに居ないアクター) どれも何もしない

    //! @brief 描く物 renderable を、描く時に渡す持ち主の根 root と組にして入れる。出ている物として入る
    //! @details renderable と root は EndDraw まで生きていること
    void InitDraw(const IUseRenderer* user, IRenderable* renderable, const Transform* root);
    //! @brief 同じアクターの体をもう一度描く物を入れる。model は描くたびに renderable へ渡す元の Model
    //! @details model も EndDraw まで生きていること
    void InitDraw(const IUseRenderer* user, IRenderable* renderable, const Transform* root, const Model* model);
    //! @brief 影を入れる。collision は影が描くたびに真下の地面を探すシーンで、影が持つ
    //! @details collision も EndDraw まで生きていること
    void InitDraw(const IUseRenderer* user, Shadow* shadow, const Transform* root, const IUseCollision* collision);

    //! 入れた描く物を描き始める。入れていない物は何もしない
    void ShowModel(const IUseRenderer* user, const IRenderable* renderable) noexcept;
    //! 入れた描く物を、入れたまま描かない。入れていない物は何もしない
    void HideModel(const IUseRenderer* user, const IRenderable* renderable) noexcept;
    //! 描く物を管理クラスから外す。入れていない物は何もしないので、2 回呼んでもよい
    void EndDraw(const IUseRenderer* user, const IRenderable* renderable) noexcept;

    // 重ね描きと平行光も描く物と同じく、OnInit で入れて OnEndPlay で外し、出る・消える時は入れたまま切り替える

    //! @brief 重ね描き overlay を入れる。描く物として入る
    //! @details overlay は EndOverlay まで生きていること
    void InitOverlay(const IUseRenderer* user, IOverlay* overlay);
    //! 入れた重ね描きを描き始める。入れていない物は何もしない
    void ShowOverlay(const IUseRenderer* user, const IOverlay* overlay) noexcept;
    //! 入れた重ね描きを、入れたまま描かない。入れていない物は何もしない
    void HideOverlay(const IUseRenderer* user, const IOverlay* overlay) noexcept;
    //! 重ね描きを管理クラスから外す。入れていない物は何もしない
    void EndOverlay(const IUseRenderer* user, const IOverlay* overlay) noexcept;

    //! @brief 平行光 light を、向きを出す持ち主の根 root と組にして入れる。シーンを照らす物として入る
    //! @details light と root は EndLight まで生きていること
    void InitLight(const IUseRenderer* user, DirectionalLight* light, const Transform* root);
    //! 入れた平行光でシーンを照らし始める。入れていない物は何もしない
    void EnableLight(const IUseRenderer* user, const DirectionalLight* light) noexcept;
    //! 入れた平行光を、入れたまま照らす物から外す。入れていない物は何もしない
    void DisableLight(const IUseRenderer* user, const DirectionalLight* light) noexcept;
    //! 平行光を管理クラスから外す。入れていない物は何もしない
    void EndLight(const IUseRenderer* user, const DirectionalLight* light) noexcept;
} // namespace NS
