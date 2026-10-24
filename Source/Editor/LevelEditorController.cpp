#include "Editor/LevelEditorController.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"

#include "Editor/EditorActors.h"
#include "Editor/AssetPicker.h"
#include "Editor/GridMath.h"
#include "Editor/HitZoneColors.h"
#include "Editor/LevelFilePaths.h"
#include "Editor/Undo/CompositeCommand.h"
#include "Editor/Undo/ActorSnapshotCommand.h"
#include "Game/Level/CourseDirector.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/HitZones.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/PropActor.h"
#include "Game/Level/SlamAim.h"
#include "Game/PlayerActor.h"
#include "NSlib/App/Application.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Sphere.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/Asset/Texture.h"
#include "NSlib/CoreObject/ClassObjs/BoxCollision.h"
#include "NSlib/CoreObject/ClassObjs/CapsuleCollision.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/MeshCollision.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/SphereCollision.h"
#include "NSlib/CoreObject/ClassObjs/ThirdPersonFollow.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/ClassObjs/VirtualCamera.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/ActorInitInfo.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneCamera.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/Gfx/Render/DebugDraw.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/Windows/Clock.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/Input.h"
#include "NSlib/Windows/Keyboard.h"

#include <algorithm>
#include <charconv>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    // 部品の欄の値を JSON で読む。欄が無ければ値なしを返す
    nlohmann::json FieldJson(const NS::Object& comp, std::string_view fieldName)
    {
        const nlohmann::json fields = NS::SerializeClassObjFields(&comp);
        const nlohmann::json::const_iterator it = fields.find(std::string{fieldName});
        if (it == fields.end())
        {
            return nullptr;
        }
        return *it;
    }

    // カメラの視錐台を描く時の far。vcam の既定 1000 のままだと錐台が画面に収まらないので近くで切る
    constexpr float k_CameraGizmoFar = 8.0f;

    // 編集復帰の視点ブレンド秒。CameraManager の vcam 切替の既定 0.35 秒と揃え、モード切替の繋ぎを同じ感触にする
    constexpr float k_EditBlendSeconds = 0.35f;

    // 部品のうち動く体の当たり (Collider) を返す。持たなければ nullptr
    const NS::Collider* ColliderOf(const NS::SceneActor& object) noexcept
    {
        const NS::Collider* found = nullptr;
        NS::ForEachReflectedClassObj(&object, [&](const NS::ClassObjInfo&, const NS::Object* classObj) {
            if (found == nullptr)
            {
                found = NS::Cast<NS::Collider>(classObj);
            }
        });
        return found;
    }

    // 配置物 1 体の当たり形状を線で描く。Box は回転込み OBB、球とカプセルは実形状
    void DrawCollisionWireframe(NS::Gfx::DebugShapes& shapes, NS::SceneActor& object, const NS::Color& color) noexcept
    {
        if (NS::BoxCollision* box = NS::Cast<NS::BoxCollision>(object.CollisionClassObj()))
        {
            shapes.OBB(box->WorldOBB(object.Root().WorldMatrix()), color);
        }
        else if (NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(object.CollisionClassObj()))
        {
            shapes.Sphere(sphere->WorldSphere(object.Root().WorldMatrix()), color);
        }
        else if (NS::CapsuleCollision* capsule = NS::Cast<NS::CapsuleCollision>(object.CollisionClassObj()))
        {
            NS::Phys::Capsule worldCapsule = capsule->WorldCapsule(object.Root().WorldMatrix());
            worldCapsule.axis.Normalize();
            shapes.Capsule(
                worldCapsule.center, worldCapsule.axis * worldCapsule.halfHeight, worldCapsule.radius, color);
        }
        else if (const NS::Collider* collider = ColliderOf(object))
        {
            // 移動が掃引するのと同じ、根を中心にした縦のカプセル。根の拡縮は掛けない
            const NS::Phys::Capsule bodyCapsule = collider->CapsuleAt(object.Root().Position());
            shapes.Capsule(bodyCapsule.center, bodyCapsule.axis * bodyCapsule.halfHeight, bodyCapsule.radius, color);
        }
        else if (NS::Cast<NS::MeshCollision>(object.CollisionClassObj()) != nullptr)
        {
            // メッシュの当たりは見た目の三角形そのもの。三角形は多いので、見た目のメッシュを包む箱を出す
            const NS::Model* renderer = object.ModelClassObj();
            if (renderer == nullptr || renderer->GetMesh()->LocalBounds() == nullptr)
            {
                return;
            }
            const NS::AABB& local = *renderer->GetMesh()->LocalBounds();
            const NS::Matrix world = object.Root().WorldMatrix();
            const NS::AffineDecomposition parts = NS::DecomposeAffine(world);
            const NS::Vector3 center = NS::Vector3::Transform(NS::Vector3{local.Center}, world);
            const NS::Vector3 half{local.Extents.x * std::abs(parts.scale.x),
                                   local.Extents.y * std::abs(parts.scale.y),
                                   local.Extents.z * std::abs(parts.scale.z)};
            shapes.OBB(NS::MakeOBB(center, parts.rotation, half), color);
        }
    }

    // 配置物 1 体のヒットセンサーの形を線で描く。範囲 (落下死・ゴール) は地形の当たりを持たないので、ここで見せる
    void DrawSensorWireframe(NS::Gfx::DebugShapes& shapes, NS::SceneActor& object, const NS::Color& color) noexcept
    {
        for (const NS::HitSensor* sensor : {object.BodySensorClassObj(), object.AttackSensorClassObj()})
        {
            if (sensor == nullptr)
            {
                continue;
            }
            const NS::SensorVolume volume = sensor->WorldVolume(object.Root().WorldMatrix());
            if (volume.isBox)
            {
                shapes.OBB(volume.box, color);
                continue;
            }
            const NS::Vector3 center = (volume.a + volume.b) * 0.5f;
            const NS::Vector3 axis = (volume.b - volume.a) * 0.5f;
            if (axis.LengthSquared() > 0.0f)
            {
                shapes.Capsule(center, axis, volume.radius, color);
            }
            else
            {
                shapes.Sphere(NS::Sphere{center, volume.radius}, color);
            }
        }
    }

    // 正面の面の塗りの不透明度。外れの青に赤を重ねても、向こうの相手と床が透けて見える濃さ
    constexpr float k_HitFaceAlpha = 0.35f;
    // 直近の当たりの触れた点の印。相手の表面に出るので、玉の模様を隠さない小ささ
    constexpr float k_HitTouchMarkerRadius = 0.08f;

    // 面の上の形を 1 つ描く。中心から縁の点へ扇に塗り、縁の点を線で結ぶ
    // 形と縁の点と面の置き方は HitZones の関数が出す。判定と同じ値から描くので、色の境目が判定とずれない
    void DrawHitFaceShape(NS::Gfx::DebugShapes& shapes,
                          const GL::Level::HitFaceFrame& frame,
                          const GL::Level::HitFaceShape& shape) noexcept
    {
        const std::vector<NS::Vector2> outline = GL::Level::HitFaceShapeOutline(shape);
        const NS::Color edgeColor = NS::Editor::HitZoneColor(shape.tier);
        NS::Color fillColor = edgeColor;
        fillColor.A(k_HitFaceAlpha);
        const NS::Vector3 center = GL::Level::HitFacePoint(frame, shape.centerU, shape.centerV);
        for (std::size_t i = 0; i < outline.size(); ++i)
        {
            const NS::Vector2& from = outline[i];
            const NS::Vector2& to = outline[(i + 1) % outline.size()];
            const NS::Vector3 a = GL::Level::HitFacePoint(frame, from.x, from.y);
            const NS::Vector3 b = GL::Level::HitFacePoint(frame, to.x, to.y);
            shapes.Triangle(center, a, b, fillColor);
            shapes.Line(a, b, edgeColor);
        }
    }

    // a→b を 0.5m を目安に等分し 1 区間おきに線を引いて点線にする。DebugShapes に破線が無いので描画側で
    // 間引く。辺長からセグメント数を出すので、長い辺でも刻みが粗くならない
    void DrawDashedLine(NS::Gfx::DebugShapes& shapes,
                        const NS::Vector3& a,
                        const NS::Vector3& b,
                        const NS::Color& color) noexcept
    {
        const float length = (b - a).Length();
        const int rawSegments = static_cast<int>(length / 0.5f);
        const int segments = std::max(rawSegments, 2);
        for (int i = 0; i < segments; i += 2)
        {
            const float t0 = static_cast<float>(i) / static_cast<float>(segments);
            const float t1 = static_cast<float>(i + 1) / static_cast<float>(segments);
            shapes.Line(NS::Vector3::Lerp(a, b, t0), NS::Vector3::Lerp(a, b, t1), color);
        }
    }

    // カメラ pose の視錐台を四角錐の点線で描く。視点から far 面 4 隅へ 4 本 + far 面の 4 辺で、向きと画角を見せる
    // target==position や up と視線が平行な縮退では基底が作れないので何も描かない
    void DrawCameraFrustum(NS::Gfx::DebugShapes& shapes,
                           const NS::CameraPose& pose,
                           float aspect,
                           const NS::Color& color) noexcept
    {
        NS::Vector3 forward = pose.target - pose.position;
        if (forward.LengthSquared() < 1e-6f)
        {
            return;
        }
        forward.Normalize();
        NS::Vector3 right = pose.up.Cross(forward);
        if (right.LengthSquared() < 1e-6f)
        {
            return;
        }
        right.Normalize();
        const NS::Vector3 up = forward.Cross(right);

        const float halfHeight = std::tan(pose.fovY.value * 0.5f) * k_CameraGizmoFar;
        const float halfWidth = halfHeight * aspect;
        const NS::Vector3 farCenter = pose.position + forward * k_CameraGizmoFar;
        const NS::Vector3 topLeft = farCenter + up * halfHeight - right * halfWidth;
        const NS::Vector3 topRight = farCenter + up * halfHeight + right * halfWidth;
        const NS::Vector3 bottomLeft = farCenter - up * halfHeight - right * halfWidth;
        const NS::Vector3 bottomRight = farCenter - up * halfHeight + right * halfWidth;

        DrawDashedLine(shapes, pose.position, topLeft, color);
        DrawDashedLine(shapes, pose.position, topRight, color);
        DrawDashedLine(shapes, pose.position, bottomLeft, color);
        DrawDashedLine(shapes, pose.position, bottomRight, color);
        DrawDashedLine(shapes, topLeft, topRight, color);
        DrawDashedLine(shapes, topRight, bottomRight, color);
        DrawDashedLine(shapes, bottomRight, bottomLeft, color);
        DrawDashedLine(shapes, bottomLeft, topLeft, color);
    }

    // 視点マーカーのワールド空間での半径。カメラから遠いほど半径を伸ばし、画面上の見かけサイズを一定に近づける
    [[nodiscard]] float CameraMarkerHalf(const NS::Vector3& center, const NS::Matrix& vp) noexcept
    {
        const float baseHalf = 0.3f;
        return baseHalf * NS::Editor::ScreenConstantScale(center, vp);
    }

    // プレイ開始の凍結と編集へ戻る時の凍結を 1 体ずつ比べ、違う物を戻せる記録にする
    // 凍結へ入るのはプレイ中にインスペクタで写した欄だけなので、プレイで動いた値は比べに出ない
    std::vector<std::unique_ptr<NS::Editor::ICommand>> MakePlayEditCommands(const nlohmann::json& playStart,
                                                                            const nlohmann::json& playEnd)
    {
        std::vector<std::unique_ptr<NS::Editor::ICommand>> commands;
        const nlohmann::json& startObjects = NS::SceneJsonActors(playStart);
        for (const nlohmann::json& after : NS::SceneJsonActors(playEnd))
        {
            const NS::uint32 id = NS::ActorJsonId(after);
            const std::size_t index = NS::FindActorIndexById(playStart, id);
            if (index == NS::k_NoActorIndex)
            {
                continue;
            }
            const nlohmann::json& before = startObjects[index];
            if (before == after)
            {
                continue;
            }
            commands.push_back(std::make_unique<NS::Editor::ActorSnapshotCommand>(
                id, std::optional<nlohmann::json>{before}, std::optional<nlohmann::json>{after}));
        }
        return commands;
    }
} // namespace

