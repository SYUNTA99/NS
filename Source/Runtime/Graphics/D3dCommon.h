#pragma once

// NS::Graphics 公開ヘッダ共通の D3D11 / DXGI include と ComPtr 別名
// D3D11 / DXGI 型を隠さず公開する層なので、 公開ヘッダも .cpp もこれ 1 本でまとめて引く

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

namespace NS::Graphics
{
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
} // namespace NS::Graphics
