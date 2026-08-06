#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Math/Math.h"

#include <memory>
#include <optional>

struct ImVec2;

namespace NS::Graphics
{
    class RenderTarget;
}

namespace NS::Object
{
    struct SceneView;
    struct CameraPose;
} // namespace NS::Object

namespace NS::Editor
{
    //! @brief 中央ビュー 1 枚のオフスクリーン描画先を持ち、 ImGui パネルへ貼る共通の道具
    //! @details Scene / Game 各パネルが 1 つずつ持つ。 前フレームに用意した RT を今フレームの Image が映す
    //! 1 フレーム遅延方式。 RT の生成 / リサイズと貼り付けをここへ集め、 パネル側は overlay と入力だけ持つ
    class ViewportSurface : public NS::Core::NonCopyable
    {
    public:
        //! RenderTarget を不完全型のまま持つので、 実体は両方 .cpp 側に置く
        //! コンストラクタも対象で、 例外時の巻き戻しで unique_ptr が破棄されるため完全型が要る
        ViewportSurface();
        ~ViewportSurface();

        //! 描画先の初期目標サイズを窓サイズに合わせ、 初回フレームから映せるようにする
        void SetInitialSize(const NS::Math::Size2D& size) noexcept { m_size = size; }

        //! @brief 画角を固定する縦横比を決める。 0 以下ならパネルの形に追従する
        //! @details ゲーム画面はパネルの形で見え方が変わると手触りを詰める基準が動くので、 出荷と同じ比率で固定する
        void SetFixedAspect(float aspect) noexcept { m_fixedAspect = aspect; }

        //! フレーム先頭で可視状態を false にする。 このフレームに BeginView されなければ不可視のままになる
        void ResetVisibility() noexcept { m_visible = false; }

        //! @brief windowName のパネルを Begin し、 content 領域があれば目標サイズを更新して RT を貼る
        //! @param outMin,outMax,outHovered 貼った映像の矩形と hover。 映像を貼れた時だけ埋まる
        //! @return 映像を貼れたか (可視かつ RT 有効)。 可視否かに関わらず EndView を必ず呼ぶ
        [[nodiscard]] bool BeginView(const char* windowName, ImVec2& outMin, ImVec2& outMax, bool& outHovered) noexcept;

        //! BeginView と対で必ず呼ぶ。 Begin した窓を閉じ、 積んだスタイルを戻す
        void EndView() noexcept;

        //! @brief 目標サイズに追従して RT を用意し、 pose とセットのビューを返す
        //! @return 可視だったフレームは {RT, pose}。 不可視やサイズ不足なら nullopt
        [[nodiscard]] std::optional<NS::Object::SceneView> CollectView(
            std::optional<NS::Object::CameraPose> pose) noexcept;

        //! 描画先を破棄する。 Renderer が非所有ポインタを踏まないよう外した後に呼ぶ
        void Release() noexcept;

        //! このフレームにパネルが可視 (サイズを持つ) だったか
        [[nodiscard]] bool IsVisible() const noexcept { return m_visible; }

    private:
        std::unique_ptr<NS::Graphics::RenderTarget> m_target;
        NS::Math::Size2D m_size{0, 0}; // content 領域。 次フレームの描画先サイズ
        float m_fixedAspect = 0.0f;    // 固定する縦横比。 0 以下はパネルの形に追従
        bool m_visible = false;        // このフレームにパネルが可視だったか
    };
} // namespace NS::Editor