LevelEditorController::LevelEditorController(NS::Scene* scene) noexcept : m_scene(scene), m_applier(scene)
{
    // 作る道と取り消しは Setup の前から使えるよう、適用処理と採番はここで差す
    m_editor.SetApplier(&m_applier);
    m_editor.SetAllocateIdFn([this]() { return m_scene->Actors().AllocateActorId(); });
    m_editor.SetSceneEditableFn([this]() { return IsSceneEditable(); });
}

bool LevelEditorController::IsSceneEditable() const noexcept
{
    return m_mode == Mode::Edit;
}

namespace
{
    class EnvironmentCommand final : public NS::Editor::ICommand
    {
    public:
        EnvironmentCommand(NS::Scene& scene, NS::SceneEnvironment before, NS::SceneEnvironment after)
            : m_scene(scene), m_before(std::move(before)), m_after(std::move(after))
        {
        }

        void Do(NS::Editor::IActorSnapshotApplier&) noexcept override { m_scene.SetEnvironment(m_after); }
        void Undo(NS::Editor::IActorSnapshotApplier&) noexcept override { m_scene.SetEnvironment(m_before); }
        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override
        {
            return sizeof(*this) + m_before.skybox.size() + m_after.skybox.size();
        }

    private:
        NS::Scene& m_scene;
        NS::SceneEnvironment m_before;
        NS::SceneEnvironment m_after;
    };

    // name の .actor を document に差し替えてファイルへ書き、name を指すアクターと name の派生を指すアクターの全部へ、
    // 全部の値を写し直す。シーンで上書きした項目はアクターの値が残る
    // 直し終えた時・取り消し・やり直しの全部がこの 1 本を通る
    void WriteActorFile(NS::Scene& scene, const std::string& name, const nlohmann::json& document)
    {
        NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
        if (!library.Set(name, document))
        {
            return;
        }
        if (!library.Save(name))
        {
            NS_LOG_WARN(App, ".actor {} をファイルへ書けなかった。今の起動の間だけ効く", name);
        }
        for (NS::SceneActor* actor : scene.Actors())
        {
            const nlohmann::json* actorFile = library.Find(actor->ActorFileName());
            if (actorFile == nullptr ||
                (actor->ActorFileName() != name && NS::ActorFileBase(*actorFile) != name))
            {
                continue;
            }
            NS::ApplyActorFile(actor);
            actor->ReloadAssets();
            // 束はアクターを作る時に .actor から読むので、読み直させて次の当たりに効かせる
            if (NS::HitReaction* reaction = actor->HitReactionClassObj())
            {
                reaction->ReloadSets(actor);
            }
        }
    }

    // actor の全部の部品の全部の欄の値。シーンの JSON と同じく classObjs の下に部品の鍵で並べる
    nlohmann::json AllFieldValues(const NS::SceneActor& actor)
    {
        nlohmann::json values = nlohmann::json::object();
        nlohmann::json& parts = NS::ActorJsonClassObjs(values);
        NS::ForEachReflectedClassObj(&actor, [&](const NS::ClassObjInfo& row, const NS::Object* part) {
            parts[std::string{row.key}] = NS::SerializeClassObjFields(part);
        });
        return values;
    }

    // before (AllFieldValues の形) から値が変わった、シーンに書く項目を actor の上書きの一覧に足す
    // 参照の欄はいつも保存するので足さない。.actor へ書く項目は変わっていても触らない
    void AddChangedSceneOverrides(NS::SceneActor& actor, const nlohmann::json& before)
    {
        NS::ForEachReflectedClassObj(&actor, [&](const NS::ClassObjInfo& row, const NS::Object* part) {
            const nlohmann::json now = NS::SerializeClassObjFields(part);
            const nlohmann::json* was = NS::ClassObjFields(before, row.key);
            for (nlohmann::json::const_iterator entry = now.begin(); entry != now.end(); ++entry)
            {
                // 触ったかを見る比べなので、値の写しどうしを == で比べる
                if (was != nullptr && was->contains(entry.key()) && was->at(entry.key()) == entry.value())
                {
                    continue;
                }
                const NS::FieldDesc* field = NS::FindField(part->GetReflection(), entry.key());
                if (field == nullptr || field->type == NS::FieldType::ActorRef ||
                    NS::FieldDestinationOf(&actor, *field) != NS::FieldDestination::Scene)
                {
                    continue;
                }
                actor.AddSceneOverride(row.key, entry.key());
            }
        });
    }

    // baseName の .actor から作る、name の派生の .actor の文書。元なら元を指して何も書いていない派生、派生なら同じ元を指す写し
    // name が空・ファイルの名前に使えない字を含む・既にある時と、baseName が読まれていない時は WARN を出して std::nullopt
    std::optional<nlohmann::json> NewDerivedActorFile(std::string_view baseName, std::string_view name)
    {
        NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
        if (name.empty() || name.find_first_of("\\/:*?\"<>|.") != std::string_view::npos)
        {
            NS_LOG_WARN(App, "派生の .actor の名前 \"{}\" はファイルの名前に使えないので作らない", name);
            return std::nullopt;
        }
        if (library.Find(name) != nullptr)
        {
            NS_LOG_WARN(App, ".actor {} は既にあるので派生を作らない", name);
            return std::nullopt;
        }
        const nlohmann::json* baseFile = library.Find(baseName);
        if (baseFile == nullptr)
        {
            NS_LOG_WARN(App, ".actor {} が読まれていないので派生 {} を作らない", baseName, name);
            return std::nullopt;
        }
        // 派生は元を 1 つだけ受け継ぐので、派生から作る時は同じ元を指す
        if (!NS::ActorFileBase(*baseFile).empty())
        {
            return std::optional<nlohmann::json>{*baseFile};
        }
        nlohmann::json derived = nlohmann::json::object();
        NS::SetActorFileClass(derived, NS::ActorFileClass(*baseFile));
        NS::SetActorFileBase(derived, baseName);
        NS::ActorJsonClassObjs(derived) = nlohmann::json::object();
        return derived;
    }

    // name の .actor を document で足してファイルへ書く。読めない文書か書けなければ足さずに false
    bool AddActorFile(std::string_view name, const nlohmann::json& document)
    {
        NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
        // 読めない訳は Set が ERROR で出している
        if (!library.Set(name, document))
        {
            return false;
        }
        if (!library.Save(name))
        {
            NS_LOG_WARN(App, ".actor {} をファイルへ書けなかったので足さない", name);
            library.Erase(name);
            return false;
        }
        return true;
    }

    // nlohmann::json::diff が作る 1 つの操作 (add・remove・replace) を document に当てられるか
    // add は親が要り、配列の親なら位置が末尾以下。remove と replace は当てる所が要る
    bool CanApplyPatchOperation(const nlohmann::json& document, const nlohmann::json& operation)
    {
        const nlohmann::json::json_pointer path{operation.at("path").get<std::string>()};
        if (operation.at("op") != "add")
        {
            return document.contains(path);
        }
        if (path.empty())
        {
            return true;
        }
        const nlohmann::json::json_pointer parentPath = path.parent_pointer();
        if (!document.contains(parentPath))
        {
            return false;
        }
        const nlohmann::json& parent = document[parentPath];
        if (parent.is_object())
        {
            return true;
        }
        if (!parent.is_array())
        {
            return false;
        }
        const std::string& last = path.back();
        if (last == "-")
        {
            return true;
        }
        std::size_t index = 0;
        const std::from_chars_result parsed = std::from_chars(last.data(), last.data() + last.size(), index);
        return parsed.ec == std::errc{} && parsed.ptr == last.data() + last.size() && index <= parent.size();
    }

    // .actor の 1 欄の書き換え。配置物でないので IActorSnapshotApplier を使わず、シーンと前後の差分を自分で持つ
    // 戻す・やり直す時は、その時の文書に差分だけを当てる。動作の窓は取り消しの履歴を通さずに文書を書くので、
    // 文書の全部を戻すと窓の直しが消える
    class ActorFileCommand final : public NS::Editor::ICommand
    {
    public:
        ActorFileCommand(NS::Scene& scene, std::string name, const nlohmann::json& before, const nlohmann::json& after)
            : m_scene(scene), m_name(std::move(name)), m_redo(nlohmann::json::diff(before, after)),
              m_undo(nlohmann::json::diff(after, before))
        {
            m_bytes = sizeof(*this) + m_name.size() + m_redo.dump().size() + m_undo.dump().size();
        }

        void Do(NS::Editor::IActorSnapshotApplier&) noexcept override { Apply(m_redo); }
        void Undo(NS::Editor::IActorSnapshotApplier&) noexcept override { Apply(m_undo); }
        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return m_bytes; }

    private:
        void Apply(const nlohmann::json& patch) noexcept
        {
            const nlohmann::json* current = NS::ActorFileLibrary::Get().Find(m_name);
            if (current == nullptr)
            {
                NS_LOG_WARN(App, ".actor {} が読まれていないので、取り消しかやり直しを当てない", m_name);
                return;
            }
            nlohmann::json document = *current;
            // 例外を切ってあるので、当てられない差分を patch に渡すと止まる。1 つずつ確かめて当てる
            for (const nlohmann::json& operation : patch)
            {
                if (!CanApplyPatchOperation(document, operation))
                {
                    // 間に別の書き換えが同じ所を消した。書かずに今の文書を残す
                    NS_LOG_WARN(App, ".actor {} の取り消しかやり直しを当てられなかった: {}", m_name,
                                operation.dump());
                    return;
                }
                document = document.patch(nlohmann::json::array({operation}));
            }
            WriteActorFile(m_scene, m_name, document);
        }

