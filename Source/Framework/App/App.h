#pragma once

/// @file App.h
/// @brief App 層 umbrella header — Application のメインループのみを集約。
///
/// @details DD7 短縮命名により basename は層名 `App` と一致。
/// class header `Framework/App/Application.h` とは別 file で衝突なし。
/// RootScene (scene-graph + lifecycle 基底) は Scene 層所属に移動済 (T1 で逆流解消)。
/// 利用側は `#include "Framework/Scene/RootScene.h"` を明示すること。

#include "Framework/App/Application.h"
