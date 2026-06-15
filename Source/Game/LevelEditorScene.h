#pragma once

#include "Framework/Graphics/RenderSettings.h"
#include "Framework/Math/Math.h"
#include "Framework/Physics/SweptTriangle.h"
#include "Framework/Scene/MaterialLibrary.h"
#include "Framework/Scene/SceneBase.h"
#include "Game/CameraRig.h"
#include "Game/Editor/EditorMode.h"
#include "Game/Editor/GizmoEditor.h"
#include "Game/EditorCameraRig.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/PlayMode.h"
#include "Game/Level/PlayState.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace NS::Graphics
{
    class InstanceBatcher;
    class Material;
    class StaticMesh;
    class SkeletalMesh;
    class Shader;
    class Skybox;
    class Texture;
    class TextureArray;
} // namespace NS::Graphics

namespace NS::Scene
{
    class IRenderable;
    class GameObject;
    class MeshRendererComponent;
    class SkeletalAnimationComponent;
    class Transform;
    class CameraComponent;
    class CameraBrainComponent;
} // namespace NS::Scene

namespace NS::Scene
{
    class PoleComponent;
} // namespace NS::Scene

class Block;
class DecorationBlock;
class HazardBlock;
class Player;
class PoleBlock;
class SlopeBlock;
class WaterBlock;

/// 編集 / プレイ両モードを 1 scene 内で扱う root scene
/// LevelData (永続) + PlayState (一時) + EditorMode を value member で保有する
class LevelEditorScene : public NS::Scene::SceneBase
{
public:
    LevelEditorScene();
    ~LevelEditorScene() override;

    LevelEditorScene(const LevelEditorScene&) = delete;
    LevelEditorScene& operator=(const LevelEditorScene&) = delete;
    LevelEditorScene(LevelEditorScene&&) = delete;
    LevelEditorScene& operator=(LevelEditorScene&&) = delete;

    void OnStart() override;
    void OnUpdate() override;
    void OnShutdown() override;

    [[nodiscard]] NS::Game::Level::LevelData& Level() noexcept { return m_level; }
    [[nodiscard]] const NS::Game::Level::LevelData& Level() const noexcept { return m_level; }
    [[nodiscard]] NS::Game::Level::PlayState& Play() noexcept { return m_play; }
    [[nodiscard]] NS::Game::Editor::EditorMode& Editor() noexcept { return m_editor; }
    [[nodiscard]] NS::Game::Level::PlayMode& PlayModeSub() noexcept { return m_playMode; }

    /// 編集 ↔ プレイのモード状態。 単一 enum で同フレーム即切替する
    enum class Mode : std::uint8_t
    {
        Edit,
        Play
    };

    [[nodiscard]] Mode CurrentMode() const noexcept { return m_mode; }

    /// Object (ギズモ変形) ツールが有効か。 false は Build (グリッド設置)。 切替は UI ボタンから行う
    [[nodiscard]] bool ObjectToolActive() const noexcept { return m_editorToolMode == EditorToolMode::Object; }
    /// 編集ツールを Build / Object 切替える。 Build へ戻す時はギズモ選択を解除する
    void SetObjectToolActive(bool active) noexcept
    {
        const EditorToolMode next = active ? EditorToolMode::Object : EditorToolMode::Build;
        if (next == m_editorToolMode)
            return;
        m_editorToolMode = next;
        if (!active)
        {
            m_gizmo.ClearSelection();
            m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
            m_lastGizmoSelected = nullptr;
        }
    }

    /// 現在選択中の配置物の m_level.objects 添字。 未選択 / 範囲外は kNoObjectIndex
    [[nodiscard]] std::size_t SelectedObjectIndex() const noexcept { return m_selectedObjectIndex; }
    /// Hierarchy から添字で配置物を選択する。 Object ツールへ切替え、 free / grid solid はギズモ選択も貼る
    void SelectObjectByIndex(std::size_t index) noexcept;

