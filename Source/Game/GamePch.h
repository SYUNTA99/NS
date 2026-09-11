#pragma once

// Game / Editor 側の共通プリコンパイルヘッダ。各 .cpp へ /FI で強制 include される
// 安定した Runtime 公開 API とよく使う標準ライブラリだけを入れ、windows.h を巻き込むヘッダは避ける

#include "Runtime/App/App.h"
#include "Runtime/Core/Core.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Graphics/Graphics.h"
#include "Runtime/Physics/Physics.h"
#include "Runtime/Platform/Platform.h"

// Object 層は一括ヘッダを持たないため、層の公開ヘッダを直接並べる
#include "Runtime/Graphics/RenderContext.h"
#include "Runtime/Object/AssetManager.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/BoxColliderComponent.h"
#include "Runtime/Object/Components/CameraBrainComponent.h"
#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/Components/CapsuleColliderComponent.h"
#include "Runtime/Object/Components/ColliderComponent.h"
#include "Runtime/Object/Components/MeshColliderComponent.h"
#include "Runtime/Object/Components/MeshRendererComponent.h"
#include "Runtime/Object/Components/OverlayRendererComponent.h"
#include "Runtime/Object/Components/PlacedVirtualCamera.h"
#include "Runtime/Object/Components/PlayerInputComponent.h"
#include "Runtime/Object/Components/ShadowComponent.h"
#include "Runtime/Object/Components/SkeletalAnimationComponent.h"
#include "Runtime/Object/Components/SlopeColliderComponent.h"
#include "Runtime/Object/Components/SphereColliderComponent.h"
#include "Runtime/Object/Components/ThirdPersonFollowComponent.h"
#include "Runtime/Object/Components/VirtualCameraComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/IRenderable.h"
#include "Runtime/Object/Object.h"
#include "Runtime/Object/Reflection/ObjectRef.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/Reflection/ReflectionJson.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneManager.h"
#include "Runtime/Object/Transform.h"

// 標準ライブラリは CommonStl.h にまとめ、各層 PCH と同じセットを使う
#include "Runtime/CommonStl.h"

// Game / Editor がよく使う重い標準ライブラリも足す
#include <algorithm>
#include <cmath>
#include <filesystem>
