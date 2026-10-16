#include "Editor/HitPreview.h"
#include "NSlib/Core/Types.h"

#include "Editor/EditorObjects.h"
#include "Editor/HitTimelineEdit.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/PropActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/CameraModifier.h"
#include "NSlib/CoreObject/SubObjects/HitSensor.h"
#include "NSlib/CoreObject/SubObjects/PlayerInput.h"
#include "NSlib/CoreObject/SubObjects/ThirdPersonFollow.h"
#include "NSlib/CoreObject/SubObjects/Transform.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/Windows/Input.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <utility>

namespace NS
{
    namespace Editor
    {
        namespace
        {
            // 置き直しを詰める回数の上限。面の位置と検知のフレームが合えば途中で止める
            constexpr int k_MaxPlacementRuns = 6;
            // 検知のフレームがこの数より狙いからずれた時だけ、突進の向きの距離を詰め直す
            // 高さを変えると触れるフレームが 1 つ前後するので、1 フレームのずれで距離を動かすと高さの詰めと行き来する
            constexpr int k_LeadTolerance = 1;
            // 面の上の位置がこの差に収まれば詰め終える
            constexpr float k_FaceTolerance = 0.02f;
            // 下見の追従カメラを置く、自機の後ろの距離と高さ (m)。ゲームで狙っている時の見え方に近い所
            constexpr float k_CameraBehindDistance = 5.0f;
            constexpr float k_CameraHeight = 2.0f;
            // 1 回目の置き直しで、玉の表面から面までに空ける距離 (m)。2 回目からは 1 歩の長さを測って決め直す
            constexpr float k_FirstLeadDistance = 2.0f;

            [[nodiscard]] nlohmann::ordered_json Vec3Json(const Vector3& v)
            {
                return nlohmann::ordered_json{v.x, v.y, v.z};
            }

            // 写しから場面を組む。描かない下見では資産と描き手は空
            std::unique_ptr<Scene> MakeScene(const nlohmann::json& scene, const HitPreviewWorld& world)
            {
                std::unique_ptr<Scene> made = std::make_unique<Scene>();
                made->SetRenderer(world.renderer);
                made->ResetFromJson(scene);
                return made;
            }

            // 写しの自機の根を rootPosition へ置き直した文書。自機が居なければ写しのまま
            nlohmann::json PlacePlayer(const nlohmann::json& snapshot, const Vector3& rootPosition)
            {
                nlohmann::json placed = snapshot;
                const std::size_t index = FindPlayerObjectIndex(placed);
                if (index != k_NoObjectIndex)
                {
                    SetObjectPosition(SceneJsonObjects(placed)[index], rootPosition);
                }
                return placed;
            }

            // 置き直した自機の入力を止めて突進を頼んだ場面。自機が居なければ outPlayer は nullptr
            std::unique_ptr<Scene> StartRun(const nlohmann::json& snapshot,
                                                const HitPreviewResult& placement,
                                                const HitPreviewWorld& world,
                                                PlayerActor*& outPlayer)
            {
                std::unique_ptr<Scene> scene = MakeScene(PlacePlayer(snapshot, placement.playerStart), world);
                outPlayer = FindPlayer(scene->Objects());
                if (outPlayer != nullptr)
                {
                    // 追従カメラは置いた時の向きのままだと、突進の横や前から映る。ゲームでは狙う向きの後ろから見ているので、
                    // 自機の後ろ上へ置き直す。当たりの動きはカメラを読まないので、記録は変わらない
                    const Vector3 behind = placement.playerStart - placement.direction * k_CameraBehindDistance +
                                               Vector3{0.0f, k_CameraHeight, 0.0f};
                    for (SceneActor* object : scene->Objects())
                    {
                        if (GL::Level::FollowCameraActor* camera = Cast<GL::Level::FollowCameraActor>(object))
                        {
                            camera->Vcam().SetInitialPoseFromCameraPosition(behind);
                        }
                    }
                    outPlayer->Input().SetLocked(true);
                    outPlayer->RequestBodySlam(placement.desc.charge01, placement.direction);
                }
                return scene;
            }

