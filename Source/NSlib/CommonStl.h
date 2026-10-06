#pragma once

// 各層 PCH と Game / Editor の GamePch が共通取り込みする定番標準ライブラリ
// windows.h を含まないので stdlib セットが揃う。Math 層のみ PCH を持たず自己完結する

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
