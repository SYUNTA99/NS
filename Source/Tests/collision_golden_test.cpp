#include "golden_trace.h"

#include "Editor/EditorObjects.h"
#include "Game/Player.h"

#include <Game/Level/Breakable.h>
#include <Game/Level/ImpactResolver.h>
#include <Game/Level/LaunchedBody.h>
#include <Game/Player/PlayerAppearance.h>
#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerStateManager.h>
#include <Runtime/Core/AABB.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Object/AssetManager.h>
#include <Runtime/Object/Components/MeshRenderer.h>
#include <Runtime/Object/Components/PlayerInput.h>
#include <Runtime/Object/Components/RigidBody.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/ObjectList.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Physics/PhysicsScene.h>
#include <Runtime/Platform/Clock.h>
#include <Runtime/Platform/Filesystem.h>
#include <Runtime/Platform/Window.h>

#include "entity_test_stage.h"
#include "jolt_test_scene.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Vector3;
    using NS::Game::Player::PlayerAppearance;
    using NS::Game::Player::PlayerComponent;
    using NS::Game::Player::PlayerStateManager;
    using NS::Obj::GameObject;
    using NS::Tests::CompareTraces;
    using NS::Tests::DescribeDiff;
    using NS::Tests::FoldTrace;
    using NS::Tests::LoadBaseline;
    using NS::Tests::MissingBaselineMessage;
    using NS::Tests::SaveBaseline;
    using NS::Tests::StepRecord;
    using NS::Tests::TraceDiff;
    using NS::Tests::TraceTolerance;

    constexpr float k_FixedDt = 1.0f / 60.0f;
    constexpr TraceTolerance k_Exact{};
    constexpr int k_RunSteps = 300; // 5 秒。走り出しから最高速度まで伸び切る長さ

    const Vector3 k_Forward{0.0f, 0.0f, 1.0f};

    // プレイヤー相当 1 体と床 1 枚だけの検証台
    class Rig
    {
    public:
        Rig()
        {
            m_object.AddComponent<PlayerStateManager>();
            m_movement = m_object.AddComponent<PlayerComponent>();

            // 床は走り切る z 方向だけ伸ばす
            NsTest::AddBox(m_physics, AABB{Vector3{0.0f, -0.5f, 32.0f}, Vector3{4.0f, 0.5f, 44.0f}});
            m_physics.OptimizeBroadPhase();
            m_object.Root().SetPosition(Vector3{0.0f, 1.0f, 0.0f});
            m_movement->OnStart();
            m_object.FindComponent<PlayerStateManager>()->OnStart();

            // 開始位置は空中に取る
            for (int i = 0; i < 30 && !m_movement->IsGrounded(); ++i)
                m_movement->OnUpdate();
        }

        void Step(const Vector3& direction, float speedScale, int steps)
        {
            for (int i = 0; i < steps; ++i)
            {
                m_movement->SetDesiredMove(direction, speedScale);
                m_movement->OnUpdate();
                m_trace.push_back(
                    StepRecord{m_object.Root().Position(), m_movement->Velocity(), m_movement->IsGrounded()});
            }
        }

        [[nodiscard]] const std::vector<StepRecord>& Trace() const noexcept { return m_trace; }

    private:
        NsTest::EntityStage m_stage;
        GameObject& m_object = m_stage.owner;
        NS::Phys::PhysicsScene& m_physics = m_stage.physics;
        PlayerComponent* m_movement = nullptr;
        std::vector<StepRecord> m_trace;
    };

    // 全開走行だけ
    std::vector<StepRecord> RunSteady()
    {
        Rig rig;
        rig.Step(k_Forward, 1.0f, k_RunSteps);
        return rig.Trace();
    }

    constexpr int k_ImpactSteps = 360; // 6 秒。押し飛ばした物へ追いついて当て直す往復が 4 回入る長さ
    // 突進と凍結と反発からの立て直しが 1 周に収まる間隔。短いと接地待ちで発動が落ちて経路が読めない
    constexpr int k_ImpactSlamPeriod = 30;
    constexpr float k_ImpactSlamCharge = 1.0f;
    // 走行速度 8.0 で 6 秒ぶん走り切れる長さ。短いと道の端から落ち、軌跡の大半が自由落下になる
    constexpr std::int16_t k_ImpactFloorCells = 100;
    constexpr std::int16_t k_ImpactTargetZ = 6;
    constexpr float k_ImpactTargetMass = 1.0f;
    // 壊れない高さ。破壊が入っても反発と押し飛ばしの経路が変わらない
    constexpr float k_ImpactTargetToughness = 99.0f;

    // 見た目の持ち替えを走行に重ねる設定。資産を差すと参照が引き当たり、MeshRenderer の mesh が実際に入れ替わる
    struct LooksSwap
    {
        NS::Obj::AssetManager* assets = nullptr;
        std::string standingMeshRef;
        std::string ballMeshRef;
        int period = 0; // 丸まる・戻るを切り替える間隔のフレーム数。0 なら切り替えない
    };

    // 突進の間隔 30 と割り切れない長さ。構え・凍結・潰れ・反発のどの最中にも切り替えが入る
    constexpr int k_LooksSwapPeriod = 7;

    // 反発と押し飛ばしを含む経路。当たりを持つ配置物が要るのでシーンの JSON から組む
    class ImpactRig
    {
    public:
        explicit ImpactRig(const LooksSwap& looks = LooksSwap{}) : m_swapPeriod(looks.period)
        {
            nlohmann::json data = NS::Obj::MakeSceneJson();
            for (std::int16_t z = 0; z < k_ImpactFloorCells; ++z)
                NS::Obj::SceneJsonObjects(data).push_back(NS::Editor::MakeCellObject(0, 0, z));

            nlohmann::json player = MakePlayerObject(Vector3{0.0f, 1.41f, 0.0f}, NS::Core::Quaternion{});
            // 基準は衝突だけの軌跡を持つ。寄せは真正面の的へも物理の丸めで出る 1e-6 度ほどの角度で働き、
            // 2 回目の突進から軌跡を動かす。寄せの確かめは衝突の試しと Replay の台本が持つ
            nlohmann::json* movementEntry = NS::Obj::FindComponentEntry(player, "PlayerComponent");
            EXPECT_NE(movementEntry, nullptr);
            if (movementEntry != nullptr)
            {
                NS::Obj::SetField(*movementEntry, "寄せる角度の上限", 0.0f);
            }
            NS::Obj::ObjectJsonComponents(player).push_back(NS::Obj::MakeComponentEntry("ImpactResolver"));
            NS::Obj::ObjectJsonComponents(player).push_back(NS::Obj::MakeComponentEntry("CollisionInput"));
            nlohmann::json appearance = NS::Obj::MakeComponentEntry("PlayerAppearance");
            NS::Obj::SetField(appearance, "立ち姿のメッシュ", looks.standingMeshRef);
            NS::Obj::SetField(appearance, "玉のメッシュ", looks.ballMeshRef);
            NS::Obj::ObjectJsonComponents(player).push_back(std::move(appearance));
            NS::Obj::SceneJsonObjects(data).push_back(player);

            nlohmann::json target = NS::Editor::MakeCellObject(0, 1, k_ImpactTargetZ);
            // 出荷のコースと同じく、重さと面は置かれている間キネマティックの RigidBody が持つ
            nlohmann::json body = NS::Obj::MakeComponentEntry("RigidBody");
            NS::Obj::SetField(body, "キネマティック", true);
            NS::Obj::SetField(body, "質量", k_ImpactTargetMass);
            NS::Obj::SetField(body, "摩擦", 0.6f);
            NS::Obj::SetField(body, "跳ね返り", 0.35f);
            // 速い着地の接触を床の面で作る。偽だと床へ沈んだ次のフレームに剛体へ渡る
            NS::Obj::SetField(body, "連続衝突判定", true);
            NS::Obj::ObjectJsonComponents(target).push_back(std::move(body));
            NS::Obj::ObjectJsonComponents(target).push_back(NS::Obj::MakeComponentEntry("Breakable"));
            NS::Obj::SceneJsonObjects(data).push_back(target);

            m_scene.SetAssets(looks.assets);
            m_scene.LoadJson(std::move(data));

            Player* live = FindPlayer(m_scene.Objects());
            EXPECT_NE(live, nullptr);
            if (live != nullptr)
            {
                m_player = live;
                m_movement = live->FindComponent<PlayerComponent>();
                m_appearance = live->FindComponent<PlayerAppearance>();
                m_renderer = live->FindComponent<NS::Obj::MeshRenderer>();
                // 見た目は毎フレーム自機の丸まりを写す。写しを止めないと、持ち替えが同じフレームの中で上書きされる
                if (m_swapPeriod > 0 && m_appearance != nullptr)
                {
                    m_appearance->SetActive(false);
                }
                if (m_renderer != nullptr)
                {
                    m_lastShownMesh = m_renderer->GetMesh();
                }
                // 入力の component は EarlyUpdate で実機の入力を書き込む。起こしたままだと走行入力が毎フレーム 0 になる
                if (NS::Obj::PlayerInput* input = live->FindComponent<NS::Obj::PlayerInput>())
                    input->SetActive(false);
            }
            m_scene.Objects().ForEachComponent<NS::Game::Level::Breakable>(
                [](NS::Game::Level::Breakable& breakable) { breakable.SetToughness(k_ImpactTargetToughness); });
        }

        void Step(const Vector3& direction, float speedScale, int steps)
        {
            for (int i = 0; i < steps; ++i)
            {
                m_movement->SetDesiredMove(direction, speedScale);
                if (m_stepIndex % k_ImpactSlamPeriod == 0)
                    m_movement->RequestBodySlam(k_ImpactSlamCharge);
                if (m_swapPeriod > 0 && m_stepIndex % m_swapPeriod == 0)
                {
                    SwapLooks();
                }
                ++m_stepIndex;
                // 岩は Jolt の剛体なので、帯だけ回しても動かない。物理の 1 フレームを LateUpdate 帯の手前へ挟む
                // RigidBody の前後の処理も Scene::OnUpdate と同じ順で挟む。抜くと飛んだ岩の姿勢が書き戻らない
                m_scene.Objects().UpdateObjects(std::numeric_limits<int>::min(), NS::Obj::TickPriority::LateUpdate);
                m_scene.Objects().ForEachComponent<NS::Obj::RigidBody>([](NS::Obj::RigidBody& body) {
                    if (body.IsActive())
                        body.PrePhysicsStep();
                });
                m_scene.Physics().Update(k_FixedDt);
                m_scene.Objects().ForEachComponent<NS::Obj::RigidBody>([](NS::Obj::RigidBody& body) {
                    if (body.IsActive())
                        body.PostPhysicsStep();
                });
                m_scene.Objects().UpdateObjects(NS::Obj::TickPriority::LateUpdate);
                m_scene.Objects().SnapshotObjects();
                m_trace.push_back(
                    StepRecord{m_player->Root().Position(), m_movement->Velocity(), m_movement->IsGrounded()});
                CountShownMeshChange();
            }
        }

        [[nodiscard]] const std::vector<StepRecord>& Trace() const noexcept { return m_trace; }
        // 1 フレームを回し終えた時点で、MeshRenderer の mesh が前のフレームから変わっていた回数
        [[nodiscard]] int ShownMeshChanges() const noexcept { return m_shownMeshChanges; }
        // 見た目を持ち替えた回数
        [[nodiscard]] int Swaps() const noexcept { return m_swaps; }

    private:
        void SwapLooks()
        {
            if (m_appearance == nullptr || m_renderer == nullptr)
            {
                return;
            }
            if (m_appearance->IsCurled())
            {
                m_appearance->Uncurl();
            }
            else
            {
                m_appearance->Curl();
            }
            ++m_swaps;
        }

        // 持ち替えた直後でなく 1 フレームを回した後に数える。フレームの途中で上書きされた持ち替えは数えない
        void CountShownMeshChange()
        {
            if (m_renderer == nullptr)
            {
                return;
            }
            const NS::Gfx::Mesh* shown = m_renderer->GetMesh();
            if (shown != m_lastShownMesh)
            {
                ++m_shownMeshChanges;
                m_lastShownMesh = shown;
            }
        }

        NS::Obj::Scene m_scene;
        Player* m_player = nullptr;
        PlayerComponent* m_movement = nullptr;
        PlayerAppearance* m_appearance = nullptr;
        NS::Obj::MeshRenderer* m_renderer = nullptr;
        std::vector<StepRecord> m_trace;
        int m_stepIndex = 0;
        int m_swapPeriod = 0;
        int m_shownMeshChanges = 0;
        int m_swaps = 0;
        const NS::Gfx::Mesh* m_lastShownMesh = nullptr;
    };

    // 壊せる物へ走り込み、反発しながら追いかけ直す。記録するのは自機だけで、飛ばされた物の位置は入れない
    std::vector<StepRecord> RunImpact()
    {
        ImpactRig rig;
        rig.Step(k_Forward, 1.0f, k_ImpactSteps);
        return rig.Trace();
    }

    // 上へ大きく、進行方向と逆の水平へ弾かれたフレームがあるか。反動が消えた改修を軌跡の一致より先に知らせる
    // 反動の水平は 1 m/s 前後。台の毎フレームの前向きの入力が、反動の空中の操作でこの水平を少しずつ打ち消す
    // 水平は向きだけを見る
    bool HasReboundStep(const std::vector<StepRecord>& trace) noexcept
    {
        for (const StepRecord& s : trace)
        {
            if (s.velocity.y > 5.0f && s.velocity.z < 0.0f)
            {
                return true;
            }
        }
        return false;
    }

    float MaxForwardSpeedFrom(const std::vector<StepRecord>& trace, std::size_t first) noexcept
    {
        float peak = 0.0f;
        for (std::size_t i = first; i < trace.size(); ++i)
            peak = std::max(trace[i].velocity.z, peak);
        return peak;
    }

} // namespace

