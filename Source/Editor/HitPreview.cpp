#include "Editor/HitPreview.h"

#include "Editor/EditorObjects.h"
#include "Game/Level/FollowCamera.h"
#include "Game/Level/HitZones.h"
#include "Game/Player.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Components/CameraManager.h"
#include "NSlib/Object/Components/CameraModifier.h"
#include "NSlib/Object/Components/HitSensor.h"
#include "NSlib/Object/Components/PlayerInput.h"
#include "NSlib/Object/Components/ThirdPersonFollow.h"
#include "NSlib/Object/Components/TransformComponent.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Windows/Input.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <utility>

namespace NS::Editor
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

        [[nodiscard]] nlohmann::ordered_json Vec3Json(const NS::Vector3& v)
        {
            return nlohmann::ordered_json{v.x, v.y, v.z};
        }

        // 写しから場面を組む。描かない下見では資産と描き手は空
        std::unique_ptr<NS::Obj::Scene> MakeScene(const nlohmann::json& scene, const HitPreviewWorld& world)
        {
            std::unique_ptr<NS::Obj::Scene> made = std::make_unique<NS::Obj::Scene>();
            made->SetAssets(world.assets);
            made->SetRenderer(world.renderer);
            made->LoadJson(scene);
            return made;
        }

        // 写しの自機の根を rootPosition へ置き直した文書。自機が居なければ写しのまま
        nlohmann::json PlacePlayer(const nlohmann::json& snapshot, const NS::Vector3& rootPosition)
        {
            nlohmann::json placed = snapshot;
            const std::size_t index = FindPlayerObjectIndex(placed);
            if (index != NS::Obj::k_NoObjectIndex)
            {
                NS::Obj::SetObjectPosition(NS::Obj::SceneJsonObjects(placed)[index], rootPosition);
            }
            return placed;
        }

        // 置き直した自機の入力を止めて突進を頼んだ場面。自機が居なければ outPlayer は nullptr
        std::unique_ptr<NS::Obj::Scene> StartRun(const nlohmann::json& snapshot,
                                                 const HitPreviewResult& placement,
                                                 const HitPreviewWorld& world,
                                                 Player*& outPlayer)
        {
            std::unique_ptr<NS::Obj::Scene> scene = MakeScene(PlacePlayer(snapshot, placement.playerStart), world);
            outPlayer = FindPlayer(scene->Objects());
            if (outPlayer != nullptr)
            {
                // 追従カメラは置いた時の向きのままだと、突進の横や前から映る。ゲームでは狙う向きの後ろから見ているので、
                // 自機の後ろ上へ置き直す。当たりの動きはカメラを読まないので、記録は変わらない
                const NS::Vector3 behind = placement.playerStart - placement.direction * k_CameraBehindDistance +
                                           NS::Vector3{0.0f, k_CameraHeight, 0.0f};
                for (NS::Obj::Actor* object : scene->Objects())
                {
                    if (NS::Game::Level::FollowCamera* camera = NS::Obj::Cast<NS::Game::Level::FollowCamera>(object))
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
            result.impact = NS::Game::Level::ImpactRecord{};
            result.detectionIndex = -1;
            // 手元の機器を読まず、振動を手元のパッドへ送らない。組む前から入れる (配置物が組む時に機器を読んでも中立)
            const NS::OS::ScopedNeutralInput neutral;
            NS::OS::Gamepad& pad = NS::OS::Input::Get().Gamepad();
            Player* player = nullptr;
            std::unique_ptr<NS::Obj::Scene> scene = StartRun(snapshot, result, world, player);
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
                frame.shape = player->Resolver().ShapeFactors();
                frame.hitStopping = player->Resolver().IsHitStopping();
                frame.awaitingRebound = player->Resolver().IsAwaitingRebound();
                frame.rebounding = player->IsRebounding();
                frame.startedRows = player->Resolver().RowsStartedThisStep();
                frame.worldSpeed = scene->WorldSpeed();
                if (const NS::Obj::CameraManager* cameras = scene->GetCameraManager())
                {
                    frame.trauma = cameras->Trauma();
                    frame.shakeOffset = cameras->ShakeOffset();
                    if (const NS::Obj::CameraSinkModifier* sink = cameras->FindModifier<NS::Obj::CameraSinkModifier>())
                    {
                        frame.sinkPixels = sink->Pixels();
                    }
                    if (const NS::Obj::CameraTraumaModifier* shake =
                            cameras->FindModifier<NS::Obj::CameraTraumaModifier>())
                    {
                        frame.shakeAngles = shake->Angles();
                    }
                }
                result.frames.push_back(std::move(frame));
                if (result.detectionIndex < 0 && player->Resolver().LastImpact().sequence != 0)
                {
                    result.detectionIndex = step;
                    result.impact = player->Resolver().LastImpact();
                    if (const NS::Game::Level::HitTimeline* timeline =
                            NS::Game::Level::HitTimelineLibrary::Get().FindForTier(result.impact.tier))
                    {
                        for (const NS::Game::Level::HitEvent& event : timeline->events)
                        {
                            lastEventClock = std::max(lastEventClock, event.start + std::max(event.length, 1) - 1);
                        }
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
        bool IsHitTarget(NS::Obj::Actor& object)
        {
            const NS::Obj::HitSensor* bodySensor = object.BodySensorPart();
            return bodySensor != nullptr && bodySensor->IsValid() &&
                   NS::Obj::ComponentCast<NS::Game::Level::HitZones>(object.Part("HitZones")) != nullptr;
        }

        // 自機の根に一番近い、下見の相手になれる配置物の id。居なければ 0
        std::uint32_t FindNearestTarget(NS::Obj::Scene& scene, const Player& player)
        {
            const NS::Vector3 from = player.Root().Position();
            std::uint32_t nearest = 0;
            float nearestDistance = 0.0f;
            for (NS::Obj::Actor* object : scene.Objects())
            {
                if (object == &player || !object->IsActiveInHierarchy() || !IsHitTarget(*object))
                {
                    continue;
                }
                const float distance = NS::Vector3::DistanceSquared(object->Root().Position(), from);
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
            const NS::Vector3 first = result.frames.front().playerPosition;
            const NS::Vector3 last = result.frames[static_cast<std::size_t>(result.detectionIndex - 1)].playerPosition;
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
        const NS::OS::ScopedNeutralInput neutral;
        // 置き直しの前の自機と相手を、写しをそのまま組んだ場面から読む
        std::unique_ptr<NS::Obj::Scene> original = MakeScene(snapshot, world);
        Player* player = FindPlayer(original->Objects());
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
        NS::Obj::Actor* target = original->Objects().FindByObjectId(result.desc.targetId);
        if (target == nullptr)
        {
            result.error = std::format("id {} の配置物が場面に無い", result.desc.targetId);
            return result;
        }
        const NS::Obj::HitSensor* bodySensor = target->BodySensorPart();
        if (bodySensor == nullptr || !bodySensor->IsValid())
        {
            result.error = std::format("id {} の配置物に突進が当たる体が無い", result.desc.targetId);
            return result;
        }
        const NS::Obj::SensorVolume body = bodySensor->WorldVolume();
        const NS::Vector3 root = player->Root().Position();
        const NS::Sphere ball = player->SlamBallAt(root);
        const NS::Vector3 ballOffset = ball.center - root;
        // 向きはエディタが面を描く時と同じく、自機の玉から相手の体の中心へ
        NS::Game::Level::HitFaceFrame face;
        if (!NS::Game::Level::MakeHitFaceFrame(body, body.Center() - ball.center, ball.radius, face))
        {
            result.error = std::format("id {} の配置物の面を置けない (真上か真下に居るか、体の形が分からない)",
                                       result.desc.targetId);
            return result;
        }
        result.direction = -face.normal;

        // 玉の中心が面の点から自機の半径だけ手前で触れるので、そこから突進の向きの逆へ空けて置く
        const NS::Vector3 contactCenter =
            NS::Game::Level::HitFacePoint(face, desc.faceU, desc.faceV) + face.normal * ball.radius;
        NS::Vector3 ballStart = contactCenter + face.normal * k_FirstLeadDistance;
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

    std::unique_ptr<NS::Obj::Scene> MakeHitPreviewScene(const nlohmann::json& snapshot,
                                                        const HitPreviewResult& result,
                                                        const HitPreviewWorld& world)
    {
        if (!result.hit)
        {
            return nullptr;
        }
        const NS::OS::ScopedNeutralInput neutral;
        Player* player = nullptr;
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
        const NS::Game::Level::ImpactRecord& impact = result.impact;
        const NS::Vector3& launch = impact.launchVelocity;
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
} // namespace NS::Editor
