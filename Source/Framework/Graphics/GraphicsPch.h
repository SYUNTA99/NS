#pragma once

#include "Framework/Framework.h"

// Graphics 層は D3D11 を直接利用するため、 detail/*.cpp 全てで D3D11 ヘッダを利用可能にする
#include <DirectXMath.h>
#include <SimpleMath.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