class CollisionGolden : public ::testing::Test
{
protected:
    void SetUp() override { NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt); }
};

TEST_F(CollisionGolden, HashIsStableAcrossTwoRuns)
{
    EXPECT_EQ(FoldTrace(RunSteady()), FoldTrace(RunSteady()));
    EXPECT_EQ(FoldTrace(RunImpact()), FoldTrace(RunImpact()));
}

TEST_F(CollisionGolden, SteadyRunMatchesGoldenTrace)
{
    Rig rig;
    rig.Step(k_Forward, 1.0f, k_RunSteps);

    const std::vector<StepRecord>& trace = rig.Trace();
    EXPECT_GT(MaxForwardSpeedFrom(trace, 0), 7.5f) << "走行速度 8 まで伸びていない";
    EXPECT_LT(MaxForwardSpeedFrom(trace, 0), 9.0f) << "走行速度 8 を超えて伸びている";
    EXPECT_TRUE(trace.back().grounded) << "走り切る前に床から外れている";

    const std::optional<std::vector<StepRecord>> baseline = LoadBaseline("collision_steady_run");
    ASSERT_TRUE(baseline.has_value()) << MissingBaselineMessage("collision_steady_run");
    const TraceDiff diff = CompareTraces(*baseline, trace, k_Exact);
    EXPECT_TRUE(diff.matched) << DescribeDiff(diff, *baseline, trace);
}

