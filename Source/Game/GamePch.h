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
#include "NSlib/Object/SubObject.h"
#include "NSlib/Object/SubObjects/Animation.h"
#include "NSlib/Object/SubObjects/BoxCollision.h"
#include "NSlib/Object/SubObjects/CameraManager.h"
#include "NSlib/Object/SubObjects/CapsuleCollision.h"
#include "NSlib/Object/SubObjects/Collision.h"
#include "NSlib/Object/SubObjects/MeshCollision.h"
#include "NSlib/Object/SubObjects/Model.h"
#include "NSlib/Object/SubObjects/OverlayRenderer.h"
#include "NSlib/Object/SubObjects/PlayerInput.h"
#include "NSlib/Object/SubObjects/Shadow.h"
#include "NSlib/Object/SubObjects/SphereCollision.h"
#include "NSlib/Object/SubObjects/ThirdPersonFollow.h"
#include "NSlib/Object/SubObjects/VirtualCamera.h"
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
