#pragma once

#include "Runtime/Core/NonCopyable.h"

#include <string>
#include <string_view>
#include <utility>

namespace NS::App
{

    //! @brief アプリケーションのメインループで反復処理される層の基底クラス
    //! @details
    //! ゲーム、エディタ、デバッグHUDなどを独立した層として並立させる
    //! 入力イベントの階層は持たず、全体の入力状態をそれぞれポーリングして参照する
    //! メインループ実行中のレイヤーの追加・削除はイテレータの無効化を招くため禁止。構成は起動時に確定させること
    class Layer : public NS::Core::NonCopyable
    {
    public:
        explicit Layer(std::string name = "Layer") noexcept : m_name(std::move(name)) {}
        virtual ~Layer() = default;

        //! アプリケーション初期化時に一度だけ呼ばれる
        virtual void OnAttach() {}

        //! アプリケーション終了時に逆順で一度だけ呼ばれる
        virtual void OnDetach() {}

        //! 固定時間刻みのループ内で、1回の更新につき 1 回呼ばれる
        virtual void OnUpdate() {}

        //! 可変フレームにおける描画時に、1フレームにつき1回呼ばれる
        virtual void OnRender() {}

        [[nodiscard]] std::string_view Name() const noexcept { return m_name; }
        [[nodiscard]] bool IsActive() const noexcept { return m_active; }
        void SetActive(bool active) noexcept { m_active = active; }

    private:
        std::string m_name;
        bool m_active = true; //!< false の場合は更新と描画がスキップされる
    };

} // namespace NS::App
