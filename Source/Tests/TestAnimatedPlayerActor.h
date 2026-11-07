#pragma once

#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/Action/ActorActionInfo.h"
#include "NSlib/CoreObject/ClassObjs/Animation.h"
#include "NSlib/CoreObject/Reflection/AssetField.h"
#include "TestActorFile.h"

#include <string>
#include <utility>
#include <vector>

class TestAnimatedPlayerActor final : public PlayerActor
{
protected:
    void OnInit() override
    {
        PlayerActor::OnInit();
        CreateClassObj(m_animation);
    }
};

//! 動作の名前と、その動作のアニメーションの項目に入れるクリップの道
using TestActionClips = std::vector<std::pair<std::string, std::string>>;

//! 試しの間だけ、自機の .actor の動作のアニメーションの項目を clips のクリップに替える。無い動作は足す
//! 替えた動作の事象はアニメーションの項目 1 つだけになる。自機を作る前に作る
class ScopedPlayerActionClips
{
public:
    explicit ScopedPlayerActionClips(TestActionClips clips)
        : m_edit("PlayerActor", [&clips](nlohmann::json& actorFile) {
              std::vector<NS::ActorActionInfo> actions = NS::ReadActorActions("PlayerActor", actorFile);
              for (const std::pair<std::string, std::string>& clip : clips)
              {
                  NS::ActionAnimItem item;
                  NS::LoadAssetField(item.clip, clip.second);
                  NS::ActorActionInfo* target = nullptr;
                  for (NS::ActorActionInfo& action : actions)
                  {
                      if (action.name == clip.first)
                      {
                          target = &action;
                      }
                  }
                  if (target == nullptr)
                  {
                      target = &actions.emplace_back();
                      target->name = clip.first;
                  }
                  target->events = {NS::ActorActionEvent{.item = item, .start = 0, .length = 1}};
              }
              NS::WriteActorActions(actorFile, actions);
          })
    {
    }

private:
    ScopedActorFileEdit m_edit;
};