            // 置き直しを 1 回走らせ、frames・impact・detectionIndex を埋める
            void RunOnce(const nlohmann::json& snapshot, const HitPreviewWorld& world, HitPreviewResult& result)
            {
                result.frames.clear();
                result.hit = false;
                result.error.clear();
                result.impact = GL::Level::ImpactRecord{};
                result.detectionIndex = -1;
                // 手元の機器を読まず、振動を手元のパッドへ送らない。組む前から入れる
                // (配置物が組む時に機器を読んでも中立)
                const OS::ScopedNeutralInput neutral;
                OS::Gamepad& pad = OS::Input::Get().Gamepad();
                PlayerActor* player = nullptr;
                std::unique_ptr<Scene> scene = StartRun(snapshot, result, world, player);
                if (player == nullptr)
                {
                    result.error = "場面に自機が居ない";
                    return;
                }
                bool sawRebound = false;
                int afterRebound = -1;
                int lastEventClock = 0;
                for (int step = 0; step < result.desc.maxFrames; ++step)
                {
                    // ゲームでは書かれなかったフレームの振動を Input::Update が 0 に戻す。中立のパッドは Update
                    // を受けないので ここで戻す。送った速さが 0 のままなので機器へは書かない
                    pad.StopVibration();
                    scene->OnUpdate();
                    HitPreviewFrame frame;
                    frame.pad = pad.Vibration();
                    frame.playerPosition = player->Root().Position();
                    frame.shape = player->Resolver().Answer().shapeFactors;
                    frame.hitStopping = player->Resolver().Answer().hitStopping;
                    frame.awaitingRebound = player->Resolver().Answer().awaitingRebound;
                    frame.rebounding = player->IsRebounding();
                    frame.startedRows = player->Resolver().Answer().rowsStarted;
                    frame.worldSpeed = scene->WorldSpeed();
                    if (const CameraManager* cameras = scene->GetCameraManager())
                    {
                        frame.trauma = cameras->Trauma();
                        frame.shakeOffset = cameras->ShakeOffset();
                        if (const CameraSinkModifier* sink = cameras->FindModifier<CameraSinkModifier>())
                        {
                            frame.sinkPixels = sink->Pixels();
                        }
                        if (const CameraTraumaModifier* shake = cameras->FindModifier<CameraTraumaModifier>())
                        {
                            frame.shakeAngles = shake->Angles();
                        }
                    }
                    result.frames.push_back(std::move(frame));
                    if (result.detectionIndex < 0 && player->Resolver().Answer().lastImpact.sequence != 0)
                    {
                        result.detectionIndex = step;
                        result.impact = player->Resolver().Answer().lastImpact;
                        if (const GL::Level::HitTimeline* timeline =
                                GL::Level::HitTimelineLibrary::Get().Find(result.impact.timeline))
                        {
                            lastEventClock = HitTimelineFrameRange(*timeline, nullptr).last;
                        }
                    }
                    if (result.frames.back().rebounding)
                    {
                        sawRebound = true;
                    }
                    else if (sawRebound && afterRebound < 0)
                    {
                        afterRebound = 0;
                    }
                    if (afterRebound >= 0)
                    {
                        if (afterRebound >= result.desc.framesAfterRebound &&
                            step - result.detectionIndex >= lastEventClock)
                        {
                            break;
                        }
                        ++afterRebound;
                    }
                }
                if (result.detectionIndex < 0)
                {
                    result.error = std::format("{} フレームの間に何にも当たらなかった", result.desc.maxFrames);
                    return;
                }
                if (result.impact.targetId != result.desc.targetId)
                {
                    result.error = std::format(
                        "選んだ相手 (id {}) より先に id {} に当たった", result.desc.targetId, result.impact.targetId);
                    return;
                }
                result.hit = true;
            }