        NS::Scene& m_scene;
        std::string m_name;
        nlohmann::json m_redo;
        nlohmann::json m_undo;
        std::size_t m_bytes = 0;
    };
}

void LevelEditorController::SelectSceneSettings()
{
    ClearAssetSelection();
    SelectActorById(NS::k_NoActorId);
}

const std::string& LevelEditorController::SkyboxPath() const noexcept
{
    if (m_scene != nullptr)
    {
        return m_scene->SkyboxPath();
    }
    static const std::string empty;
    return empty;
}

bool LevelEditorController::SetSkyboxPath(std::string_view path)
{
    if (m_scene == nullptr || !IsSceneEditable())
    {
        return false;
    }
    std::string normalized;
    if (!path.empty())
    {
        const std::string root = NS::OS::FileSystem::ContentRoot();
        const std::optional<std::string> resolved = NS::OS::FileSystem::ResolveUnder(root, path);
        if (!resolved || !NS::Editor::IsSkyboxAssetPath(*resolved))
        {
            return false;
        }
        std::optional<std::string> relative = NS::OS::FileSystem::RelativeUnder(root, *resolved);
        if (!relative)
        {
            NS_LOG_ERROR(App, "空の素材 {} を ContentRoot からの道にできないので変えない", *resolved);
            return false;
        }
        normalized = std::move(*relative);
    }
    if (normalized != m_scene->SkyboxPath())
    {
        NS::SceneEnvironment after = m_scene->Environment();
        after.skybox = std::move(normalized);
        m_editor.Undo().Push(std::make_unique<EnvironmentCommand>(*m_scene, m_scene->Environment(), std::move(after)),
                             m_applier);
    }
    return true;
}

LevelEditorController::~LevelEditorController() = default;

bool LevelEditorController::PlayPaused() const noexcept
{
    return m_scene != nullptr && m_scene->IsSimulationPaused();
}

void LevelEditorController::TogglePlayPause() noexcept
{
    if (m_scene != nullptr)
    {
        m_scene->SetSimulationPaused(!m_scene->IsSimulationPaused());
        // 固定したままだと Inspector を触れず、プレイ中に値を調整する動線が消える。止めている間は解く
        // 再開は Game が映っている時だけ固定へ戻す。Scene を見ている時はタブを押せるよう出したまま
        if (m_mode == Mode::Play)
        {
            ApplyPlayCursor(NS::Editor::CursorAfterPauseToggle(
                {.paused = m_scene->IsSimulationPaused(), .gameViewInFront = !m_gameViewHidden}));
        }
    }
}

void LevelEditorController::RecaptureCursorOnGameClick(bool gameViewClicked) noexcept
{
    NS::Application* app = NS::Application::Get();
    if (app == nullptr)
    {
        return;
    }
    const bool recapture = NS::Editor::ShouldRecaptureCursor({.playMode = m_mode == Mode::Play,
                                                              .paused = PlayPaused(),
                                                              .cursorReleased = app->Window().IsCursorVisible(),
                                                              .gameViewClicked = gameViewClicked});
    if (recapture)
    {
        ApplyPlayCursor(NS::Editor::PlayCursor::Captured);
    }
}

void LevelEditorController::ApplyPlayCursor(NS::Editor::PlayCursor cursor) noexcept
{
    NS::Application* app = NS::Application::Get();
    if (app == nullptr)
    {
        return;
    }
    app->SetCursorCaptured(cursor == NS::Editor::PlayCursor::Captured);
}

const NS::ActorList& LevelEditorController::Actors() const noexcept
{
    return m_scene->Actors();
}

NS::CameraManager* LevelEditorController::Cameras() const noexcept
{
    if (m_scene == nullptr)
    {
        return nullptr;
    }
    return m_scene->GetCameraManager();
}

NS::SceneCamera* LevelEditorController::MainCamera() const noexcept
{
    if (m_scene == nullptr)
    {
        return nullptr;
    }
    return m_scene->MainCamera();
}

void LevelEditorController::Setup(std::string_view startLevelName)
{
    NS::Application* app = NS::Application::Get();
    if (app == nullptr || m_scene == nullptr)
    {
        return;
    }

    // 編集の投影設定で、遠景を 5000 まで見せ near 0.1 は既定
    // far は EditorCamera の k_MaxDistance より広く取り、最大ズームアウトでも地形を映す
    m_editorCamera.SetNearPlane(0.1f);
    m_editorCamera.SetFarPlane(5000.0f);
    m_editorCamera.SetFovY(NS::ToRadians(NS::Degrees{60.0f}));

    // 保存は実体から作り、読込は取込関数がデータを実体へ写して用済みにする
    m_editor.SetCaptureLevelFn([this]() { return m_scene->ToJson(); });
    m_editor.SetLoadLevelFn([this](nlohmann::json&& fresh) { m_scene->ResetFromJson(std::move(fresh)); });
    // セルの照会は実体で行う
    m_editor.SetFindCellActorFn([this](NS::int16 x, NS::int16 y, NS::int16 z) {
        return NS::Editor::FindActorIdAtCell(m_scene->Actors(), x, y, z);
    });
    m_editor.SetCellOccupiedFn([this](NS::int16 x, NS::int16 y, NS::int16 z) {
        return NS::Editor::HasPlacedActorAtCell(m_scene->Actors(), x, y, z);
    });
    m_editor.SetCollectCellsFn([this]() {
        std::vector<NS::Editor::EditorMode::CellCoord> cells;
        cells.reserve(m_scene->Actors().ActorCount());
        for (const NS::SceneActor* object : NS::Editor::PlacedActors(m_scene->Actors()))
        {
            if (!NS::Editor::IsCellBrushActor(object))
            {
                continue;
            }
            cells.push_back(NS::Editor::EditorMode::CellCoord{
                NS::Editor::ActorCellX(object), NS::Editor::ActorCellY(object), NS::Editor::ActorCellZ(object)});
        }
        return cells;
    });
    m_editor.SetInput(&app->Input());
    m_editor.SetSceneCamera(MainCamera());

    m_gizmo.SetInput(&app->Input());

    // Game が立てた空のシーンはプレイが回っている。プレイを終えて編集モードへ切替えてから起動シーンを開く
    // 先に開くと、プレイ開始の写しへ戻す組み直しが、開く時に補ったプレイヤーを消す
    LeavePlayForEdit();
    m_mode = Mode::Edit;
    if (!startLevelName.empty())
    {
        if (!m_editor.OpenLevel(startLevelName))
        {
            NS_LOG_ERROR(App, "起動シーンを開けなかった。 空のシーンで編集を始める: {}", startLevelName);
        }
    }
    // 選択候補はこの後に今の実体から集めるので、初回 Tick の貼り直しを省く
    m_editor.ClearLevelDirty();

    // 初期視点はプレイヤーの位置を中心に少し引いた位置から見下ろす。不在なら原点
    NS::SceneActor* bootPlayer = FindPlayer(m_scene->Actors());
    NS::Vector3 startCenter{0.0f, 0.0f, 0.0f};
    if (bootPlayer != nullptr)
    {
        startCenter = bootPlayer->Root().Position();
    }
    m_editorCamera.SetCenter(startCenter);
    // 起動直後は出現地点の block を真ん中近めに見せる距離。1m cube が画面の十数 % を占める
    m_editorCamera.SetDistance(5.0f);

    // 選択候補の生ポインタは組み直しの後に集める。先に集めると破棄済みの相手を指す
    RefreshGizmoSelectables();
}

void LevelEditorController::Teardown()
{
    // ギズモは配置物の Transform を非所有参照するので、scene 破棄前に選択を外す
    m_gizmo.ClearSelection();
    m_selectablePtrs.clear();
    m_selectablePickable.clear();
}

