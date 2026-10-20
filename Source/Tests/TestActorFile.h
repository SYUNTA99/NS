#pragma once

#include "NSlib/CoreObject/Reflection/ActorFile.h"

#include <string>
#include <string_view>
#include <utility>

//! 中身がクラスだけの .actor の文書。欄の値はコードの既定のまま
inline nlohmann::json ClassOnlyActorFile(std::string_view className)
{
    nlohmann::json actorFile = nlohmann::json::object();
    NS::SetActorFileClass(actorFile, className);
    return actorFile;
}

//! @brief 試しの間だけ name の .actor を読んだ物として敷き、壊す時に外す
//! @details ファイルへは書かない。Assets/Actors に .actor の無い試しのクラスを配置物から作る時に使う
//! 置き場を差し替える型と使う時は、置き場を差し替えた後に作る。差し替えは読み直すので、前に敷いた物が消える
class ScopedActorFile
{
public:
    ScopedActorFile(std::string_view name, nlohmann::json actorFile) : m_name(name)
    {
        NS::ActorFileLibrary::Get().Set(m_name, std::move(actorFile));
    }

    ~ScopedActorFile() { NS::ActorFileLibrary::Get().Erase(m_name); }

    ScopedActorFile(const ScopedActorFile&) = delete;
    ScopedActorFile& operator=(const ScopedActorFile&) = delete;

private:
    std::string m_name;
};