            // 突進が当たる体と面を持つ配置物か。下見の相手になれる
            bool IsHitTarget(SceneActor& object)
            {
                const HitSensor* bodySensor = object.BodySensorSubObj();
                return bodySensor != nullptr && bodySensor->IsValid() && Cast<GL::Level::PropActor>(&object) != nullptr;
            }

            // 自機の根に一番近い、下見の相手になれる配置物の id。居なければ 0
            uint32 FindNearestTarget(Scene& scene, const PlayerActor& player)
            {
                const Vector3 from = player.Root().Position();
                uint32 nearest = 0;
                float nearestDistance = 0.0f;
                for (SceneActor* object : scene.Objects())
                {
                    if (object == &player || !object->IsAppeared() || !IsHitTarget(*object))
                    {
                        continue;
                    }
                    const float distance = Vector3::DistanceSquared(object->Root().Position(), from);
                    if (nearest == 0 || distance < nearestDistance)
                    {
                        nearest = object->Id();
                        nearestDistance = distance;
                    }
                }
                return nearest;
            }

            // 検知までの 1 歩の水平の長さを、突進を出してから検知までの自機の進みで測る。測れなければ 0
            float MeasureStepLength(const HitPreviewResult& result)
            {
                if (result.detectionIndex < 2)
                {
                    return 0.0f;
                }
                const Vector3 first = result.frames.front().playerPosition;
                const Vector3 last =
                    result.frames[static_cast<std::size_t>(result.detectionIndex - 1)].playerPosition;
                const float dx = last.x - first.x;
                const float dz = last.z - first.z;
                return std::sqrt(dx * dx + dz * dz) / static_cast<float>(result.detectionIndex - 1);
            }
        } // namespace

        HitPreviewResult RunHitPreview(const nlohmann::json& snapshot,
                                       const HitPreviewDesc& desc,
                                       const HitPreviewWorld& world)
        {
            HitPreviewResult result;
            result.desc = desc;
            // 写しの場面を組む・進める・壊す間は、手元の機器を読まず振動を送らない
            const OS::ScopedNeutralInput neutral;
            // 置き直しの前の自機と相手を、写しをそのまま組んだ場面から読む
            std::unique_ptr<Scene> original = MakeScene(snapshot, world);
            PlayerActor* player = FindPlayer(original->Objects());
            if (player == nullptr)
            {
                result.error = "場面に自機が居ない";
                return result;
            }
            if (result.desc.targetId == 0)
            {
                result.desc.targetId = FindNearestTarget(*original, *player);
                if (result.desc.targetId == 0)
                {
                    result.error = "突進が当たる体と面を持つ配置物が場面に無い";
                    return result;
                }
            }
            SceneActor* target = original->Objects().FindByObjectId(result.desc.targetId);
            if (target == nullptr)
            {
                result.error = std::format("id {} の配置物が場面に無い", result.desc.targetId);
                return result;
            }
            const HitSensor* bodySensor = target->BodySensorSubObj();
            if (bodySensor == nullptr || !bodySensor->IsValid())
            {
                result.error = std::format("id {} の配置物に突進が当たる体が無い", result.desc.targetId);
                return result;
            }
            const SensorVolume body = bodySensor->WorldVolume();
            const Vector3 root = player->Root().Position();
            const Sphere ball = player->SlamBallAt(root);
            const Vector3 ballOffset = ball.center - root;
            // 向きはエディタが面を描く時と同じく、自機の玉から相手の体の中心へ
            GL::Level::HitFaceFrame face;
            if (!GL::Level::MakeHitFaceFrame(body, body.Center() - ball.center, ball.radius, face))
            {
                result.error = std::format("id {} の配置物の面を置けない (真上か真下に居るか、体の形が分からない)",
                                           result.desc.targetId);
                return result;
            }
            result.direction = -face.normal;

            // 玉の中心が面の点から自機の半径だけ手前で触れるので、そこから突進の向きの逆へ空けて置く
            const Vector3 contactCenter =
                GL::Level::HitFacePoint(face, desc.faceU, desc.faceV) + face.normal * ball.radius;
            Vector3 ballStart = contactCenter + face.normal * k_FirstLeadDistance;
            // 選んだ位置に一番近く当てた回。床より下へ置き直して当たらなくなった時はこれを返す
            HitPreviewResult best;
            float bestMiss = 0.0f;
            for (int run = 0; run < k_MaxPlacementRuns; ++run)
            {
                result.playerStart = ballStart - ballOffset;
                RunOnce(snapshot, world, result);
                if (!result.hit)
                {
                    if (best.hit)
                    {
                        return best;
                    }
                    return result;
                }
                const float du = desc.faceU - result.impact.faceU;
                const float dv = desc.faceV - result.impact.faceV;
                const int lateFrames = desc.leadFrames - result.detectionIndex;
                const bool leadSettled = std::abs(lateFrames) <= k_LeadTolerance;
                const float miss = std::abs(du) + std::abs(dv);
                if (!best.hit || miss < bestMiss)
                {
                    best = result;
                    bestMiss = miss;
                }
                if (std::abs(du) <= k_FaceTolerance && std::abs(dv) <= k_FaceTolerance && leadSettled)
                {
                    return result;
                }
                ballStart += face.right * (du * face.reachU) + face.up * (dv * face.reachV);
                if (!leadSettled)
                {
                    ballStart += face.normal * (static_cast<float>(lateFrames) * MeasureStepLength(result));
                }
            }
            return best;
        }

