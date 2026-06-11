#pragma once

/// @file Core.h
/// @brief Core 層一括 include ヘッダ — Logger / Math / Clock / Filesystem / StringUtils
///
/// @details `#include "Framework/Core/Core.h"` のみで Core 層公開 API 全体を取り込める
/// 個別 include (`Framework/Core/Logger.h` 等) も引き続き利用可能

#include "Framework/Core/Clock.h"
#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Core/StringUtils.h"
#include "Framework/Math/Math.h"
