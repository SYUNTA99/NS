#pragma once

#include "NSlib/Core/NonCopyable.h"

#include <string>
#include <string_view>
#include <utility>

namespace NS
{

    //! メインループが更新と描画を呼ぶ層。実行中の追加・削除は禁止
    class Layer : public NonCopyable
    {
    public:
        explicit Layer(std::string name = "Layer") noexcept : m_name(std::move(name)) {}
        virtual ~Layer() = default;

        //! アプリケーション初期化時に一度だけ呼ばれる
        virtual void OnAttach() {}

        //! アプリケーション終了時に逆順で一度だけ呼ばれる
        virtual void OnDetach() {}

        //! 固定時間刻みのループ内で、1 回の更新につき 1 回呼ばれる
        virtual void OnUpdate() {}

        //! 可変フレームにおける描画時に、1 フレームにつき 1 回呼ばれる
        virtual void OnRender() {}

        [[nodiscard]] std::string_view Name() const noexcept { return m_name; }
        [[nodiscard]] bool IsActive() const noexcept { return m_active; }
        //! false の間は OnUpdate と OnRender がスキップされる
        void SetActive(bool active) noexcept { m_active = active; }

    private:
        std::string m_name;
        bool m_active = true;
    };

} // namespace NS
