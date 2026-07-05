#pragma once

/// @file GamePch.h
/// @brief Game / Editor 側の共通プリコンパイルヘッダ
///
/// @details GameCore / Editor / Game の各 .cpp へ /FI で強制 include される
/// 安定した Framework 公開 API と多用する標準ライブラリだけを集約し、各 .cpp が
/// 定番 include を書かずに済むようにする。 自分でよく編集する Game / Editor 自身の
/// ヘッダや、 windows.h を巻き込む Framework.h はここに入れない

#include "Framework/App/App.h"
#include "Framework/Core/Core.h"
#include "Framework/Graphics/Graphics.h"
#include "Framework/Math/Math.h"
#include "Framework/Physics/Physics.h"
#include "Framework/Platform/Platform.h"
#include "Framework/Scene/Scene.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