    /// Inspector が編集 / 表示できる選択を持つか (選択添字が範囲内か)
    [[nodiscard]] bool HasInspectableSelection() const noexcept
    {
        return m_selectedObjectIndex < m_level.objects.size();
    }
    /// 選択中の配置物が gridAligned か。 未選択は false
    [[nodiscard]] bool SelectedIsGridAligned() const noexcept
    {
        return HasInspectableSelection() &&
               (m_level.objects[m_selectedObjectIndex].flags & NS::Game::Level::kObjectFlagGridAligned) != 0;
    }
    /// Inspector 表示用に選択中 ObjectInstance のコピーを返す。 未選択は既定値
    [[nodiscard]] NS::Game::Level::ObjectInstance SelectedObjectSnapshot() const noexcept
    {
        if (m_selectedObjectIndex < m_level.objects.size())
            return m_level.objects[m_selectedObjectIndex];
        return NS::Game::Level::ObjectInstance{};
    }
    /// 選択中の自由オブジェクトの位置を設定する。 永続化は SyncFreeObjectTransforms 任せ (gridAligned / 非選択は no-op)
    void SetSelectedFreePosition(NS::Math::Vector3 position) noexcept;
    /// 選択中の自由オブジェクトのスケールを設定する。 最小正値に clamp する (gridAligned / 非選択は no-op)
    void SetSelectedFreeScale(NS::Math::Vector3 scale) noexcept;
    /// 選択中の grid solid ブロックを自由オブジェクトへ昇格する (grid solid 以外は no-op)
    void PromoteSelectedToFree() noexcept;

    /// Object モードかつギズモで何か選択中なら true (material 適用先がある状態)
    [[nodiscard]] bool HasGizmoSelection() const noexcept
    {
        return ObjectToolActive() && m_gizmo.Selected() != nullptr;
    }
    /// ギズモ選択中の自由オブジェクトに matPath の .mat を適用する。 適用できたら true
    bool ApplyMaterialToSelected(const std::filesystem::path& matPath);

    /// 最後に OnRenderScene で解決した scene 段設定 (project 既定 ← scene override)。object 段は含まない
    [[nodiscard]] const NS::Graphics::RenderSettings& DebugResolvedSettings() const noexcept
    {
        return m_debugResolvedSettings;
    }
    /// 最後に構築した scene override。出所逆算 (default / scene) の入力に使う
    [[nodiscard]] const NS::Graphics::RenderSettingsOverride& DebugSceneOverride() const noexcept
    {
        return m_debugSceneOverride;
    }
    /// 代表 object override (Player の MeshRenderer)。出所逆算 (object) の入力。未設定なら空 override
    [[nodiscard]] const NS::Graphics::RenderSettingsOverride& DebugPlayerObjectOverride() const noexcept
    {
        return m_debugPlayerObjectOverride;
    }

    /// Edit → Play 遷移。 spawn 位置に player 再構築、 EditorCamera off、 Player Component 再活性化
    void EnterPlay() noexcept;

    /// Play → Edit 遷移。 paused/clear/death をリセット、 EditorCamera on、 Player Component 休止
    void EnterEdit() noexcept;

protected:
    /// テーマの lighting をシーン単位の上書きとして宣言する。push は書かず override を返すだけ
    NS::Graphics::RenderSettingsOverride BuildSceneOverride() override;

private:
    /// 基底 OnRender が scene 解決後に呼ぶ描画本体。ctx を組み立てて全 Renderable を描く
    void OnRenderScene() override;

    /// dirty flag 検出時のみ m_blocks と m_collisionWorld を LevelData から再構築する
    void RebuildBlocksFromLevelData();

    /// 仮 skinned キャラ (glTF) を毎ステップ進めて描画する debug hook
    /// F1 再生/停止、 F2 クリップ送り、 F3/F4 速度。 ImGui 入力中はキー無効
    void UpdateAnimatedModel();

    /// ギズモの選択候補 (自由オブジェクト + grid solid ブロック) を連結し直して注入する
    /// grid rebuild で m_blocks の pointer が変わるたびに呼んで span を貼り直す
    void RefreshGizmoSelectables();

