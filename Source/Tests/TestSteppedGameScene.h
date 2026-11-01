#pragma once

#include "Game/Level/GameScene.h"

//! 当たりの判定とその後の StepGame だけを回せる GameScene。アクター・暗転は進めない。
//! 本番の普段の歩 (Scene::OnUpdate) と同じく、判定の後に StepGame を呼ぶ。アクターの解放を断る囲み (ActorList::BeginStep) は Scene だけが持つので、ここでは囲まない
class SteppedGameScene final : public GL::Level::GameScene
{
public:
    void StepSensorsAndGame()
    {
        HitSensors().OnTick();
        StepGame();
    }
};
