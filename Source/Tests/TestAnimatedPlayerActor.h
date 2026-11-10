#pragma once

#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/Action/ActorActionInfo.h"
#include "NSlib/CoreObject/ClassObjs/Animation.h"
#include "NSlib/CoreObject/Reflection/AssetField.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "TestActorFile.h"
#include "TestSpawn.h"

#include <string>
#include <utility>
#include <vector>

class TestAnimatedPlayerActor final : public PlayerActor
{
public:
    NS_REFLECT_NONE(TestAnimatedPlayerActor, PlayerActor)

protected:
    void OnInit() override
    {
        PlayerActor::OnInit();
        CreateClassObj(m_animation);
    }
};

//! 自機の今の .actor の値で、アニメーションを持つ自機を .actor を通してシーンに入れる
//! 自機の .actor をクラスだけ替え、アニメーションの部品の欄を足して TestAnimatedPlayerActor の .actor に敷く。作った後も動作を
//! .actor の名前で読み直すので、敷いた物は戻さない。次に作る時は、その時の自機の .actor で敷き直す
//! 登録は actor_body_time_test.cpp の NS_CLASS
inline TestAnimatedPlayerActor* SpawnTestAnimatedPlayer(NS::Scene& scene, std::string_view name = {})
{
    const nlohmann::json* player = NS::ActorFileLibrary::Get().Find("PlayerActor");
    EXPECT_NE(player, nullptr);
    if (player == nullptr)
    {
        return nullptr;
    }
    nlohmann::json actorFile = *player;
    NS::SetActorFileClass(actorFile, "TestAnimatedPlayerActor");
    // 自機の .actor に無いアニメーションの部品の欄は、コードの値で書く
    NS::ActorJsonClassObjs(actorFile)["Animation"] =
        NS::ActorJsonClassObjs(CodeValuesActorFile("TestAnimatedPlayerActor"))["Animation"];
    EXPECT_TRUE(NS::ActorFileLibrary::Get().Set("TestAnimatedPlayerActor", std::move(actorFile)));
    return SpawnFromActorFile<TestAnimatedPlayerActor>(scene, "TestAnimatedPlayerActor", name);
}

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
                  NS::ActionAnimInfo item;
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