    /// grid solid ブロックの ObjectInstance から gridAligned ビットを落として自由オブジェクト化する
    /// Object モードで grid ブロックを掴んだ時に呼ぶ。 自由化後も当たり判定とセーブ対象のまま
    void PromoteGridBlockToFree(std::size_t blockIndex);

    /// ギズモで変形した自由オブジェクトの Transform を対応する ObjectInstance へ書き戻す
    /// セーブに載せ、 次の RebuildBlocksFromLevelData で巻き戻らないようにする
    void SyncFreeObjectTransforms();

    /// ビューポートでギズモ選択が変わった時だけ m_selectedObjectIndex を追従させる
    /// Hierarchy で選んだ非選択候補 (slope 等) を毎フレーム潰さないよう前フレーム値で差分判定する
    void ResolveSelectedIndexFromGizmo() noexcept;

    std::unique_ptr<NS::Graphics::StaticMesh> m_cubeMesh;
    std::unique_ptr<NS::Graphics::Texture> m_texture;
    std::unique_ptr<NS::Graphics::TextureArray> m_blockTextures;
    std::unique_ptr<NS::Graphics::Shader> m_standardVS; // player / block 共有 (standard.vs)
    std::unique_ptr<NS::Graphics::Shader> m_playerPS;   // player / block / skinned 共有 (player.ps)
    std::unique_ptr<NS::Graphics::Shader> m_waterPS;    // 水専用 (water.ps、alpha<1 出力)
    std::unique_ptr<NS::Graphics::Shader> m_shadowPS;   // 接地シャドウ専用 (shadow.ps、放射状アルファ)
    std::unique_ptr<NS::Graphics::Material> m_playerMaterial;
    std::unique_ptr<NS::Graphics::Material> m_blockMaterial;
    // 水ブロック専用の半透明 Material (Alpha)。不透明ブロックと共有すると全ブロックが透けるため別インスタンス
    std::unique_ptr<NS::Graphics::Material> m_waterMaterial;
    // 接地シャドウ共有 Material (standard.vs + shadow.ps、Alpha)
    std::unique_ptr<NS::Graphics::Material> m_shadowMaterial;
    std::unique_ptr<NS::Graphics::Skybox> m_skybox;
    std::unique_ptr<NS::Graphics::InstanceBatcher> m_instanceBatcher;

    // 角度別 wedge mesh を 4 種だけ shared でキャッシュ。 SlopeBlock 1 個ずつに mesh を持たせず、
    // scene 寿命のあいだ共有して描画コストとメモリを抑える
    std::unique_ptr<NS::Graphics::StaticMesh> m_wedgeMesh45;
    std::unique_ptr<NS::Graphics::StaticMesh> m_wedgeMesh30;
    std::unique_ptr<NS::Graphics::StaticMesh> m_wedgeMesh22;
    std::unique_ptr<NS::Graphics::StaticMesh> m_wedgeMesh15;

    // 掴まり系 mesh: 円柱を 1 度だけ生成して全 instance で共有する
    std::unique_ptr<NS::Graphics::StaticMesh> m_poleMesh;

    // 接地シャドウ用の共有 quad mesh (XZ 平面)
    std::unique_ptr<NS::Graphics::StaticMesh> m_shadowMesh;

    // 仮 skinned キャラの描画リソース (mesh / shader / material)。 アセット未取得時は全て null
    std::unique_ptr<NS::Graphics::SkeletalMesh> m_skinnedMesh;
    std::unique_ptr<NS::Graphics::Shader> m_skinnedVS;
    std::unique_ptr<NS::Graphics::Material> m_skinnedMaterial;

    std::unique_ptr<Player> m_player;

