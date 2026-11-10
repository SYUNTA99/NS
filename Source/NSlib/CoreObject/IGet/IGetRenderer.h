#pragma once

namespace NS
{
    class DirectionalLight;
    class Model;
    class SceneRenderer;
    class Shadow;
    class WorldLines;

    //! 所属シーンの描く物の管理クラスを取得するインターフェース
    class IGetRenderer
    {
    public:
        //! 描く物の管理クラス。シーンに居ない間は nullptr
        [[nodiscard]] virtual SceneRenderer* GetSceneRenderer() const noexcept = 0;

    protected:
        ~IGetRenderer() = default;
    };

    // 描く部品 (Model・Shadow・WorldLines) は、基底の SceneActor が OnInit の直後に入れ、OnEndPlay で EndDraw で外す。
    // 出る・消える時は ShowModel・HideModel で切り替え、入れたままにするので描く順は変わらない。
    // 管理クラスが無ければ (シーンに居ないアクター) どれも何もしない

    //! 入れた描く部品を描き始める。入れていない物は何もしない
    void ShowModel(const IGetRenderer* user, const Model* model) noexcept;
    void ShowModel(const IGetRenderer* user, const Shadow* shadow) noexcept;
    void ShowModel(const IGetRenderer* user, const WorldLines* lines) noexcept;
    //! 入れた描く部品を、入れたまま描かない。入れていない物は何もしない
    void HideModel(const IGetRenderer* user, const Model* model) noexcept;
    void HideModel(const IGetRenderer* user, const Shadow* shadow) noexcept;
    void HideModel(const IGetRenderer* user, const WorldLines* lines) noexcept;
    //! 描く部品を管理クラスから外す。入れていない物は何もしないので、2 回呼んでもよい
    void EndDraw(const IGetRenderer* user, const Model* model) noexcept;
    void EndDraw(const IGetRenderer* user, const Shadow* shadow) noexcept;
    void EndDraw(const IGetRenderer* user, const WorldLines* lines) noexcept;

    // 平行光は基底の SceneActor が OnInit の直後に入れ、出入りと OnEndPlay で下の 3 つを呼ぶ

    //! 入れた平行光でシーンを照らし始める。入れていない物は何もしない
    void EnableLight(const IGetRenderer* user, const DirectionalLight* light) noexcept;
    //! 入れた平行光を、入れたまま照らす物から外す。入れていない物は何もしない
    void DisableLight(const IGetRenderer* user, const DirectionalLight* light) noexcept;
    //! 平行光を管理クラスから外す。入れていない物は何もしない
    void EndLight(const IGetRenderer* user, const DirectionalLight* light) noexcept;
} // namespace NS
