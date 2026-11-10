#pragma once

namespace NS
{
    class DirectionalLight;
    class IOverlay;
    class Model;
    class SceneRenderer;
    class Shadow;

    //! 所属シーンの描く物の管理クラスを取得するインターフェース
    class IGetRenderer
    {
    public:
        //! 描く物の管理クラス。シーンに居ない間は nullptr
        [[nodiscard]] virtual SceneRenderer* GetSceneRenderer() const noexcept = 0;

    protected:
        ~IGetRenderer() = default;
    };

    // 描く部品 (Model・Shadow) は、基底の SceneActor が OnInit の直後に入れ、OnEndPlay で EndDraw で外す。
    // 出る・消える時は ShowModel・HideModel で切り替え、入れたままにするので描く順は変わらない。
    // 管理クラスが無ければ (シーンに居ないアクター) どれも何もしない

    //! 入れた描く部品を描き始める。入れていない物は何もしない
    void ShowModel(const IGetRenderer* user, const Model* model) noexcept;
    void ShowModel(const IGetRenderer* user, const Shadow* shadow) noexcept;
    //! 入れた描く部品を、入れたまま描かない。入れていない物は何もしない
    void HideModel(const IGetRenderer* user, const Model* model) noexcept;
    void HideModel(const IGetRenderer* user, const Shadow* shadow) noexcept;
    //! 描く部品を管理クラスから外す。入れていない物は何もしないので、2 回呼んでもよい
    void EndDraw(const IGetRenderer* user, const Model* model) noexcept;
    void EndDraw(const IGetRenderer* user, const Shadow* shadow) noexcept;

    // 重ね描きは描く物と同じく、OnInit で入れて OnEndPlay で外し、出る・消える時は入れたまま切り替える

    //! 重ね描き overlay を入れる。描く物として入る
    //! overlay は EndOverlay まで生きていること
    void InitOverlay(const IGetRenderer* user, IOverlay* overlay);
    //! 入れた重ね描きを描き始める。入れていない物は何もしない
    void ShowOverlay(const IGetRenderer* user, const IOverlay* overlay) noexcept;
    //! 入れた重ね描きを、入れたまま描かない。入れていない物は何もしない
    void HideOverlay(const IGetRenderer* user, const IOverlay* overlay) noexcept;
    //! 重ね描きを管理クラスから外す。入れていない物は何もしない
    void EndOverlay(const IGetRenderer* user, const IOverlay* overlay) noexcept;

    // 平行光は基底の SceneActor が OnInit の直後に入れ、出入りと OnEndPlay で下の 3 つを呼ぶ

    //! 入れた平行光でシーンを照らし始める。入れていない物は何もしない
    void EnableLight(const IGetRenderer* user, const DirectionalLight* light) noexcept;
    //! 入れた平行光を、入れたまま照らす物から外す。入れていない物は何もしない
    void DisableLight(const IGetRenderer* user, const DirectionalLight* light) noexcept;
    //! 平行光を管理クラスから外す。入れていない物は何もしない
    void EndLight(const IGetRenderer* user, const DirectionalLight* light) noexcept;
} // namespace NS
