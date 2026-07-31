#pragma once

#include "Runtime/Core/NonCopyable.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace NS::App
{

    class Layer;

    /// @brief レイヤーを順序付きで保持するコンテナ
    /// @details
    /// 通常のレイヤーとオーバーレイを区別し、オーバーレイは常に後段に配置・実行される。
    /// メインループ実行中のレイヤーの追加・取り外しはイテレータの無効化を招くため禁止。構成は起動時に確定させること。
    class Layers : public NS::Core::NonCopyable
    {
    public:
        Layers();
        ~Layers();

        /// 通常のレイヤーを追加する。常にオーバーレイより手前に配置される
        void AddLayer(std::unique_ptr<Layer> layer);

        /// オーバーレイを追加する。常に通常のレイヤーより後ろに配置される
        void AddOverlay(std::unique_ptr<Layer> overlay);

        /// 指定したレイヤーを取り外して返す。見つからなければ nullptr を返す
        std::unique_ptr<Layer> Remove(Layer* layer) noexcept;

        [[nodiscard]] std::size_t Size() const noexcept { return m_layers.size(); }
        [[nodiscard]] bool Empty() const noexcept { return m_layers.empty(); }

        [[nodiscard]] auto begin() noexcept { return m_layers.begin(); }
        [[nodiscard]] auto end() noexcept { return m_layers.end(); }
        [[nodiscard]] auto begin() const noexcept { return m_layers.begin(); }
        [[nodiscard]] auto end() const noexcept { return m_layers.end(); }
        [[nodiscard]] auto rbegin() noexcept { return m_layers.rbegin(); }
        [[nodiscard]] auto rend() noexcept { return m_layers.rend(); }

    private:
        std::vector<std::unique_ptr<Layer>> m_layers;
        std::size_t m_overlayBegin = 0; ///< オーバーレイ群の開始インデックス
    };

} // namespace NS::App
