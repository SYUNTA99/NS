#pragma once

/// @file D3dCommon.h
/// @brief NS::Graphics 公開ヘッダ共通の D3D11 / DXGI include と ComPtr 別名
///
/// @details exposed-D3D lean 設計のため D3D11 / DXGI 型は隠さず公開する。 公開ヘッダ・ 層内 .cpp は
/// これ 1 本で `<d3d11.h>` / `<dxgi.h>` / `<wrl/client.h>` をまとめて引き、 各所での重複 include や
/// 型ごとの前方宣言を不要にする。 D3D オブジェクトの所有メンバは `NS::Graphics::ComPtr<T>` で宣言する
/// 他層 / Game から間接 include されることも許容する (Graphics 層は D3D を公開する方針のため)

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

namespace NS::Graphics
{
    /// D3D / COM オブジェクト所有用の参照カウント付きスマートポインタ (Microsoft::WRL::ComPtr<T> の別名)
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
} // namespace NS::Graphics
