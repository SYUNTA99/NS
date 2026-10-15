#pragma once

// Game / Editor 側の共通プリコンパイルヘッダ。各 .cpp へ /FI で強制 include される
// 安定した NSlib 公開 API と標準ライブラリを入れ、windows.h を巻き込むヘッダは避ける

#include "NSlib/App/App.h"
#include "NSlib/Core/Core.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Graphics/Graphics.h"
#include "NSlib/Physics/Physics.h"
#include "NSlib/Windows/Windows.h"

// Object 層は一括ヘッダを持たないため、層の公開ヘッダを直接並べる
#include "NSlib/CoreObject/AssetManager.h"
#include "NSlib/CoreObject/IRenderable.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/ActorRef.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneCamera.h"
#include "NSlib/CoreObject/Scene/SceneManager.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObject.h"
#include "NSlib/CoreObject/SubObjects/Animation.h"
#include "NSlib/CoreObject/SubObjects/BoxCollision.h"
#include "NSlib/CoreObject/SubObjects/CapsuleCollision.h"
#include "NSlib/CoreObject/SubObjects/Collision.h"
#include "NSlib/CoreObject/SubObjects/MeshCollision.h"
#include "NSlib/CoreObject/SubObjects/Model.h"
#include "NSlib/CoreObject/SubObjects/OverlayRenderer.h"
#include "NSlib/CoreObject/SubObjects/PlayerInput.h"
#include "NSlib/CoreObject/SubObjects/Shadow.h"
#include "NSlib/CoreObject/SubObjects/SphereCollision.h"
#include "NSlib/CoreObject/SubObjects/ThirdPersonFollow.h"
#include "NSlib/CoreObject/SubObjects/Transform.h"
#include "NSlib/CoreObject/SubObjects/VirtualCamera.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/Graphics/RenderContext.h"

#include "NSlib/CommonStl.h"

#include <algorithm>
#include <cmath>