    // 仮 skinned キャラ本体。 GameObject が MeshRenderer + SkeletalAnimation を所有し、 参照をキャッシュする
    std::unique_ptr<NS::Scene::GameObject> m_animatedModel;
    NS::Scene::MeshRendererComponent* m_animMesh = nullptr;
    NS::Scene::SkeletalAnimationComponent* m_animPlayer = nullptr;
    float m_animSpeed = 1.0f;
    std::vector<std::unique_ptr<Block>> m_blocks;
    std::vector<std::unique_ptr<SlopeBlock>> m_slopes;
    std::vector<std::unique_ptr<PoleBlock>> m_poles;
    std::vector<std::unique_ptr<HazardBlock>> m_hazards;
    std::vector<std::unique_ptr<WaterBlock>> m_waters;
    std::vector<std::unique_ptr<DecorationBlock>> m_decorations;

    std::unique_ptr<CameraRig> m_cameraRig;
    std::unique_ptr<EditorCameraRig> m_editorCameraRig;

    // 実カメラ 1 個 + Brain を載せる host。Brain が follow / free-fly vcam から選んで実カメラへ書く
    std::unique_ptr<NS::Scene::GameObject> m_cameraHost;
    NS::Scene::CameraComponent* m_mainCamera = nullptr;
    NS::Scene::CameraBrainComponent* m_brain = nullptr;

    std::vector<NS::Math::AABB> m_collisionWorld;
    std::vector<NS::Physics::Triangle> m_collisionTriangles;
    std::vector<NS::Scene::PoleComponent*> m_polePtrs;

    NS::Game::Level::LevelData m_level{};
    NS::Game::Level::PlayState m_play{};
    NS::Game::Editor::EditorMode m_editor{};
    NS::Game::Level::PlayMode m_playMode{};
    Mode m_mode = Mode::Edit;

    /// 編集中の入力所有。 Build=grid 設置、 Object=ギズモ変形。 Tab で切替え同時に 1 つだけが LMB/R/Ctrl+Z を消費
    enum class EditorToolMode : std::uint8_t
    {
        Build,
        Object
    };
    EditorToolMode m_editorToolMode = EditorToolMode::Build;

    NS::Game::Editor::GizmoEditor m_gizmo{};

    // .mat からマテリアルを読み込みキャッシュする。 free オブジェクトより先に宣言し、 暗黙デストラクタの
    // 逆順破棄でも free オブジェクト (Material* を参照) より後に破棄されるよう順序を保証する
    std::unique_ptr<NS::Scene::MaterialLibrary> m_materialLibrary;

    // 非 gridAligned な配置物の runtime インスタンス。 RebuildBlocksFromLevelData が m_level.objects から作り直す
    std::vector<std::unique_ptr<Block>> m_freeObjects;
    // m_freeObjects[i] に対応する m_level.objects の添字 (材質適用 / 再選択の逆引き用)
    std::vector<std::size_t> m_freeSourceIndices;

    // ギズモへ渡す選択候補の安定ストレージ。 自由オブジェクトと grid solid ブロックを連結した span の実体
    std::vector<NS::Scene::GameObject*> m_selectablePtrs;
    std::vector<NS::Math::Vector3> m_selectableHalfExtents;

    // m_blocks[i] に対応する m_level.objects の添字 (Hierarchy からの grid solid 選択の逆引き用、 m_blocks と同長)
    std::vector<std::size_t> m_blockSourceIndices;
    // Hierarchy / Inspector が参照する選択添字。 Hierarchy クリックとギズモ選択の両方から更新する
    std::size_t m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
    // ビューポート由来のギズモ選択変化だけを index へ反映するための前フレーム値 (Hierarchy 選択を潰さない)
    NS::Scene::Transform* m_lastGizmoSelected = nullptr;

    // Debug provenance パネルの読み出し元。書き込みは OnRenderScene で毎フレーム行う
    // 値メンバなので Release でも存在するが、 ImGui 読み出しのみ #if ガードする
    NS::Graphics::RenderSettings m_debugResolvedSettings{};
    NS::Graphics::RenderSettingsOverride m_debugSceneOverride{};
    NS::Graphics::RenderSettingsOverride m_debugPlayerObjectOverride{};

    /// 差分フレームのみ cubemap を再ロードするため前回パスを保持する
    std::filesystem::path m_loadedSkyboxPath{};
};