void LevelEditorController::EnterPlay() noexcept
{
    if (m_mode == Mode::Play)
    {
        return;
    }
    m_mode = Mode::Play;
    // モード遷移へ押しっぱなしを持ち越さないよう消す
    if (NS::Application* app = NS::Application::Get())
    {
        app->Input().Keyboard().ClearState();
        app->Input().Mouse().ClearState();
    }
    // 引いている途中の変形を確定してギズモを休ませる。引きを持ち越すと、編集へ戻る組み直しで消えた物を読む
    CommitTransformEdit();
    m_dragFollowers.clear();
    m_gizmoWasDragging = false;
    m_gizmo.SetActive(false);

    // 実体が唯一の出所なので組み直しは要らない。編集で動いた当たりは、世界を止めた歩が揃えている
    // 凍結を取り、そこから走行を最初から。プレイの間の判定と編集復帰の姿はこの凍結を読む
    // 手順は出荷と同じ CourseDirector::StartCourse の持ち物
    if (GL::Level::CourseDirector* director = NS::GetOrCreateSceneSystem<GL::Level::CourseDirector>(m_scene))
    {
        director->StartCourse();
    }
    // 編集へ戻る時に、プレイ中に写した欄をこの凍結と比べて履歴へ記録する
    m_playStartBaseline = m_scene->PlayBaseline();
    // 編集の自由視点からプレイ視点へ、vcam 切替と同じブレンドで繋ぐ
    if (NS::CameraManager* cameras = Cameras())
    {
        cameras->BeginBlendFrom(m_editorCamera.Pose());
    }
    m_scene->SetSimulationEnabled(true);

    // プレイ突入はカーソルを消し、マウスを相対モードにして視点操作をカーソル位置から切り離す
    ApplyPlayCursor(NS::Editor::PlayCursor::Captured);

    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::EnterEdit() noexcept
{
    if (m_mode == Mode::Edit)
    {
        return;
    }
    m_mode = Mode::Edit;
    // モード遷移へ押しっぱなしを持ち越さないよう消す
    if (NS::Application* app = NS::Application::Get())
    {
        app->Input().Keyboard().ClearState();
        app->Input().Mouse().ClearState();
    }
    // プレイを終え、凍結スナップショットから編集の姿へ組み直す
    LeavePlayForEdit();
    // プレイ視点から自由視点へ繋ぐ。始点は直前まで実カメラに書かれていた pose
    if (NS::CameraManager* cameras = Cameras())
    {
        m_editBlendFrom = cameras->LastPose();
        m_editBlendElapsed = 0.0f;
        m_editBlending = true;
    }
    // 編集復帰の組み直しを跨いだ選択を、id から現在のオブジェクトへ貼り直してから編集へ戻る
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::RequestStepFrame() noexcept
{
    if (m_mode != Mode::Play || m_scene == nullptr)
    {
        return;
    }
    m_scene->StepSimulation();
}

void LevelEditorController::LeavePlayForEdit()
{
    if (m_scene == nullptr)
    {
        return;
    }
    m_scene->SetSimulationEnabled(false);

    // プレイで変わった配置を編集へ持ち込まないため、開始時の控えから復元する
    nlohmann::json baseline = m_scene->PlayBaseline();
    std::vector<std::unique_ptr<NS::Editor::ICommand>> playEdits;
    if (m_playStartBaseline)
    {
        playEdits = MakePlayEditCommands(*m_playStartBaseline, baseline);
        m_playStartBaseline.reset();
    }
    // 履歴は残す。持っているアクターは組み直しが捨てる管理クラスを指すので、先に解放して JSON から戻す形にする
    m_editor.Undo().ReleaseHeldActors();
    m_scene->ResetFromJson(std::move(baseline));
    // プレイ中の変更は 1 つずつ積まず、戻る時に 1 まとまりで積む。実体は組み直しで既に変えた後の姿
    if (!playEdits.empty())
    {
        m_editor.Undo().Record(MakeUndoUnit(std::move(playEdits)));
    }

    if (NS::Application* app = NS::Application::Get())
    {
        app->SetCursorCaptured(false);
    }
}

void LevelEditorController::SetGameView(int x, int y, int width, int height, bool hovered) noexcept
{
    m_gameViewRect = NS::Editor::ViewRect{x, y, width, height};
    m_gameViewRectValid = true;
    m_gameViewHovered = hovered;
    m_gameViewHidden = false;
    // 窓の中心だと別のパネルの上へ乗ることがある。プレイ中は Game ビューの中心へ留める
    if (m_mode == Mode::Play)
    {
        if (NS::Application* app = NS::Application::Get())
        {
            app->Window().SetCursorLockPoint(x + width / 2, y + height / 2);
        }
    }
    // 編集入力は表示矩形から光線を飛ばす。カーソルが重なっていない間は配置位置を表示しない
    m_editor.SetViewRect(m_gameViewRect);
    m_editor.SetViewHovered(hovered);
}

void LevelEditorController::ClearGameView() noexcept
{
    // 全画面描画では予備の矩形を使い、カーソルが重なっている状態にする
    m_gameViewRectValid = false;
    m_gameViewHovered = true;
    m_gameViewHidden = false;
    m_editor.SetViewRect(CurrentViewRect());
    m_editor.SetViewHovered(true);
}

void LevelEditorController::HideGameView() noexcept
{
    // 前面のパネルが裏へ隠れた。配置カーソルと編集オーバーレイを止める
    m_gameViewHidden = true;
    m_gameViewHovered = false;
    m_editor.SetViewHovered(false);
}

NS::Editor::ViewRect LevelEditorController::CurrentViewRect() const noexcept
{
    if (m_gameViewRectValid)
    {
        return m_gameViewRect;
    }
    // 未設定時は全画面をフォールバックとする。ウィンドウ不在は 0 サイズ
    NS::Editor::ViewRect full{};
    if (NS::Application* app = NS::Application::Get())
    {
        const NS::Size2D size = app->Window().Size();
        full.width = size.width;
        full.height = size.height;
    }
    return full;
}

void LevelEditorController::TickPlaySceneView(const NS::Editor::EditorCameraInput& input) noexcept
{
    // プレイ中に Scene タブへ自由視点を映すフレームだけ効かせる。編集モード中は実カメラを触らない
    // 描画視点は SceneViewPose が自由視点の pose を渡すので、ここは入力適用だけでよい
    if (m_mode != Mode::Play)
    {
        return;
    }
    m_editorCamera.ApplyInput(input);
}

std::optional<NS::CameraPose> LevelEditorController::SceneViewPose() noexcept
{
    // 編集中は実カメラ (TickEdit が自由視点の pose を書く) 任せ。プレイ中だけ自由視点を上書きする
    if (m_mode != Mode::Play)
    {
        return std::nullopt;
    }
    return m_editorCamera.Pose();
}

std::optional<NS::CameraPose> LevelEditorController::GameViewPose() noexcept
{
    // プレイ中は CameraManager (follow・ブレンド維持) 任せ。編集中だけゲームカメラを上書きする
    if (m_mode == Mode::Play || Cameras() == nullptr)
    {
        return std::nullopt;
    }
    return Cameras()->EvaluateTopPose(1.0f);
}

nlohmann::json LevelEditorController::SceneSnapshot() const
{
    if (m_scene == nullptr)
    {
        return NS::MakeSceneJson();
    }
    return m_scene->ToJson();
}

void LevelEditorController::SetSceneViews(std::vector<NS::SceneView> views)
{
    if (m_scene != nullptr)
    {
        m_scene->SetSceneViews(std::move(views));
    }
}

void LevelEditorController::Tick()
{
    // プレイ中のクリア / 死亡は応答部品が出荷と同じ手順で完結させる
    // editor は割り込まず、編集へ戻るのは Tab / Pause modal の明示操作だけ
    if (m_mode == Mode::Edit)
    {
        TickEdit();
        return;
    }

    // Esc はカーソルの解放に使い、パネルを操作する途中でプレイを終了させない
    NS::Application* app = NS::Application::Get();
    if (app == nullptr)
    {
        return;
    }
    if (app->Input().Keyboard().IsPressed(NS::OS::Key::Escape) && !app->Window().IsCursorVisible())
    {
        ApplyPlayCursor(NS::Editor::PlayCursor::Released);
    }
}

void LevelEditorController::TickEdit()
{
    NS::Application* app = NS::Application::Get();
    if (app == nullptr)
    {
        return;
    }

    // F5 のシェーダ再読込は編集時だけ。プレイ時は UI 表示の切替に使う
    if (!m_editShortcutsBlocked && app->Input().Keyboard().IsPressed(NS::OS::Key::F5))
    {
        app->Assets().ReloadAllShaders();
    }

    // Esc: Object モードで選択中ならまず選択解除に使い終了させない
    if (app->Input().Keyboard().IsPressed(NS::OS::Key::Escape))
    {
        if (m_gizmoToolActive && m_gizmo.Selected() != nullptr)
        {
            // 選択の id ごと外す。ギズモだけ外すと、次のフレームに id から選び直される
            SelectActorById(NS::k_NoActorId);
            return;
        }
        NS::Application::Quit();
        return;
    }

    m_editorCamera.Tick();

    // 自由視点を更新して実カメラへ反映し、ギズモと編集の光線判定に同じ視点を使う
    // 編集中は世界が止まり CameraManager が実カメラを書かないので、この書き込みが上書きされずに残る
    if (NS::SceneCamera* camera = MainCamera())
    {
        NS::CameraPose pose = m_editorCamera.Pose();
        if (m_editBlending)
        {
            m_editBlendElapsed += NS::OS::FrameTimer::FixedDelta();
            const float t = std::min(m_editBlendElapsed / k_EditBlendSeconds, 1.0f);
            const float eased = NS::SmoothStep(t);
            pose = NS::CameraPose::Lerp(m_editBlendFrom, pose, eased);
            if (t >= 1.0f)
            {
                m_editBlending = false;
            }
        }
        camera->ApplyPose(pose);
    }

    // 同時に 1 モードだけがクリックと R を消費する。Object 中は grid 入力を抑制しギズモへ回す
    // モード切替は Editor の UI ボタンが SetGizmoToolActive で入れる。Tab は Edit↔Play 専用
    const bool objectMode = m_gizmoToolActive;
    m_gizmo.SetActive(objectMode);
    m_editor.SetInputSuppressed(objectMode);

    if (objectMode && Cameras())
    {
        // 追従カメラの Root を実プレイ視点位置へ寄せてからギズモを回す。判定箱とギズモがその位置に出る
        SyncFollowCameraPoses();

        // 組み直しで破棄された配置物を参照しないよう、候補と選択を現在の実体へ引き直す
        RefreshGizmoSelectables();
        ResolveSelectionFromId();

        const NS::Matrix vp = Cameras()->ViewProjection();
        const bool wasDragging = m_gizmoWasDragging;
        m_gizmo.Tick(vp, CurrentViewRect(), m_gameViewHovered, m_editShortcutsBlocked);

        // ビューポートでのギズモ選択変化を選択 id と Inspector が見る派生添字へ追従させる
        CaptureSelectionFromGizmo();

        // 追従カメラを掴んでいたら Root 位置を初期姿勢へ逆算し ThirdPersonFollow へ書き戻す。位置の書き戻しはこちら
        ApplyFollowCameraGizmoDrag();

        // ドラッグ開始で baseline を控え、終了で変わった分のスナップショットを履歴へ積む。grid undo と同じ経路
        // 追従カメラの引きは初期姿勢の欄を変える。控えは部品の値まで写すので同じ道で積める
        const bool nowDragging = m_gizmo.IsDragging();
        if (!wasDragging && nowDragging)
        {
            BeginTransformEdit();
            // 掴んだ瞬間はまだ動いていない。ここで控えれば差分の基準が揃う
            CaptureDragFollowers();
        }
        else if (wasDragging && !nowDragging)
        {
            CommitTransformEdit();
            m_dragFollowers.clear();
        }
        // ギズモは主対象しか動かさないので、残りの選択はここで追わせる
        if (nowDragging)
        {
            ApplyDragToFollowers();
        }
        m_gizmoWasDragging = nowDragging;
    }
    else if (m_transformEditing)
    {
        // Object モードを抜けても未確定の変形があれば確定し、記録されない変更を残さない
        CommitTransformEdit();
        m_gizmoWasDragging = false;
    }

    // 掴んでいる間は選択の貼り直しを飛ばすので、undo が旧 Transform を指す
    m_editor.SetUndoRedoSuppressed(m_gizmo.IsDragging());
    m_editor.Tick(m_editShortcutsBlocked);
    if (m_editor.IsLevelDirty())
    {
        // grid 編集・undo・読込は applier / reload が配置物を組み直し済。候補と gizmo を今の実体へ貼り直す
        // 消えた選択や読込での id 振り直しは解決が自然に外す
        RefreshGizmoSelectables();
        ResolveSelectionFromId();
        m_editor.ClearLevelDirty();
    }
}

void LevelEditorController::DrawSceneViewShapes(NS::Gfx::DebugShapes& shapes, const NS::Matrix& viewProjection) noexcept
{
    if (m_scene == nullptr)
    {
        return;
    }

    if (m_mode != Mode::Edit)
    {
        // 動いている形をそのまま追えるよう選択に関わらず全部出す
        RenderCollisionWireframes(shapes, true);
        RenderHitFaces(shapes);
        return;
    }

    m_editor.DrawCursorShapes(shapes);
    // 錐台の横幅はゲーム画面の比で出す。窓が無い時は RenderCameraGizmos が 16:9 で代える
    NS::Size2D viewport{};
    if (const NS::Application* app = NS::Application::Get())
    {
        viewport = app->Window().Size();
    }
    RenderCameraGizmos(shapes, viewProjection, viewport);
    RenderCollisionWireframes(shapes, false);
    RenderHitFaces(shapes);
    RenderSelectionOutlines(shapes);
}

void LevelEditorController::Render()
{
    // プレイ中に Scene タブへ出す図形は DrawSceneViewShapes が積む。ImGui へ重ねる物は無い
    if (m_mode != Mode::Edit)
    {
        return;
    }

    m_editor.RenderCursorPreview();
    // Object モードはブラシを置かないので Build モードの時だけ出す
    // Game ビュー前面などで編集ビューが隠れているフレームは、ゲーム画面へ被せないよう出さない
    if (!GizmoToolActive() && !m_gameViewHidden)
    {
        m_editor.Palette().Render(CurrentViewRect());
    }
    // Object モードのギズモは最前面の drawlist に重ねる
    if (m_gizmo.IsActive() && Cameras())
    {
        m_gizmo.Render(Cameras()->ViewProjection(), CurrentViewRect());
    }
}

void LevelEditorController::SetGizmoToolActive(bool active) noexcept
{
    if (active == m_gizmoToolActive)
    {
        return;
    }
    m_gizmoToolActive = active;
    if (!active)
    {
        SelectActorById(NS::k_NoActorId);
    }
}

bool LevelEditorController::HasInspectableSelection() const noexcept
{
    return m_selectedActorId != NS::k_NoActorId && m_scene->Actors().FindByActorId(m_selectedActorId) != nullptr;
}

NS::SceneActor* LevelEditorController::SelectedActor() noexcept
{
    // 選択の真実は永続 id。player も含め全配置物が ActorList に居るので id で引く
    if (m_selectedActorId == NS::k_NoActorId)
    {
        return nullptr;
    }
    return m_scene->Actors().FindByActorId(m_selectedActorId);
}

bool LevelEditorController::SelectedIsPlayerActor() const noexcept
{
    NS::SceneActor* player = FindPlayer(m_scene->Actors());
    return player != nullptr && m_selectedActorId != NS::k_NoActorId && m_selectedActorId == player->Id();
}

void LevelEditorController::RefreshGizmoSelectables()
{
    m_selectablePtrs.clear();
    m_selectablePickable.clear();
    m_selectablePtrs.reserve(m_scene->Actors().ActorCount());
    m_selectablePickable.reserve(m_scene->Actors().ActorCount());

    // 可視メッシュを持つ候補は 1、見えないカメラ等は 0。ギズモは 1 の候補を先に選ぶ
    // 全配置物をギズモ候補に積む。判定箱はギズモがクリックの時に配置物から引く
    for (NS::SceneActor* object : NS::Editor::PlacedActors(m_scene->Actors()))
    {
        m_selectablePtrs.push_back(object);
        m_selectablePickable.push_back(static_cast<NS::uint8>(object->ModelClassObj() != nullptr));
    }

    m_gizmo.SetSelectableActors(m_selectablePtrs, m_selectablePickable);
}

NS::ThirdPersonFollow* LevelEditorController::SelectedFollowCamera() noexcept
{
    if (GL::Level::FollowCameraActor* camera = NS::Cast<GL::Level::FollowCameraActor>(SelectedActor()))
    {
        return &camera->Vcam();
    }
    return nullptr;
}

void LevelEditorController::SyncFollowCameraPoses()
{
    // 追従カメラは位置を持たないので、edit 中は実プレイの視点位置へ Root を寄せて frustum / pick / ギズモを出す
    // ドラッグ中の選択カメラだけは gizmo が Root を握るため触らず、その位置を初期姿勢へ逆算する側に任せる
    const NS::ActorList& objects = m_scene->Actors();
    for (NS::SceneActor* object : objects)
    {
        GL::Level::FollowCameraActor* camera = NS::Cast<GL::Level::FollowCameraActor>(object);
        if (camera == nullptr)
        {
            continue;
        }
        NS::ThirdPersonFollow* follow = &camera->Vcam();
        if (m_gizmo.IsDragging() && object->Id() == m_selectedActorId)
        {
            continue;
        }
        object->Root().SetPosition(follow->EvaluatePose(1.0f).position);
    }
}

void LevelEditorController::ApplyFollowCameraGizmoDrag()
{
    // ドラッグ中の追従カメラは、gizmo が動かした Root 位置から初期姿勢の方位角・仰角/距離を逆算して
    // 実体の部品へ書き戻す。Root 位置は初期姿勢由来なので保存対象は部品側になる
    if (!m_gizmo.IsDragging())
    {
        return;
    }
    NS::ThirdPersonFollow* follow = SelectedFollowCamera();
    if (follow == nullptr)
    {
        return;
    }
    NS::SceneActor* go = SelectedActor();
    if (go == nullptr)
    {
        return;
    }
    follow->SetInitialPoseFromCameraPosition(go->Root().Position());
}

void LevelEditorController::SelectActorById(NS::uint32 id) noexcept
{
    m_selectionIds.clear();
    if (id != NS::k_NoActorId)
    {
        m_selectionIds.push_back(id);
    }
    SetPrimarySelection(id);
}

bool LevelEditorController::IsActorSelected(NS::uint32 id) const noexcept
{
    if (id == NS::k_NoActorId)
    {
        return false;
    }
    return std::find(m_selectionIds.begin(), m_selectionIds.end(), id) != m_selectionIds.end();
}

void LevelEditorController::ToggleActorSelection(NS::uint32 id) noexcept
{
    if (id == NS::k_NoActorId)
    {
        return;
    }

    const std::vector<NS::uint32>::iterator it = std::find(m_selectionIds.begin(), m_selectionIds.end(), id);
    if (it != m_selectionIds.end())
    {
        m_selectionIds.erase(it);
        if (m_selectedActorId != id)
        {
            return;
        }
        // 主対象を外したので、残っている中の直近へ譲る
        NS::uint32 next = NS::k_NoActorId;
        if (!m_selectionIds.empty())
        {
            next = m_selectionIds.back();
        }
        SetPrimarySelection(next);
        return;
    }

    m_selectionIds.push_back(id);
    SetPrimarySelection(id);
}

void LevelEditorController::SelectActors(std::vector<NS::uint32> ids, NS::uint32 primary) noexcept
{
    m_selectionIds = std::move(ids);
    if (primary != NS::k_NoActorId && !IsActorSelected(primary))
    {
        m_selectionIds.push_back(primary);
    }
    SetPrimarySelection(primary);
}

void LevelEditorController::SetPrimarySelection(NS::uint32 id) noexcept
{
    m_selectedActorId = id;

    if (id == NS::k_NoActorId)
    {
        m_gizmo.ClearSelection();
        m_lastGizmoSelected = nullptr;
        return;
    }

    // アクターの選択と素材の選択は排他
    ClearAssetSelection();

    // ハンドルを出すため Object ツールへ切替える。Build のままだとギズモが描かれない
    SetGizmoToolActive(true);

    // player も含め全配置物が ActorList に居る。選択した Root を id で引いてギズモへ貼る
    if (NS::SceneActor* go = m_scene->Actors().FindByActorId(id))
    {
        m_gizmo.SetSelected(&go->Root());
        m_lastGizmoSelected = m_gizmo.Selected();
        return;
    }
    m_gizmo.ClearSelection();
    m_lastGizmoSelected = nullptr;
}

void LevelEditorController::RenderCameraGizmos(NS::Gfx::DebugShapes& shapes,
                                               const NS::Matrix& viewProjection,
                                               NS::Size2D viewport) noexcept
{
    // edit 中、各カメラの視錐台を点線の四角錐で、視点位置を小箱で可視化する
    // 選択中は強調色にする。追従カメラは pose がプレイヤー基準なので、錐台はプレイ中に居る視点位置へ出る
    const NS::ActorList& objects = m_scene->Actors();
    // 錐台の横幅は実ビューポート比で出す。viewport が潰れている時だけ 16:9 目安へフォールバックする
    const float aspect = [viewport]() -> float {
        if (viewport.height > 0)
        {
            return static_cast<float>(viewport.width) / static_cast<float>(viewport.height);
        }
        return 16.0f / 9.0f;
    }();
    for (NS::SceneActor* object : objects)
    {
        GL::Level::FollowCameraActor* camera = NS::Cast<GL::Level::FollowCameraActor>(object);
        if (camera == nullptr)
        {
            continue;
        }
        NS::VirtualCamera* vcam = &camera->Vcam();
        const bool selected = (object->Id() == m_selectedActorId);
        const NS::Color camColor = [selected]() -> NS::Color {
            if (selected)
            {
                return NS::Color{1.0f, 0.55f, 0.10f, 1.0f};
            }
            return NS::Color{1.0f, 0.85f, 0.10f, 1.0f};
        }();

        const NS::CameraPose pose = vcam->EvaluatePose(1.0f);
        DrawCameraFrustum(shapes, pose, aspect, camColor);
        const float markerHalf = CameraMarkerHalf(pose.position, viewProjection);
        shapes.AABB(NS::AABB{pose.position, NS::Vector3{markerHalf, markerHalf, markerHalf}}, camColor);
    }
}

void LevelEditorController::RenderSelectionOutlines(NS::Gfx::DebugShapes& shapes) noexcept
{
    // ギズモが出るのは主対象だけなので、一緒に選んでいる分は枠で見せる
    if (m_selectionIds.size() < 2)
    {
        return;
    }

    const NS::Color color{1.0f, 0.65f, 0.15f, 1.0f};
    for (const NS::uint32 id : m_selectionIds)
    {
        if (id == m_selectedActorId)
        {
            continue;
        }
        NS::SceneActor* object = m_scene->Actors().FindByActorId(id);
        if (object == nullptr)
        {
            continue;
        }

        const NS::Matrix world = object->Root().WorldMatrix();
        const NS::AffineDecomposition parts = NS::DecomposeAffine(world);
        // 1m 立方の cube mesh の半サイズ 0.5 に拡縮を掛ける
        const NS::Vector3 half{
            std::abs(parts.scale.x) * 0.5f, std::abs(parts.scale.y) * 0.5f, std::abs(parts.scale.z) * 0.5f};
        shapes.OBB(NS::MakeOBB(world.Translation(), parts.rotation, half), color);
    }
}

void LevelEditorController::RenderCollisionWireframes(NS::Gfx::DebugShapes& shapes, bool all) noexcept
{
    const NS::Color color{0.35f, 1.0f, 0.45f, 1.0f};
    // センサーは当たりの緑と見分けが付く橙
    const NS::Color sensorColor{1.0f, 0.6f, 0.2f, 1.0f};

    // プレイ中は動いている形を追えるよう全部出す
    if (all)
    {
        for (NS::SceneActor* objPtr : m_scene->Actors())
        {
            DrawCollisionWireframe(shapes, *objPtr, color);
            DrawSensorWireframe(shapes, *objPtr, sensorColor);
        }
        return;
    }

    // 編集中は選んだ分だけ。全部出すと線が重なって、どれの形か読み取れない
    for (const NS::uint32 id : m_selectionIds)
    {
        if (NS::SceneActor* objPtr = m_scene->Actors().FindByActorId(id))
        {
            DrawCollisionWireframe(shapes, *objPtr, color);
            DrawSensorWireframe(shapes, *objPtr, sensorColor);
        }
    }
}

void LevelEditorController::RenderHitFaces(NS::Gfx::DebugShapes& shapes) noexcept
{
    PlayerActor* player = FindPlayer(m_scene->Actors());
    // 自機が居ない場面は半径 0 として、相手の輪郭の大きさで描く
    float playerRadius = 0.0f;
    NS::Vector3 ballCenter{};
    GL::Level::SlamLineTarget aim{};
    bool aiming = false;
    if (player != nullptr)
    {
        // 突進の玉は、狙う相手の探し方と同じ PlayerActor::SlamBallAt から引く
        const NS::Sphere ball = player->SlamBallAt(player->Root().Position());
        ballCenter = ball.center;
        playerRadius = ball.radius;
        aiming = player->TryGetAimTarget(aim);
    }
    const NS::Vector3 cameraPosition = m_editorCamera.Pose().position;

    for (NS::SceneActor* object : m_scene->Actors())
    {
        if (!object->IsAppeared())
        {
            continue;
        }
        const GL::Level::PropActor* prop = NS::Cast<GL::Level::PropActor>(object);
        const NS::HitSensor* bodySensor = object->BodySensorClassObj();
        if (prop == nullptr || bodySensor == nullptr)
        {
            continue;
        }
        const GL::Level::HitZones& zones = prop->Zones();
        // 調べる対象から外した体は、突進が当たらないので面も出さない
        if (!bodySensor->IsValid())
        {
            continue;
        }
        const NS::SensorVolume body = bodySensor->WorldVolume(object->Root().WorldMatrix());
        NS::Vector3 direction = body.Center() - cameraPosition;
        if (player != nullptr)
        {
            direction = body.Center() - ballCenter;
        }
        if (aiming && aim.target == object)
        {
            direction = aim.direction;
        }
        // 真上や真下から見て水平の向きが決まらない相手は描かない
        GL::Level::HitFaceFrame frame;
        if (!GL::Level::MakeHitFaceFrame(body, direction, playerRadius, frame))
        {
            continue;
        }
        for (const GL::Level::HitFaceShape& shape : GL::Level::HitFaceShapes(zones.Face(), frame.bodyShape))
        {
            DrawHitFaceShape(shapes, frame, shape);
        }
    }

    if (player == nullptr)
    {
        return;
    }
    const GL::Level::ImpactRecord& impact = player->Resolver().Answer().lastImpact;
    if (impact.sequence == 0)
    {
        return;
    }
    shapes.Sphere(NS::Sphere{impact.surfacePoint, k_HitTouchMarkerRadius}, NS::Editor::HitZoneColor(impact.tier));
}

void LevelEditorController::CaptureSelectionFromGizmo() noexcept
{
    NS::Transform* selected = m_gizmo.Selected();
    // 一覧で選んだ非 gizmo 選択を毎フレーム潰さないため前フレームと同じなら据え置き
    if (selected == m_lastGizmoSelected)
    {
        return;
    }
    m_lastGizmoSelected = selected;
    if (selected == nullptr)
    {
        m_selectionIds.clear();
        m_selectedActorId = NS::k_NoActorId;
        return;
    }
    // player も ActorList に居るので、ビューポートのピックと同じ経路で引ける
    for (NS::SceneActor* object : m_scene->Actors())
    {
        if (&object->Root() == selected)
        {
            // ビューポートのクリックは 1 体に絞る。複数選択はアクター一覧の Ctrl / Shift で組む
            m_selectedActorId = object->Id();
            m_selectionIds.assign(1, m_selectedActorId);
            ClearAssetSelection();
            return;
        }
    }
    m_selectionIds.clear();
    m_selectedActorId = NS::k_NoActorId;
}

void LevelEditorController::ResolveSelectionFromId() noexcept
{
    // SetSelected が進行中ドラッグを切ってしまうのでドラッグ中は gizmo の選択を貼り直さない
    if (m_gizmo.IsDragging())
    {
        return;
    }

    // 選択 id が現存する配置物を指すなら gizmo に貼り直す。不在なら gizmo を外す
    // player も ActorList に居るので id で引ける。rebuild を跨いでも掴める状態を保つ
    if (m_selectedActorId != NS::k_NoActorId)
    {
        if (NS::SceneActor* go = m_scene->Actors().FindByActorId(m_selectedActorId))
        {
            NS::Transform* root = &go->Root();
            if (m_gizmo.Selected() != root)
            {
                m_gizmo.SetSelected(root);
            }
            m_lastGizmoSelected = root;
            return;
        }
    }
    if (m_gizmo.Selected() != nullptr)
    {
        m_gizmo.ClearSelection();
    }
    m_lastGizmoSelected = nullptr;
}

void LevelEditorController::MirrorPlayEditToBaseline(const NS::SceneActor* actor,
                                                     const NS::ClassObjInfo& row,
                                                     std::string_view fieldName)
{
    NS_ASSERT(App, actor != nullptr, "MirrorPlayEditToBaseline: アクターが null");
    // 写すのはプレイ中だけ。編集モードで写すと次のプレイ突入の捕捉と二重管理になる
    if (m_mode != Mode::Play || m_scene == nullptr)
    {
        return;
    }
    m_scene->WritePlayBaselineField(actor, row, fieldName);
}

void LevelEditorController::NoteFieldEdit(const NS::SceneActor* actor,
                                          const NS::ClassObjInfo& row,
                                          std::string_view fieldName)
{
    NS_ASSERT(App, actor != nullptr, "NoteFieldEdit: アクターが null");
    const NS::Object* part = row.get(actor);
    if (part == nullptr)
    {
        return;
    }
    const NS::FieldDesc* field = NS::FindField(part->GetReflection(), fieldName);
    if (field == nullptr)
    {
        return;
    }
    if (NS::FieldDestinationOf(actor, *field) == NS::FieldDestination::Scene)
    {
        // 保存は上書きの一覧の項目だけを書くので、直したら一覧に足す。参照の欄はいつも書く
        NS::SceneActor* edited = nullptr;
        if (m_scene != nullptr)
        {
            edited = m_scene->Actors().FindByActorId(actor->Id());
        }
        if (edited != nullptr && field->type != NS::FieldType::ActorRef)
        {
            edited->AddSceneOverride(row.key, fieldName);
        }
        MirrorPlayEditToBaseline(actor, row, fieldName);
        return;
    }
    for (const ActorFileEdit& edit : m_actorFileEdits)
    {
        if (edit.actorId == actor->Id() && edit.row == &row && edit.fieldName == fieldName)
        {
            return;
        }
    }
    m_actorFileEdits.push_back(ActorFileEdit{actor->Id(), &row, std::string{fieldName}});
}

void LevelEditorController::NoteActorFileFieldEdit(const NS::ClassObjInfo& row, std::string_view fieldName)
{
    if (m_actorFileDraft == nullptr)
    {
        return;
    }
    const NS::Object* part = row.get(m_actorFileDraft.get());
    if (part == nullptr)
    {
        return;
    }
    const NS::FieldDesc* field = NS::FindField(part->GetReflection(), fieldName);
    if (field == nullptr || field->type == NS::FieldType::ActorRef)
    {
        return;
    }
    m_assetEdited = true;
    for (const ActorFileEdit& edit : m_actorFileEdits)
    {
        if (edit.actorId == NS::k_NoActorId && edit.row == &row && edit.fieldName == fieldName)
        {
            return;
        }
    }
    m_actorFileEdits.push_back(ActorFileEdit{NS::k_NoActorId, &row, std::string{fieldName}});
}

void LevelEditorController::CommitActorFileEdits()
{
    std::vector<ActorFileEdit> edits = std::move(m_actorFileEdits);
    m_actorFileEdits.clear();
    if (m_scene == nullptr)
    {
        return;
    }
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    for (const ActorFileEdit& edit : edits)
    {
        // 選んだ .actor の 1 体は作り直さずに読む。作り直すと直した値が文書の値へ戻る
        const NS::SceneActor* actor = m_actorFileDraft.get();
        if (edit.actorId != NS::k_NoActorId)
        {
            actor = m_scene->Actors().FindByActorId(edit.actorId);
        }
        if (actor == nullptr)
        {
            continue;
        }
        const NS::Object* part = edit.row->get(actor);
        if (part == nullptr)
        {
            continue;
        }
        const NS::FieldDesc* field = NS::FindField(part->GetReflection(), edit.fieldName);
        if (field == nullptr)
        {
            continue;
        }
        // 共有の項目は派生を指すアクターでも元へ書く。派生へ書くと、派生を読む時に書けない項目として断られる
        // 選んだ .actor の 1 体はシーンに居ないので、グローバルの項目はその .actor 自身へ書く
        NS::FieldDestination destination = NS::FieldDestinationOf(actor, *field);
        if (destination == NS::FieldDestination::Scene)
        {
            destination = NS::FieldDestination::DerivedActorFile;
        }
        const std::string name = NS::ActorFileToWrite(actor, destination);
        const nlohmann::json* current = library.Find(name);
        if (current == nullptr)
        {
            NS_LOG_WARN(App, ".actor {} が読まれていないので、欄 {} を書かない", name, edit.fieldName);
            continue;
        }
        const nlohmann::json before = *current;
        nlohmann::json after = before;
        NS::ActorJsonClassObjs(after)[std::string{edit.row->key}][edit.fieldName] = FieldJson(*part, edit.fieldName);
        if (after == before)
        {
            continue;
        }
        // .actor は配置の姿でないので、プレイ中に直した物も積む。編集へ戻った後に戻せる
        m_editor.Undo().Push(std::make_unique<ActorFileCommand>(*m_scene, name, before, after), m_applier);
    }
}

void LevelEditorController::WriteActorFileHitReactions(std::string_view actorFileName,
                                                       const std::vector<NS::HitReactionSet>& sets)
{
    if (m_scene == nullptr)
    {
        return;
    }
    const std::string name{actorFileName};
    const nlohmann::json* current = NS::ActorFileLibrary::Get().Find(name);
    if (current == nullptr)
    {
        NS_LOG_WARN(App, ".actor {} が読まれていないので、束を書かない", name);
        return;
    }
    nlohmann::json before = *current;
    nlohmann::json after = before;
    NS::WriteHitReactionSets(after, sets);
    if (after == before)
    {
        return;
    }
    // .actor は配置の姿でないので、プレイ中に直した物も積む。編集へ戻った後に戻せる
    m_editor.Undo().Push(std::make_unique<ActorFileCommand>(*m_scene, name, before, after), m_applier);
}

bool LevelEditorController::CreateDerivedActorFile(std::string_view baseName, std::string_view name)
{
    const std::optional<nlohmann::json> document = NewDerivedActorFile(baseName, name);
    if (!document.has_value())
    {
        return false;
    }
    return AddActorFile(name, *document);
}

bool LevelEditorController::DeriveActorFileFromActor(NS::uint32 actorId, std::string_view name)
{
    if (!IsSceneEditable())
    {
        NS_LOG_WARN(App, "編集モードでないので、アクターから派生 {} を作らない", name);
        return false;
    }
    const NS::SceneActor* actor = m_scene->Actors().FindByActorId(actorId);
    if (actor == nullptr)
    {
        NS_LOG_WARN(App, "アクター {} が居ないので派生 {} を作らない", actorId, name);
        return false;
    }
    std::optional<nlohmann::json> document = NewDerivedActorFile(actor->ActorFileName(), name);
    std::optional<nlohmann::json> before = m_applier.CaptureActor(actorId);
    if (!document.has_value() || !before.has_value())
    {
        return false;
    }
    // 保存の JSON はシーンの上書きと参照の欄だけを持つ。派生に書ける項目を移し、残りはシーンに残す
    nlohmann::json after = *before;
    NS::ForEachReflectedClassObj(actor, [&](const NS::ClassObjInfo& row, const NS::Object* part) {
        nlohmann::json* fields = NS::ClassObjFields(after, row.key);
        if (fields == nullptr)
        {
            return;
        }
        for (nlohmann::json::iterator entry = fields->begin(); entry != fields->end();)
        {
            const NS::FieldDesc* field = NS::FindField(part->GetReflection(), entry.key());
            if (field == nullptr || !NS::CanWriteField(NS::FieldDestination::DerivedActorFile, *field))
            {
                ++entry;
                continue;
            }
            NS::ActorJsonClassObjs(*document)[std::string{row.key}][entry.key()] = entry.value();
            entry = fields->erase(entry);
        }
    });
    if (!AddActorFile(name, *document))
    {
        return false;
    }
    NS::SetActorJsonFile(after, name);
    m_editor.Undo().Push(std::make_unique<NS::Editor::ActorSnapshotCommand>(actorId, std::move(before), std::move(after)),
                         m_applier);
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
    return true;
}

void LevelEditorController::FinishFieldWrite(NS::SceneActor* actor, NS::FieldType writtenType)
{
    NS_ASSERT(App, actor != nullptr, "FinishFieldWrite: アクターが null");
    // 部品は ActorRef の指す相手を取得して持つので、欄を書き換えた時に取得し直す
    if (writtenType == NS::FieldType::ActorRef)
    {
        actor->ResolveActorRefs();
        return;
    }
    // 素材の欄は道を書くだけで、描くメッシュの差し替え・描くマテリアル・メッシュの当たりの形は読み直しで作る
    // 素材でない欄で読み直すと、ドラッグの間じゅう骨の差し直しとメッシュの当たりの作り直しが毎回走る
    if (writtenType != NS::FieldType::Asset)
    {
        return;
    }
    actor->ReloadAssets();
}

void LevelEditorController::PlaceItem(const NS::Editor::PlacementItem& item)
{
    nlohmann::json object = item.prototype;
    NS::Editor::SetActorJsonPosition(object, m_editorCamera.Center());
    CreateActorAndSelect(std::move(object));
}

void LevelEditorController::AddMeshParts(std::string_view meshPath)
{
    // 参照は ContentRoot 相対で持つ。build 時にこの文字列から実体を引く
    const std::optional<std::string> meshRef =
        NS::OS::FileSystem::RelativeUnder(NS::OS::FileSystem::ContentRoot(), meshPath);
    if (!meshRef)
    {
        NS_LOG_ERROR(App, "メッシュ {} が ContentRoot の外にあるので置かない", meshPath);
        return;
    }

    nlohmann::json object = NS::Editor::MakeMeshPartsPrototype(*meshRef);
    NS::Editor::SetActorJsonPosition(object, m_editorCamera.Center());
    NS::SetActorJsonName(object, NS::OS::FileSystem::Stem(meshPath));
    CreateActorAndSelect(std::move(object));
}

void LevelEditorController::CreateActorAndSelect(nlohmann::json object)
{
    std::vector<nlohmann::json> objects;
    objects.push_back(std::move(object));
    const std::vector<NS::uint32> created = m_editor.CreateActors(std::move(objects));
    if (created.empty())
    {
        return;
    }

    // 組み直し後の新規を選択する。選択候補も貼り直す
    m_selectedActorId = created.front();
    m_selectionIds.assign(1, created.front());
    ClearAssetSelection();
    SetGizmoToolActive(true);
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::RenameActor(NS::uint32 id, std::string_view name)
{
    if (!IsSceneEditable() || id == NS::k_NoActorId)
    {
        return;
    }
    NS::SceneActor* object = m_scene->Actors().FindByActorId(id);
    std::optional<nlohmann::json> before = m_applier.CaptureActor(id);
    if (object == nullptr || !before)
    {
        return;
    }

    // 実体の名前を直接変える。他の配置物と重なれば番号が付く。参照は id で持つので切れない
    const std::string previous = object->Name();
    m_scene->Actors().RenameActor(object, name);
    if (object->Name() == previous)
    {
        return; // 同じ名前で履歴を汚さない
    }

    std::optional<nlohmann::json> after = m_applier.CaptureActor(id);
    // 実体は既に after なので Do を呼ばず履歴だけ積む
    m_editor.Undo().Record(
        std::make_unique<NS::Editor::ActorSnapshotCommand>(id, std::move(before), std::move(after)));

    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::DuplicateSelectedActor()
{
    if (m_selectionIds.empty())
    {
        return;
    }

    const NS::SceneActor* player = FindPlayer(m_scene->Actors());

    // 選択物の忠実コピーを作る。新しい id と、写した物どうしの参照の付け替えは CreateActors が行う
    std::vector<nlohmann::json> copies;
    copies.reserve(m_selectionIds.size());
    for (const NS::uint32 id : m_selectionIds)
    {
        // プレイヤーは必ず 1 体。複製で 2 体目を作らせない
        if (player != nullptr && id == player->Id())
        {
            continue;
        }
        std::optional<nlohmann::json> source = m_applier.CaptureActor(id);
        if (!source)
        {
            continue;
        }
        copies.push_back(std::move(*source));
    }

    std::vector<NS::uint32> created = m_editor.CreateActors(std::move(copies));
    if (created.empty())
    {
        return;
    }

    const NS::uint32 primary = created.back();
    SelectActors(std::move(created), primary);
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::DeleteSelectedActor()
{
    if (!IsSceneEditable() || m_selectionIds.empty())
    {
        return;
    }

    const NS::SceneActor* player = FindPlayer(m_scene->Actors());
    if (player != nullptr &&
        std::find(m_selectionIds.begin(), m_selectionIds.end(), player->Id()) != m_selectionIds.end())
    {
        NS_LOG_WARN(App, "プレイヤーを含むため削除しなかった");
        return;
    }

    std::vector<std::unique_ptr<NS::Editor::ICommand>> commands;
    commands.reserve(m_selectionIds.size());
    for (const NS::uint32 victim : m_selectionIds)
    {
        std::optional<nlohmann::json> before = m_applier.CaptureActor(victim);
        if (!before)
        {
            continue;
        }
        commands.push_back(
            std::make_unique<NS::Editor::ActorSnapshotCommand>(victim, std::move(before), std::nullopt));
    }
    if (commands.empty())
    {
        return;
    }

    m_editor.Undo().Push(MakeUndoUnit(std::move(commands)), m_applier);

    SelectActorById(NS::k_NoActorId);
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::FocusSelectedInView() noexcept
{
    if (m_selectionIds.empty())
    {
        return;
    }

    // 選んだ分を全部収める。中心は重心、距離は一番外側までの広がりで決める
    NS::Vector3 sum{0.0f, 0.0f, 0.0f};
    std::vector<NS::Vector3> centers;
    float extent = 0.0f;
    centers.reserve(m_selectionIds.size());
    for (const NS::uint32 id : m_selectionIds)
    {
        NS::SceneActor* object = m_scene->Actors().FindByActorId(id);
        if (object == nullptr)
        {
            continue;
        }
        const NS::Matrix world = object->Root().WorldMatrix();
        const NS::Vector3 center{world._41, world._42, world._43};
        centers.push_back(center);
        sum += center;

        const NS::Vector3 scale = NS::DecomposeAffine(world).scale;
        // 1m 立方の cube mesh の半サイズ 0.5 に拡縮を掛ける
        const float half = std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)}) * 0.5f;
        extent = std::max(extent, half);
    }
    if (centers.empty())
    {
        return;
    }

    const NS::Vector3 center = sum / static_cast<float>(centers.size());
    for (const NS::Vector3& each : centers)
    {
        extent = std::max(extent, (each - center).Length());
    }

    const float distance = std::max(extent * 4.0f, NS::Editor::EditorCamera::k_MinDistance);

    m_editorCamera.SetCenter(center);
    m_editorCamera.SetDistance(distance);
}

void LevelEditorController::CaptureDragFollowers() noexcept
{
    m_dragFollowers.clear();
    if (m_selectionIds.size() < 2)
    {
        return;
    }

    NS::SceneActor* primary = m_scene->Actors().FindByActorId(m_selectedActorId);
    if (primary == nullptr)
    {
        return;
    }
    m_dragPrimaryWorld = primary->Root().WorldMatrix();

    for (const NS::uint32 id : m_selectionIds)
    {
        if (id == m_selectedActorId)
        {
            continue;
        }
        NS::SceneActor* object = m_scene->Actors().FindByActorId(id);
        if (object == nullptr)
        {
            continue;
        }

        m_dragFollowers.push_back(DragFollower{id, object->Root().WorldMatrix()});
    }
}

void LevelEditorController::ApplyDragToFollowers() noexcept
{
    if (m_dragFollowers.empty())
    {
        return;
    }
    NS::SceneActor* primary = m_scene->Actors().FindByActorId(m_selectedActorId);
    if (primary == nullptr)
    {
        return;
    }

    // 主対象が動いた分をワールド空間の差分として取り、残りへ同じだけ効かせる
    const NS::Matrix delta = m_dragPrimaryWorld.Invert() * primary->Root().WorldMatrix();
    for (const DragFollower& follower : m_dragFollowers)
    {
        NS::SceneActor* object = m_scene->Actors().FindByActorId(follower.id);
        if (object == nullptr)
        {
            continue;
        }

        NS::Matrix local = follower.world * delta;

        NS::Vector3 scale{};
        NS::Quaternion rotation{};
        NS::Vector3 position{};
        if (!local.Decompose(scale, rotation, position))
        {
            continue;
        }
        object->Root().SetPosition(position);
        object->Root().SetRotation(rotation);
        object->Root().SetScale(scale);
    }
}

void LevelEditorController::BeginTransformEdit() noexcept
{
    if (m_transformEditing)
    {
        return;
    }
    m_editBaselines.clear();
    // 選択している分をまとめて控える。動かなかった物は確定時に落ちる
    for (const NS::uint32 id : m_selectionIds)
    {
        std::optional<nlohmann::json> baseline = m_applier.CaptureActor(id);
        if (!baseline)
        {
            continue;
        }
        // 控えが取れたので、m_applier と同じシーンにこのアクターが居る
        const NS::SceneActor* actor = m_scene->Actors().FindByActorId(id);
        if (actor != nullptr)
        {
            m_editBaselines.push_back(TransformEditBaseline{id, std::move(*baseline), AllFieldValues(*actor)});
        }
    }
    if (m_editBaselines.empty())
    {
        return;
    }
    m_transformEditing = true;
}

void LevelEditorController::CommitTransformEdit() noexcept
{
    if (!m_transformEditing)
    {
        return;
    }
    m_transformEditing = false;

    // ドラッグは実体の Root を既に動かしている。after は実体の忠実な写し。baseline と同じなら履歴に積まない
    std::vector<std::unique_ptr<NS::Editor::ICommand>> commands;
    for (TransformEditBaseline& entry : m_editBaselines)
    {
        const NS::uint32 id = entry.id;
        nlohmann::json& baseline = entry.snapshot;
        // 保存は上書きの一覧の項目だけを書くので、動かした姿勢の項目を一覧に足してから後の姿を控える
        if (NS::SceneActor* actor = m_scene->Actors().FindByActorId(id))
        {
            AddChangedSceneOverrides(*actor, entry.fields);
        }
        std::optional<nlohmann::json> after = m_applier.CaptureActor(id);
        if (!after || *after == baseline)
        {
            continue;
        }
        commands.push_back(std::make_unique<NS::Editor::ActorSnapshotCommand>(id, baseline, std::move(*after)));
    }
    m_editBaselines.clear();
    // プレイ中の控えはプレイの姿。編集へ戻った後に戻すと、プレイの位置を編集の物へ書く
    if (!IsSceneEditable() || commands.empty())
    {
        return;
    }

    // 実体は既に after なので Do を呼ばず履歴だけ積む。取り消し で baseline へ、やり直し で after へ戻す
    m_editor.Undo().Record(MakeUndoUnit(std::move(commands)));
}

void LevelEditorController::BeginClassObjEdit() noexcept
{
    if (m_classObjEditing)
    {
        return;
    }
    std::optional<nlohmann::json> baseline = m_applier.CaptureActor(m_selectedActorId);
    if (!baseline)
    {
        return;
    }
    m_classObjEditBaseline = std::move(*baseline);
    m_classObjEditBaselineId = m_selectedActorId;
    m_classObjEditing = true;
}

void LevelEditorController::CommitClassObjEdit() noexcept
{
    CommitActorFileEdits();
    if (!m_classObjEditing)
    {
        return;
    }
    m_classObjEditing = false;

    // リフレクション編集は実体の部品へ直接入っている。after は実体の忠実な写しで、baseline と同じなら積まない
    // プレイ中の控えはプレイの姿なので積まない
    if (!IsSceneEditable())
    {
        return;
    }
    std::optional<nlohmann::json> after = m_applier.CaptureActor(m_classObjEditBaselineId);
    if (!after || *after == m_classObjEditBaseline)
    {
        return;
    }

    // 実体は既に after なので Do を呼ばず履歴だけ積む
    m_editor.Undo().Record(std::make_unique<NS::Editor::ActorSnapshotCommand>(
        m_classObjEditBaselineId, m_classObjEditBaseline, std::move(*after)));
}

bool LevelEditorController::ApplyMaterialToSelected(std::string_view matPath)
{
    NS::Application* app = NS::Application::Get();
    if (!IsSceneEditable() || !m_gizmoToolActive || app == nullptr)
    {
        return false;
    }

    if (m_selectedActorId == NS::k_NoActorId)
    {
        return false;
    }
    NS::SceneActor* go = SelectedActor();
    if (go == nullptr)
    {
        return false;
    }

    NS::Model* mesh = go->ModelClassObj();
    if (mesh == nullptr)
    {
        return false;
    }

    // .mat パスは ContentRoot 相対で持つ。材質の正データは欄の名前なので、名前を書いて欄から読み直す
    const std::optional<std::string> stored =
        NS::OS::FileSystem::RelativeUnder(NS::OS::FileSystem::ContentRoot(), matPath);
    if (!stored)
    {
        NS_LOG_ERROR(App, "マテリアル {} が ContentRoot の外にあるので付けない", matPath);
        return false;
    }
    const NS::Material* material = app->Assets().LoadMaterial(*stored);
    if (material->IsEmpty())
    {
        return false;
    }

    // 差替前を忠実に写す。matRef を実体へ書き込み、差替後との差分を 取り消し 履歴へ積む
    std::optional<nlohmann::json> before = m_applier.CaptureActor(m_selectedActorId);

    mesh->SetMaterialRef(*stored);
    FinishFieldWrite(go, NS::FieldType::Asset);

    std::optional<nlohmann::json> after = m_applier.CaptureActor(m_selectedActorId);
    if (before && after && !(*before == *after))
    {
        m_editor.Undo().Record(std::make_unique<NS::Editor::ActorSnapshotCommand>(
            m_selectedActorId, std::move(before), std::move(after)));
    }
    return true;
}

namespace
{
    [[nodiscard]] NS::Object* LoadSelectableAsset(std::string_view path)
    {
        if (NS::Editor::IsMaterialAssetPath(path))
        {
            return NS::AssetManager::Get().LoadMaterial(path);
        }
        if (NS::Editor::IsModelAssetPath(path))
        {
            return NS::AssetManager::Get().LoadMesh(path);
        }
        if (NS::Editor::IsTextureAssetPath(path))
        {
            const std::optional<std::string> resolved = NS::ResolveContentPath(std::string{path});
            if (resolved)
            {
                return NS::AssetManager::Get().LoadTexture(*resolved);
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool IsReadable(const NS::Object& asset)
    {
        if (const NS::Material* material = NS::Cast<NS::Material>(&asset))
        {
            return !material->IsEmpty();
        }
        if (const NS::Mesh* mesh = NS::Cast<NS::Mesh>(&asset))
        {
            return !mesh->IsEmpty();
        }
        if (const NS::Texture* texture = NS::Cast<NS::Texture>(&asset))
        {
            return texture->D3D()->IsValid() && !texture->D3D()->IsUsingFallback();
        }
        return false;
    }
} // namespace

void LevelEditorController::SelectAsset(std::string_view assetPath)
{
    std::optional<std::string> stored = NS::OS::FileSystem::RelativeUnder(NS::OS::FileSystem::ContentRoot(), assetPath);
    if (!stored)
    {
        NS_LOG_ERROR(App, "素材 {} が ContentRoot の外にあるので選ばない", assetPath);
        return;
    }

    ClearAssetSelection();
    SelectActorById(NS::k_NoActorId);

    // .actor は名前で引く。読めなかった .actor は持っていないので選ばない
    if (NS::Editor::IsActorFileAssetPath(*stored))
    {
        if (NS::ActorFileLibrary::Get().Find(NS::OS::FileSystem::Stem(*stored)) == nullptr)
        {
            NS_LOG_WARN(App, ".actor {} を読めていないので選ばない", *stored);
            return;
        }
        m_selectedAssetPath = std::move(*stored);
        return;
    }
    const NS::Object* asset = LoadSelectableAsset(*stored);
    if (asset == nullptr || !IsReadable(*asset))
    {
        NS_LOG_WARN(App, "素材 {} を読めないので選ばない", *stored);
        return;
    }
    m_selectedAssetPath = std::move(*stored);
}

bool LevelEditorController::IsAssetSelected(std::string_view assetPath) const
{
    if (m_selectedAssetPath.empty())
    {
        return false;
    }
    return NS::OS::FileSystem::RelativeUnder(NS::OS::FileSystem::ContentRoot(), assetPath) == m_selectedAssetPath;
}

NS::Object* LevelEditorController::SelectedAsset()
{
    if (m_selectedAssetPath.empty())
    {
        return nullptr;
    }
    if (NS::Editor::IsActorFileAssetPath(m_selectedAssetPath))
    {
        return SelectedActorFile();
    }
    return LoadSelectableAsset(m_selectedAssetPath);
}

void LevelEditorController::ClearAssetSelection()
{
    CommitAssetEdit();
    m_selectedAssetPath.clear();
    m_actorFileDraft.reset();
}

NS::SceneActor* LevelEditorController::SelectedActorFile()
{
    const std::string name = NS::OS::FileSystem::Stem(m_selectedAssetPath);
    const nlohmann::json* document = NS::ActorFileLibrary::Get().Find(name);
    if (document == nullptr)
    {
        m_actorFileDraft.reset();
        return nullptr;
    }
    if (m_actorFileDraft == nullptr || m_actorFileDraft->ActorFileName() != name || m_actorFileDraftSource != *document)
    {
        m_actorFileDraftSource = *document;
        const NS::ActorInitInfo info{.actorFile = name};
        m_actorFileDraft = NS::CreateActorOfClass(NS::ActorFileClass(m_actorFileDraftSource), info);
    }
    return m_actorFileDraft.get();
}

void LevelEditorController::MarkAssetEdited() noexcept
{
    m_assetEdited = true;
}

void LevelEditorController::CommitAssetEdit()
{
    if (!m_assetEdited)
    {
        return;
    }
    m_assetEdited = false;

    if (NS::Editor::IsActorFileAssetPath(m_selectedAssetPath))
    {
        CommitActorFileEdits();
        // 直した 1 体は書いた文書と同じ値だが、作った時の文書と比べると取り消しで元の文書へ戻った事を見落とす。
        // 次に引いた時に今の文書から作り直させる
        m_actorFileDraftSource = nlohmann::json{};
        return;
    }
    NS::Material* material = NS::Cast<NS::Material>(SelectedAsset());
    if (material == nullptr)
    {
        return;
    }
    NS::AssetManager& assets = NS::AssetManager::Get();
    // 共有のマテリアルは書く先が無いので、絵も作り直さない。Inspector は見るだけにしている
    if (!assets.MakeMaterialFileJson(*material))
    {
        return;
    }
    // 作り直せなくても値は .mat に残す。作り直しの失敗は Rebuild が報告する
    (void)assets.RebuildMaterial(*material);
    const std::optional<std::string> path = NS::ResolveContentPath(m_selectedAssetPath);
    if (!path)
    {
        return;
    }
    (void)NS::Editor::WriteMaterialFile(assets, *material, *path);
}

namespace NS
{
    namespace Editor
    {
        bool WriteMaterialFile(const AssetManager& assets, const Material& material, std::string_view filePath)
        {
            const std::optional<std::string> text = assets.MakeMaterialFileJson(material);
            if (!text)
            {
                NS_LOG_WARN(App, "マテリアル {} は .mat から読んだ物でないか、書けない値を持つので書かない", material.Path());
                return false;
            }
            const std::span<const std::byte> bytes = std::as_bytes(std::span<const char>{text->data(), text->size()});
            if (!OS::FileSystem::ReplaceAllBytes(filePath, bytes))
            {
                NS_LOG_WARN(App, "マテリアル {} を {} に書けないので前の .mat を残す", material.Path(), filePath);
                return false;
            }
            return true;
        }
    } // namespace Editor
} // namespace NS
