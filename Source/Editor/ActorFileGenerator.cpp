#include "Editor/ActorFileGenerator.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Windows/Filesystem.h"

#include <format>
#include <memory>
#include <optional>

namespace NS
{
    namespace Editor
    {
        nlohmann::json CompleteActorFile(const nlohmann::json& actorFile, std::vector<std::string>& added)
        {
            if (!ActorFileBase(actorFile).empty())
            {
                return actorFile;
            }
            const std::unique_ptr<SceneActor> actor = CreateActorWithCodeValues(ActorFileClass(actorFile));
            if (actor == nullptr)
            {
                return actorFile;
            }

            nlohmann::json full = actorFile;
            nlohmann::json& parts = ObjectJsonClassObjs(full);
            ForEachReflectedClassObj(*actor, [&](const ClassObjInfo& row, const Object* part) {
                // 根の姿勢はシーンに置いたアクターの物
                if (part == &actor->Root())
                {
                    parts.erase(row.key);
                    return;
                }
                nlohmann::json& fields = parts[row.key];
                if (!fields.is_object())
                {
                    fields = nlohmann::json::object();
                }
                const nlohmann::json code = SerializeClassObjFields(*part);
                for (nlohmann::json::const_iterator field = code.begin(); field != code.end(); ++field)
                {
                    const FieldDesc* desc = FindField(part->GetReflection(), field.key());
                    if (desc == nullptr || desc->type == FieldType::ActorRef || fields.contains(field.key()))
                    {
                        continue;
                    }
                    fields[field.key()] = field.value();
                    added.push_back(std::format("{}/{}", row.key, field.key()));
                }
            });
            return full;
        }

        int GenerateActorFiles(const std::string& directory)
        {
            if (!OS::FileSystem::IsDirectory(directory))
            {
                return 0;
            }
            int written = 0;
            for (const std::string& path : OS::FileSystem::ListFiles(directory, ".actor"))
            {
                const std::optional<nlohmann::json> actorFile = ReadActorFileAt(path);
                if (!actorFile.has_value())
                {
                    continue;
                }
                std::vector<std::string> added;
                const nlohmann::json full = CompleteActorFile(*actorFile, added);
                if (added.empty() || !WriteActorFileAt(path, full))
                {
                    continue;
                }
                std::string items = added.front();
                for (std::size_t i = 1; i < added.size(); ++i)
                {
                    items += "、" + added[i];
                }
                NS_LOG_INFO(App, ".actor {} にコードの値で {} 項目を書き足した: {}", path, added.size(), items);
                ++written;
            }
            return written;
        }
    } // namespace Editor
} // namespace NS
