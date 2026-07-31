#pragma once

#include "Runtime/Object/Scene/SceneData.h"

#include <cstddef>
#include <cstdint>

namespace NS::Game::Level
{
    /// 追従カメラの配置物か。ThirdPersonFollowComponent の有無で見分ける
    [[nodiscard]] bool IsFollowCameraObject(const NS::Object::ObjectData& object) noexcept;

    /// objects から追従カメラを探す。最初の 1 件の添字、無ければ k_NoObjectIndex
    [[nodiscard]] std::size_t FindFollowCameraObjectIndex(const NS::Object::SceneData& scene) noexcept;

    /// 追従カメラの ObjectData を作る。追従先の id を Target 参照へ焼く。0 は未設定
    /// 位置は追従で毎フレーム決まるため Transform は既定のまま
    [[nodiscard]] NS::Object::ObjectData MakeFollowCameraObject(std::uint32_t targetObjectId);

    /// 追従カメラが 1 つも無ければ targetObjectId を追う 1 台を足し、永続 id まで振る
    [[nodiscard]] bool EnsureFollowCameraObject(NS::Object::SceneData& scene, std::uint32_t targetObjectId);
} // namespace NS::Game::Level
