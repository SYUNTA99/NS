#pragma once

/// @file Framework.h
/// @brief Framework 共通環境ヘッダ。 PCH / detail/ / .cpp からのみ include
///
/// @details
/// 公開ヘッダ (Framework/<Layer>/X.h) には絶対に include しない方針
/// Windows.h などの重い OS / SDK ヘッダを公開 API に巻き込まないため、
/// 層 PCH (<Layer>Pch.h) と detail/*.cpp のみがこのヘッダを参照する

// NOMINMAX 等の前提 define は premake5.lua でグローバルに伝搬させ、サードパーティ経由の漏れ込みを避けている
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