TEST_F(CollisionGolden, ImpactMatchesGoldenTrace)
{
    const std::vector<StepRecord> trace = RunImpact();

    EXPECT_TRUE(HasReboundStep(trace)) << "経路に反発が現れていない";

    const std::optional<std::vector<StepRecord>> baseline = LoadBaseline("collision_impact");
    ASSERT_TRUE(baseline.has_value()) << MissingBaselineMessage("collision_impact");
    const TraceDiff diff = CompareTraces(*baseline, trace, k_Exact);
    EXPECT_TRUE(diff.matched) << DescribeDiff(diff, *baseline, trace);
}

// 見た目を持ち替えても動きは変わらない。玉と立ち姿を走行の途中で何度も持ち替え、持ち替えない基準とビットまで比べる
// 仮の形でも、見た目のファイルを本物のモデルへ差し替えた形でも、同じ基準に一致する
TEST_F(CollisionGolden, ImpactMatchesGoldenTraceWhileSwappingLooks)
{
    NS::Platform::WindowDesc windowDesc{};
    windowDesc.title = "ns_collision_golden_looks";
    windowDesc.size = NS::Core::Size2D{320, 240};
    windowDesc.visible = false;
    NS::Platform::Window window(windowDesc);
    ASSERT_TRUE(window.IsValid());
    NS::Gfx::RendererDesc rendererDesc{};
    rendererDesc.vsync = false;
    rendererDesc.enableDebugLayer = false;
    NS::Gfx::Renderer renderer(rendererDesc, window);
    if (!renderer.IsValid())
    {
        GTEST_SKIP() << "Device 確立不可 (headless)。mesh を作れないので持ち替えが MeshRenderer に届かない";
    }

    const std::optional<std::vector<StepRecord>> baseline = LoadBaseline("collision_impact");
    ASSERT_TRUE(baseline.has_value()) << MissingBaselineMessage("collision_impact");

    // 資産は Renderer より先に手放す。Renderer より後に宣言した物が先に破棄される
    NS::Obj::AssetManager assets{NS::Platform::FileSystem::ContentRoot()};
    assets.RegisterBuiltins();
    assets.RegisterSharedMaterials();

    const LooksSwap placeholder{&assets, "", "", k_LooksSwapPeriod};
    const LooksSwap models{&assets, "Assets/Models/Soldier.glb", "Assets/Models/Xbot.glb", k_LooksSwapPeriod};
    for (const LooksSwap& looks : {placeholder, models})
    {
        ImpactRig rig(looks);
        rig.Step(k_Forward, 1.0f, k_ImpactSteps);

        ASSERT_GT(rig.Swaps(), 0);
        EXPECT_EQ(rig.ShownMeshChanges(), rig.Swaps())
            << "持ち替えがフレームの終わりまで MeshRenderer に残っていない: " << looks.standingMeshRef;
        const TraceDiff diff = CompareTraces(*baseline, rig.Trace(), k_Exact);
        EXPECT_TRUE(diff.matched) << "立ち姿の参照 '" << looks.standingMeshRef << "'\n"
                                  << DescribeDiff(diff, *baseline, rig.Trace());
        EXPECT_EQ(FoldTrace(rig.Trace()), FoldTrace(*baseline));
    }
}

TEST_F(CollisionGolden, DISABLED_SaveBaselines)
{
    EXPECT_TRUE(SaveBaseline("collision_steady_run", RunSteady()));
    EXPECT_TRUE(SaveBaseline("collision_impact", RunImpact()));
}
