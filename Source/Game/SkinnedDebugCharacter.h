#pragma once

/// @file SkinnedDebugCharacter.h
/// @brief 仮 skinned キャラ (glTF) をプレイ画面で常時再生する debug 用 GameObject
///
/// @details 形 (SkeletalMesh) と材質 (.mat) は AssetManager 所有を参照し、 アニメ clips と
/// 再生状態のみ自分で持つ。 MeshRenderer + SkeletalAnimation を自分に合成する単一の「種別」
/// F1 再生/停止、 F2 クリップ送り、 F3/F4 速度。 依存: NS::Scene::AssetManager / GameObject

#include "Framework/Scene/GameObject.h"

#include <filesystem>
#include <memory>

namespace NS::Scene
{
    class AssetManager;
    class SceneBase;
    class SkeletalAnimationComponent;
} // namespace NS::Scene

/// glTF の skinned mesh を AssetManager から借りて再生する debug キャラ
class SkinnedDebugCharacter : public NS::Scene::GameObject
{
public:
    /// modelDir 配下の候補 glTF を読み、 materialPath の .mat を貼って構築する
    /// scene へ attach し OnStart まで済ませた即使用可能な状態で返す。 アセット欠落時は nullptr
    [[nodiscard]] static std::unique_ptr<SkinnedDebugCharacter> Create(NS::Scene::AssetManager& assets,
                                                                       NS::Scene::SceneBase* scene,
                                                                       const std::filesystem::path& modelDir,
                                                                       const std::filesystem::path& materialPath);

    /// F1〜F4 の debug 操作を 1 フレームぶん処理する (ImGui 入力中は無効)
    void HandleDebugInput();

private:
    SkinnedDebugCharacter() = default;

    NS::Scene::SkeletalAnimationComponent* m_anim = nullptr;
    float m_speed = 1.0f;
};
