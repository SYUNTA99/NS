#pragma once

namespace NS::Game::Level
{

    /// @brief プレイ中だけ使う一時データ。.scene には書かない
    /// @details Tick で書き換わり、プレイ終了で破棄する。保存する値は SceneData 側にある
    /// SceneData と別 struct に分け、プレイ進行が誤ってレベルを書き換える経路をコンパイル時に断つ
    struct PlayState
    {
        NS::Math::Vector3 playerPosition{}; // 出現位置 (capsule 中心)。 プレイ突入時に baseline から写す
        bool paused = false;                // 時間停止中か
        std::int32_t stepFrames = 0;        // コマ送り残り fixed step 数。 paused でもこの数だけは進める
        bool clearTriggered = false;        // ゴール接触でクリアへ入ったか。 ヘルスは player の HealthComponent が持つ
    };

} // namespace NS::Game::Level
