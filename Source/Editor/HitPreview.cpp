#include "Editor/HitPreview.h"

#include "Editor/EditorObjects.h"
#include "Game/Level/HitZones.h"
#include "Game/Player.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <format>
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
        // 1 回目の置き直しで、玉の表面から面までに空ける距離 (m)。2 回目からは 1 歩の長さを測って決め直す
        constexpr float k_FirstLeadDistance = 2.0f;

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
        nlohmann::json PlacePlayer(const nlohmann::json& snapshot, const NS::Core::Vector3& rootPosition)
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
            Player* player = nullptr;
            std::unique_ptr<NS::Obj::Scene> scene = StartRun(snapshot, result, world, player);
            if (player == nullptr)
            {
                result.error = "場面に自機が居ない";
                return;
            }
            bool sawRebound = false;
            int afterRebound = -1;
            for (int step = 0; step < result.desc.maxFrames; ++step)
            {
                scene->OnUpdate();
                HitPreviewFrame frame;
                frame.playerPosition = player->Root().Position();
                frame.shape = player->Resolver().ShapeFactors();
                frame.hitStopping = player->Resolver().IsHitStopping();
                frame.awaitingRebound = player->Resolver().IsAwaitingRebound();
                frame.rebounding = player->IsRebounding();
                frame.startedRows = player->Resolver().RowsStartedThisStep();
                result.frames.push_back(std::move(frame));
                if (result.detectionIndex < 0 && player->Resolver().LastImpact().sequence != 0)
                {
                    result.detectionIndex = step;
                    result.impact = player->Resolver().LastImpact();
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
                    if (afterRebound >= result.desc.framesAfterRebound)
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

        // 検知までの 1 歩の水平の長さを、突進を出してから検知までの自機の進みで測る。測れなければ 0
        float MeasureStepLength(const HitPreviewResult& result)
        {
            if (result.detectionIndex < 2)
            {
                return 0.0f;
            }
            const NS::Core::Vector3 first = result.frames.front().playerPosition;
            const NS::Core::Vector3 last =
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
        // 置き直しの前の自機と相手を、写しをそのまま組んだ場面から読む
        std::unique_ptr<NS::Obj::Scene> original = MakeScene(snapshot, world);
        Player* player = FindPlayer(original->Objects());
        if (player == nullptr)
        {
            result.error = "場面に自機が居ない";
            return result;
        }
        NS::Obj::Actor* target = original->Objects().FindByObjectId(desc.targetId);
        if (target == nullptr)
        {
            result.error = std::format("id {} の配置物が場面に無い", desc.targetId);
            return result;
        }
        const NS::Obj::HitSensor* bodySensor = target->BodySensorPart();
        if (bodySensor == nullptr || !bodySensor->IsValid())
        {
            result.error = std::format("id {} の配置物に突進が当たる体が無い", desc.targetId);
            return result;
        }
        const NS::Obj::SensorVolume body = bodySensor->WorldVolume();
        const NS::Core::Vector3 root = player->Root().Position();
        const NS::Core::Sphere ball = player->SlamBallAt(root);
        const NS::Core::Vector3 ballOffset = ball.center - root;
        // 向きはエディタが面を描く時と同じく、自機の玉から相手の体の中心へ
        NS::Game::Level::HitFaceFrame face;
        if (!NS::Game::Level::MakeHitFaceFrame(body, body.Center() - ball.center, ball.radius, face))
        {
            result.error =
                std::format("id {} の配置物の面を置けない (真上か真下に居るか、体の形が分からない)", desc.targetId);
            return result;
        }
        result.direction = -face.normal;

        // 玉の中心が面の点から自機の半径だけ手前で触れるので、そこから突進の向きの逆へ空けて置く
        const NS::Core::Vector3 contactCenter =
            NS::Game::Level::HitFacePoint(face, desc.faceU, desc.faceV) + face.normal * ball.radius;
        NS::Core::Vector3 ballStart = contactCenter + face.normal * k_FirstLeadDistance;
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

    std::unique_ptr<NS::Obj::Scene> BuildHitPreviewSceneAt(const nlohmann::json& snapshot,
                                                           const HitPreviewResult& result,
                                                           int frameIndex,
                                                           const HitPreviewWorld& world)
    {
        Player* player = nullptr;
        std::unique_ptr<NS::Obj::Scene> scene = StartRun(snapshot, result, world, player);
        if (player == nullptr)
        {
            return nullptr;
        }
        const int last = std::max(static_cast<int>(result.frames.size()) - 1, 0);
        const int steps = std::clamp(frameIndex, 0, last) + 1;
        for (int step = 0; step < steps; ++step)
        {
            scene->OnUpdate();
        }
        return scene;
    }
} // namespace NS::Editor