        std::unique_ptr<Scene> MakeHitPreviewScene(const nlohmann::json& snapshot,
                                                       const HitPreviewResult& result,
                                                       const HitPreviewWorld& world)
        {
            if (!result.hit)
            {
                return nullptr;
            }
            const OS::ScopedNeutralInput neutral;
            PlayerActor* player = nullptr;
            return StartRun(snapshot, result, world, player);
        }

        std::string HitPreviewHitLine(const HitPreviewResult& result)
        {
            if (!result.hit)
            {
                return std::string{};
            }
            // 鍵と並びは当たりの記録 hits.jsonl の 1 行に合わせる
            using Json = nlohmann::ordered_json;
            const GL::Level::ImpactRecord& impact = result.impact;
            const Vector3& launch = impact.launchVelocity;
            Json root;
            root["f"] = result.detectionIndex;
            root["victim"] = impact.targetId;
            root["power"] = impact.power;
            root["charge"] = impact.charge01;
            root["centerCoef"] = impact.positionFactor;
            root["offset"] = impact.offset01;
            root["tier"] = static_cast<int>(impact.tier);
            root["centerHit"] = impact.centerHit;
            root["broke"] = impact.broke;
            root["hitstop"] = impact.hitStopSteps;
            root["launchSpeed"] = std::sqrt(launch.x * launch.x + launch.y * launch.y + launch.z * launch.z);
            root["launchVel"] = Vec3Json(launch);
            root["launchDist"] = impact.launchDistance;
            root["launchApex"] = impact.launchApexHeight;
            root["selfKnockback"] = Vec3Json(impact.selfVelocity);
            root["selfApex"] = impact.reboundApexHeight;
            root["dir"] = Vec3Json(impact.impactDir);
            root["targetPos"] = Vec3Json(impact.targetPos);
            root["cameraShake"] = impact.cameraShake;
            root["flashStart"] = impact.flashStart;
            root["zoomStart"] = impact.zoomStart;
            root["rollStart"] = impact.rollStart;
            root["padStart"] = Json{impact.padStart.left, impact.padStart.right};
            return root.dump(-1, ' ', false, Json::error_handler_t::replace);
        }
    } // namespace Editor
} // namespace NS
