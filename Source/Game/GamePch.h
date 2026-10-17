#pragma once

// Game / Editor 側の共通プリコンパイルヘッダ。各 .cpp へ /FI で強制 include される
// 安定した NSlib 公開 API と標準ライブラリを入れ、windows.h を巻き込むヘッダは避ける

#include "NSlib/App/App.h"
#include "NSlib/Core/Core.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Gfx/Gfx.h"
#include "NSlib/Phys/Phys.h"
#include "NSlib/Windows/Windows.h"

// Object 層は一括ヘッダを持たないため、層の公開ヘッダを直接並べる
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/Animation.h"
#include "NSlib/CoreObject/ClassObjs/BoxCollision.h"
#include "NSlib/CoreObject/ClassObjs/CapsuleCollision.h"
#include "NSlib/CoreObject/ClassObjs/Collision.h"
#include "NSlib/CoreObject/ClassObjs/MeshCollision.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/OverlayRenderer.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/ClassObjs/Shadow.h"
#include "NSlib/CoreObject/ClassObjs/SphereCollision.h"
#include "NSlib/CoreObject/ClassObjs/ThirdPersonFollow.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/ClassObjs/VirtualCamera.h"
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
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/Gfx/RenderContext.h"

#include "NSlib/CommonStl.h"

#include <algorithm>
#include <cmath>
