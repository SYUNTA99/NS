#pragma once

// Game / Editor 側の共通プリコンパイルヘッダ。各 .cpp へ /FI で強制 include される
// 安定した Runtime 公開 API とよく使う標準ライブラリだけを入れ、windows.h を巻き込むヘッダは避ける

#include "NSlib/App/App.h"
#include "NSlib/Core/Core.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Graphics/Graphics.h"
#include "NSlib/Physics/Physics.h"
#include "NSlib/Windows/Windows.h"

// Object 層は一括ヘッダを持たないため、層の公開ヘッダを直接並べる
#include "NSlib/Graphics/RenderContext.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/AssetManager.h"
#include "NSlib/Object/Component.h"
#include "NSlib/Object/Components/Animation.h"
#include "NSlib/Object/Components/BoxCollision.h"
#include "NSlib/Object/Components/CameraManager.h"
#include "NSlib/Object/Components/CapsuleCollision.h"
#include "NSlib/Object/Components/Collision.h"
#include "NSlib/Object/Components/MeshCollision.h"
#include "NSlib/Object/Components/Model.h"
#include "NSlib/Object/Components/OverlayRenderer.h"
#include "NSlib/Object/Components/PlayerInput.h"
#include "NSlib/Object/Components/Shadow.h"
#include "NSlib/Object/Components/SphereCollision.h"
#include "NSlib/Object/Components/ThirdPersonFollow.h"
#include "NSlib/Object/Components/VirtualCamera.h"
#include "NSlib/Object/IRenderable.h"
#include "NSlib/Object/Object.h"
#include "NSlib/Object/Reflection/ActorRef.h"
#include "NSlib/Object/Reflection/Reflection.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneCamera.h"
#include "NSlib/Object/Scene/SceneManager.h"
#include "NSlib/Object/Transform.h"

// 標準ライブラリは CommonStl.h にまとめ、各層 PCH と同じセットを使う
#include "NSlib/CommonStl.h"

// Game / Editor がよく使う重い標準ライブラリも足す
#include <algorithm>
#include <cmath>
