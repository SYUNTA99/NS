#pragma once

/// @file CommonStl.h
/// @brief プロジェクト全体で共通取り込みする定番標準ライブラリの単一の源
///
/// @details windows.h を含まないため、 層 PCH 用の Framework.h と Game / Editor 用の
/// GamePch の双方がこれを include して stdlib セットを一致させる。 各 .cpp と Framework
/// 公開ヘッダはこれらの PCH 経由で下記が届く前提で個別 include を書かない
/// 例外は Math 層で、 依存ゼロの最下層リーフのため PCH を持たず自己完結する

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
