#include "Runtime/Object/Scene/SceneJson.h"

#include "Runtime/Core/Logger.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/ObjectName.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Platform/Filesystem.h"

#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace NS::Obj
{
    namespace
    {
        //! 保存形式のバージョン。形式を変えたら上げ、読込は一致のみ受け付ける
        //! 3: リフレクション欄名を日本語化。旧欄名のファイルを黙って既定値で読まないための引き上げ
        //! 4: transform の回転を Euler 度 3 要素から クォータニオン 4 要素の 1 欄へ
        constexpr int k_FormatVersion = 4;

        //! 読込時の上限。巨大 size / 要素数による メモリ枯渇を防ぐ
        constexpr std::size_t k_MaxSceneFileBytes = 16u * 1024u * 1024u;
        constexpr std::size_t k_MaxObjectCount = 100'000u;

        //! ファイルの配置物 1 体を、知っている欄だけの形へ整える。未知の欄は捨て、fields の中身は素通し
        nlohmann::json NormalizeObject(const nlohmann::json& json)
        {
            nlohmann::json object = MakeObjectJson();
            if (!json.is_object())
            {
                return object;
            }

            const nlohmann::json::const_iterator idIt = json.find("id");
            if (idIt != json.end() && idIt->is_number_unsigned())
            {
                SetObjectJsonId(object, idIt->get<std::uint32_t>());
            }
            SetObjectJsonClass(object, ObjectJsonClass(json));
            SetObjectJsonName(object, ObjectJsonName(json));
            SetObjectJsonParent(object, ObjectJsonParent(json));
            // 欄が無い古いファイルは有効として読む
            SetObjectJsonActive(object, ObjectJsonActive(json));

            nlohmann::json& components = ObjectJsonComponents(object);
            for (const nlohmann::json& componentJson : ObjectJsonComponents(json))
            {
                if (!componentJson.is_object())
                {
                    continue;
                }
                nlohmann::json fields = nlohmann::json::object();
                const nlohmann::json::const_iterator fieldsIt = componentJson.find("fields");
                if (fieldsIt != componentJson.end() && fieldsIt->is_object())
                {
                    fields = *fieldsIt;
                }
                nlohmann::json entry = MakeComponentEntry(ComponentEntryType(componentJson), std::move(fields));
                // id を落とすと読むたびに振り直しになり、名指ししている参照が外れる
                SetComponentEntryId(entry, ComponentEntryId(componentJson));
                // 名前はファイルの参照がコンポーネントを名指しするのに使う
                SetComponentEntryName(entry, ComponentEntryName(componentJson));
                // 保存側は書き出すので、ここで落とすと切った component が開くたびに有効へ戻る
                SetComponentEntryEnabled(entry, ComponentEntryEnabled(componentJson));
                components.push_back(std::move(entry));
            }
            // 手編集で transform エントリを欠くファイルにも 1 つ保証し、以降の transform 読取を成立させる
            (void)EnsureTransformComponent(object);
            return object;
        }

        // ファイルの参照は相手の名前で書く。空と重複した名前は相手が 1 つに決まらないので id のまま残す
        // Component は持ち主の中の名前で書く。読込は持ち主を決めてから、その中で名前を引く
        void WriteRefsByName(nlohmann::json& scene)
        {
            nlohmann::json& objects = SceneJsonObjects(scene);
            std::unordered_map<std::string, int> nameCounts;
            for (const nlohmann::json& object : objects)
            {
                ++nameCounts[std::string{ObjectJsonName(object)}];
            }
            std::unordered_map<std::uint32_t, std::string> namesById;
            std::unordered_map<std::uint32_t, std::string> componentNamesById;
            for (const nlohmann::json& object : objects)
            {
                const std::string name{ObjectJsonName(object)};
                if (!name.empty() && nameCounts[name] == 1)
                {
                    namesById.emplace(ObjectJsonId(object), name);
                }
                for (const nlohmann::json& entry : ObjectJsonComponents(object))
                {
                    const std::string_view componentName = ComponentEntryName(entry);
                    if (!componentName.empty())
                    {
                        componentNamesById.emplace(ComponentEntryId(entry), std::string{componentName});
                    }
                }
            }
            for (nlohmann::json& object : objects)
            {
                ForEachRefValue(object, [&namesById, &componentNamesById](nlohmann::json& value) {
                    nlohmann::json& ref = value["ref"];
                    if (ref.is_number_unsigned())
                    {
                        const std::unordered_map<std::uint32_t, std::string>::iterator it =
                            namesById.find(ref.get<std::uint32_t>());
                        if (it != namesById.end())
                        {
                            ref = it->second;
                        }
                    }
                    const nlohmann::json::iterator componentIt = value.find("component");
                    if (componentIt != value.end() && componentIt->is_number_unsigned())
                    {
                        const std::unordered_map<std::uint32_t, std::string>::iterator it =
                            componentNamesById.find(componentIt->get<std::uint32_t>());
                        if (it != componentNamesById.end())
                        {
                            *componentIt = it->second;
                        }
                    }
                });
            }
        }

        // ファイルの参照は相手の名前で書かれている。名前を一意にした後で id へ直す。旧形式の数値はそのまま通す
        // Component の名前は持ち主の中で一意なので、持ち主の id を決めてからその中で引く
        void ReadRefsByName(nlohmann::json& scene)
        {
            nlohmann::json& objects = SceneJsonObjects(scene);
            std::unordered_map<std::string, std::uint32_t> idsByName;
            std::unordered_map<std::uint32_t, std::unordered_map<std::string, std::uint32_t>> componentIdsByObject;
            for (const nlohmann::json& object : objects)
            {
                const std::uint32_t objectId = ObjectJsonId(object);
                idsByName.emplace(std::string{ObjectJsonName(object)}, objectId);
                std::unordered_map<std::string, std::uint32_t>& componentIds = componentIdsByObject[objectId];
                for (const nlohmann::json& entry : ObjectJsonComponents(object))
                {
                    componentIds.emplace(std::string{ComponentEntryName(entry)}, ComponentEntryId(entry));
                }
            }
            for (nlohmann::json& object : objects)
            {
                ForEachRefValue(object, [&idsByName, &componentIdsByObject](nlohmann::json& value) {
                    nlohmann::json& ref = value["ref"];
                    if (ref.is_string())
                    {
                        const std::unordered_map<std::string, std::uint32_t>::iterator it =
                            idsByName.find(ref.get<std::string>());
                        if (it == idsByName.end())
                        {
                            ref = k_NoObjectId; // 居ない名前は未設定へ戻す
                        }
                        else
                        {
                            ref = it->second;
                        }
                    }
                    const nlohmann::json::iterator componentIt = value.find("component");
                    if (componentIt == value.end() || !componentIt->is_string())
                    {
                        return;
                    }
                    std::uint32_t componentId = 0;
                    if (ref.is_number_unsigned())
                    {
                        const std::unordered_map<std::uint32_t, std::unordered_map<std::string, std::uint32_t>>::iterator
                            owner = componentIdsByObject.find(ref.get<std::uint32_t>());
                        if (owner != componentIdsByObject.end())
                        {
                            const std::unordered_map<std::string, std::uint32_t>::iterator it =
                                owner->second.find(componentIt->get<std::string>());
                            if (it != owner->second.end())
                            {
                                componentId = it->second;
                            }
                        }
                    }
                    // 居ない名前は未設定。持ち主だけ残すと、どれも指していない参照が生きて見える
                    *componentIt = componentId;
                    if (componentId == 0)
                    {
                        ref = k_NoObjectId;
                    }
                });
            }
        }
    } // namespace

    nlohmann::json MakeSceneJson()
    {
        nlohmann::json scene = nlohmann::json::object();
        scene["version"] = k_FormatVersion;
        nlohmann::json environment = nlohmann::json::object();
        environment["skybox"] = "";
        scene["environment"] = std::move(environment);
        scene["objects"] = nlohmann::json::array();
        scene["nextObjectId"] = 1u;
        return scene;
    }

    const nlohmann::json& SceneJsonObjects(const nlohmann::json& scene) noexcept
    {
        static const nlohmann::json k_Empty = nlohmann::json::array();
        if (!scene.is_object())
        {
            return k_Empty;
        }
        const nlohmann::json::const_iterator it = scene.find("objects");
        if (it == scene.end() || !it->is_array())
        {
            return k_Empty;
        }
        return *it;
    }

    nlohmann::json& SceneJsonObjects(nlohmann::json& scene)
    {
        if (!scene.is_object())
        {
            scene = MakeSceneJson();
        }
        nlohmann::json& objects = scene["objects"];
        if (!objects.is_array())
        {
            objects = nlohmann::json::array();
        }
        return objects;
    }

    std::uint32_t SceneJsonNextObjectId(const nlohmann::json& scene) noexcept
    {
        if (!scene.is_object())
        {
            return 1;
        }
        const nlohmann::json::const_iterator it = scene.find("nextObjectId");
        if (it == scene.end() || !it->is_number_unsigned())
        {
            return 1;
        }
        return it->get<std::uint32_t>();
    }

    void SetSceneJsonNextObjectId(nlohmann::json& scene, std::uint32_t nextObjectId)
    {
        if (!scene.is_object())
        {
            scene = MakeSceneJson();
        }
        scene["nextObjectId"] = nextObjectId;
    }

    std::string_view SceneJsonSkybox(const nlohmann::json& scene) noexcept
    {
        if (!scene.is_object())
        {
            return {};
        }
        const nlohmann::json::const_iterator environmentIt = scene.find("environment");
        if (environmentIt == scene.end() || !environmentIt->is_object())
        {
            return {};
        }
        const nlohmann::json::const_iterator skyboxIt = environmentIt->find("skybox");
        if (skyboxIt == environmentIt->end() || !skyboxIt->is_string())
        {
            return {};
        }
        return skyboxIt->get_ref<const std::string&>();
    }

    void SetSceneJsonSkybox(nlohmann::json& scene, std::string_view path)
    {
        if (!scene.is_object())
        {
            scene = MakeSceneJson();
        }
        nlohmann::json& environment = scene["environment"];
        if (!environment.is_object())
        {
            environment = nlohmann::json::object();
        }
        environment["skybox"] = std::string{path};
    }

    std::size_t FindObjectIndexById(const nlohmann::json& scene, std::uint32_t id) noexcept
    {
        if (id == k_NoObjectId)
        {
            return k_NoObjectIndex;
        }
        const nlohmann::json& objects = SceneJsonObjects(scene);
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            if (ObjectJsonId(objects[i]) == id)
            {
                return i;
            }
        }
        return k_NoObjectIndex;
    }

    void EnsureUniqueObjectIds(nlohmann::json& scene)
    {
        nlohmann::json& objects = SceneJsonObjects(scene);

        // 先にカウンタを既存最大 id の先へ進め、これから振る id が既存と衝突しないようにする
        // component の id も同じ空間なので、カウンタを進める段から一緒に見る
        std::uint32_t nextObjectId = SceneJsonNextObjectId(scene);
        for (const nlohmann::json& object : objects)
        {
            if (ObjectJsonId(object) >= nextObjectId)
            {
                nextObjectId = ObjectJsonId(object) + 1;
            }
            for (const nlohmann::json& entry : ObjectJsonComponents(object))
            {
                if (ComponentEntryId(entry) >= nextObjectId)
                {
                    nextObjectId = ComponentEntryId(entry) + 1;
                }
            }
        }

        // 未割当や重複は手編集・複製で入り得る。先勝ちで後続へ新 id を振る
        std::unordered_set<std::uint32_t> seen;
        seen.reserve(objects.size());
        for (nlohmann::json& object : objects)
        {
            const std::uint32_t id = ObjectJsonId(object);
            if (id == 0 || !seen.insert(id).second)
            {
                const std::uint32_t fresh = nextObjectId++;
                SetObjectJsonId(object, fresh);
                seen.insert(fresh);
            }
        }

        // component も同じ表で見るので、配置物と component の間でも id が重ならない
        for (nlohmann::json& object : objects)
        {
            for (nlohmann::json& entry : ObjectJsonComponents(object))
            {
                const std::uint32_t id = ComponentEntryId(entry);
                if (id == 0 || !seen.insert(id).second)
                {
                    const std::uint32_t fresh = nextObjectId++;
                    SetComponentEntryId(entry, fresh);
                    seen.insert(fresh);
                }
            }
        }
        SetSceneJsonNextObjectId(scene, nextObjectId);

        EnsureUniqueObjectNames(scene);
    }

    void EnsureUniqueObjectNames(nlohmann::json& scene)
    {
        nlohmann::json& objects = SceneJsonObjects(scene);

        // 付いている名前を先に全部押さえる。空の物へ先に番号を振ると、後ろの手書きの名前と重なる
        std::unordered_set<std::string> used;
        used.reserve(objects.size());
        std::vector<nlohmann::json*> pending;
        for (nlohmann::json& object : objects)
        {
            const std::string name{ObjectJsonName(object)};
            if (name.empty() || !used.insert(name).second)
            {
                pending.push_back(&object);
            }
        }
        for (nlohmann::json* object : pending)
        {
            const std::string unique = MakeUniqueObjectName(ObjectJsonName(*object), used);
            SetObjectJsonName(*object, unique);
            used.insert(unique);
        }

        // component の名前は配置物の中で一意にする。名前の無い古いデータは型名から付ける
        for (nlohmann::json& object : objects)
        {
            nlohmann::json& components = ObjectJsonComponents(object);
            std::unordered_set<std::string> usedComponentNames;
            usedComponentNames.reserve(components.size());
            std::vector<nlohmann::json*> pendingEntries;
            for (nlohmann::json& entry : components)
            {
                const std::string name{ComponentEntryName(entry)};
                if (name.empty() || !usedComponentNames.insert(name).second)
                {
                    pendingEntries.push_back(&entry);
                }
            }
            for (nlohmann::json* entry : pendingEntries)
            {
                std::string_view base = ComponentEntryName(*entry);
                if (base.empty())
                {
                    base = ComponentEntryType(*entry);
                }
                const std::string unique = MakeUniqueObjectName(base, usedComponentNames);
                SetComponentEntryName(*entry, unique);
                usedComponentNames.insert(unique);
            }
        }
    }

    std::size_t PruneDanglingObjectRefs(nlohmann::json& scene)
    {
        nlohmann::json& objects = SceneJsonObjects(scene);

        // 配置物ごとに、持っている component の id を控える。ComponentRef は持ち主の中に居るかまで見る
        std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>> componentIdsByObject;
        componentIdsByObject.reserve(objects.size());
        for (const nlohmann::json& object : objects)
        {
            std::unordered_set<std::uint32_t>& componentIds = componentIdsByObject[ObjectJsonId(object)];
            for (const nlohmann::json& entry : ObjectJsonComponents(object))
            {
                componentIds.insert(ComponentEntryId(entry));
            }
        }

        std::size_t prunedCount = 0;
        for (nlohmann::json& object : objects)
        {
            ForEachRefValue(object, [&componentIdsByObject, &prunedCount](nlohmann::json& value) {
                nlohmann::json& ref = value["ref"];
                if (!ref.is_number_unsigned())
                {
                    return;
                }
                const std::uint32_t objectId = ref.get<std::uint32_t>();
                const std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>>::const_iterator owner =
                    componentIdsByObject.find(objectId);
                const nlohmann::json::iterator componentIt = value.find("component");
                if (componentIt == value.end())
                {
                    // ObjectRef。未設定か、居る相手なら残す
                    if (objectId == k_NoObjectId || owner != componentIdsByObject.end())
                    {
                        return;
                    }
                    ref = 0u;
                    ++prunedCount;
                    return;
                }

                // ComponentRef。未設定か、持ち主の中に居る Component なら残す
                if (!componentIt->is_number_unsigned())
                {
                    return;
                }
                const std::uint32_t componentId = componentIt->get<std::uint32_t>();
                if (componentId == 0 && objectId == k_NoObjectId)
                {
                    return;
                }
                if (componentId != 0 && owner != componentIdsByObject.end() && owner->second.contains(componentId))
                {
                    return;
                }
                ref = 0u;
                *componentIt = 0u;
                ++prunedCount;
            });
        }
        return prunedCount;
    }

    std::size_t PruneInvalidParents(nlohmann::json& scene)
    {
        nlohmann::json& objects = SceneJsonObjects(scene);
        std::unordered_map<std::uint32_t, std::size_t> indexById;
        indexById.reserve(objects.size());
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            indexById.emplace(ObjectJsonId(objects[i]), i);
        }

        std::size_t prunedCount = 0;
        for (nlohmann::json& object : objects)
        {
            const std::uint32_t objectId = ObjectJsonId(object);
            const std::uint32_t parentId = ObjectJsonParent(object);
            if (parentId == k_NoObjectId)
            {
                continue;
            }

            bool valid = parentId != objectId && indexById.contains(parentId);
            // 祖先を辿って自分へ戻れば循環。その場で root へ落とすので、輪の残りは正当な親子として通る
            std::uint32_t ancestor = parentId;
            for (std::size_t step = 0; valid && step < objects.size(); ++step)
            {
                const std::unordered_map<std::uint32_t, std::size_t>::iterator it = indexById.find(ancestor);
                if (it == indexById.end())
                {
                    valid = false;
                    break;
                }
                ancestor = ObjectJsonParent(objects[it->second]);
                if (ancestor == k_NoObjectId)
                {
                    break;
                }
                if (ancestor == objectId)
                {
                    valid = false;
                }
            }

            if (!valid)
            {
                SetObjectJsonParent(object, k_NoObjectId);
                ++prunedCount;
            }
        }
        return prunedCount;
    }

    std::string SerializeSceneToJson(const nlohmann::json& scene)
    {
        nlohmann::json out = MakeSceneJson();
        SetSceneJsonSkybox(out, SceneJsonSkybox(scene));
        SetSceneJsonNextObjectId(out, SceneJsonNextObjectId(scene));
        out["objects"] = SceneJsonObjects(scene);
        WriteRefsByName(out);

        // 不正 UTF-8 は replace で握り、dump が例外を投げないようにして noexcept 経路を保つ
        return out.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    bool DeserializeSceneFromJson(nlohmann::json& outScene, std::string_view jsonText)
    {
        outScene = MakeSceneJson();

        const nlohmann::json root = nlohmann::json::parse(jsonText, nullptr, false);
        if (root.is_discarded())
        {
            NS_LOG_ERROR(Scene, "DeserializeSceneFromJson: JSON parse に失敗");
            return false;
        }
        if (!root.is_object())
        {
            NS_LOG_ERROR(Scene, "DeserializeSceneFromJson: ルートが object でない");
            return false;
        }

        // 小数の version が切り捨てで一致に化けないよう、version の形式検査だけは整数のみ受ける
        const nlohmann::json::const_iterator versionIt = root.find("version");
        if (versionIt == root.end() || !versionIt->is_number_integer() || versionIt->get<int>() != k_FormatVersion)
        {
            NS_LOG_ERROR(Scene, "DeserializeSceneFromJson: version 欄が整数の {} と一致しない", k_FormatVersion);
            return false;
        }

        const nlohmann::json& objects = SceneJsonObjects(root);
        if (objects.size() > k_MaxObjectCount)
        {
            NS_LOG_ERROR(
                Scene, "DeserializeSceneFromJson: object 数 {} が上限 {} を超過", objects.size(), k_MaxObjectCount);
            return false;
        }
        nlohmann::json& outObjects = SceneJsonObjects(outScene);
        for (const nlohmann::json& objectJson : objects)
        {
            outObjects.push_back(NormalizeObject(objectJson));
        }

        // environment 欄は skybox だけを所有する。旧形式の照明の欄は DirectionalLight へ移ったので読み飛ばす
        SetSceneJsonSkybox(outScene, SceneJsonSkybox(root));
        SetSceneJsonNextObjectId(outScene, SceneJsonNextObjectId(root));

        // 手編集ファイルは id 未割当・重複があり得る。読込直後に必ず一意化し、以降の経路は id を信頼できる
        EnsureUniqueObjectIds(outScene);
        ReadRefsByName(outScene);

        // 手編集や参照先削除で宙に浮いた参照は入口で未設定へ戻す。実行時は id 照合の失敗を考えずに済む
        const std::size_t prunedRefs = PruneDanglingObjectRefs(outScene);
        if (prunedRefs > 0)
        {
            NS_LOG_WARN(Scene, "存在しない object を指す参照を {} 件未設定に戻した", prunedRefs);
        }
        // 循環した親を残すと world 変換の再帰が止まらないので入口で断つ
        const std::size_t prunedParents = PruneInvalidParents(outScene);
        if (prunedParents > 0)
        {
            NS_LOG_WARN(Scene, "辿れない親を持つ object を {} 件 root に戻した", prunedParents);
        }
        return true;
    }

    bool SaveSceneToJsonFile(const nlohmann::json& scene, std::string_view path) noexcept
    {
        // 保存前の上限ガード
        const std::size_t objectCount = SceneJsonObjects(scene).size();
        if (objectCount > k_MaxObjectCount)
        {
            NS_LOG_ERROR(Scene, "SaveSceneToJsonFile: object 数が上限超過 ({} > {})", objectCount, k_MaxObjectCount);
            return false;
        }
        const std::string text = SerializeSceneToJson(scene);
        if (text.size() > k_MaxSceneFileBytes)
        {
            NS_LOG_ERROR(Scene,
                         "SaveSceneToJsonFile: 出力 file が上限 ({} byte) を超過: {} byte",
                         k_MaxSceneFileBytes,
                         text.size());
            return false;
        }

        const std::byte* raw = reinterpret_cast<const std::byte*>(text.data());
        return ::NS::Platform::FileSystem::WriteAllBytes(path, std::span<const std::byte>(raw, text.size()));
    }

    bool LoadSceneFromJsonFile(nlohmann::json& outScene, std::string_view path) noexcept
    {
        outScene = MakeSceneJson();

        std::optional<std::string> textOpt = ::NS::Platform::FileSystem::ReadAllText(path);
        if (!textOpt.has_value())
        {
            return false;
        }

        if (textOpt->size() > k_MaxSceneFileBytes)
        {
            NS_LOG_ERROR(Scene,
                         "LoadSceneFromJsonFile: file が上限 ({} byte) を超えるので reject: {}",
                         k_MaxSceneFileBytes,
                         path);
            return false;
        }

        if (!DeserializeSceneFromJson(outScene, *textOpt))
        {
            outScene = MakeSceneJson();
            return false;
        }
        return true;
    }
} // namespace NS::Obj
