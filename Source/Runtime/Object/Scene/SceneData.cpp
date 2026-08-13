#include "Runtime/Object/Scene/SceneData.h"

#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/detail/Crc32.h"

#include <span>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace NS::Object
{
    namespace
    {
        //! POD 値を std::byte span に見立てて CRC32 に流す
        template <typename T> std::uint32_t UpdateWith(std::uint32_t crc, const T& value) noexcept
        {
            static_assert(std::is_trivially_copyable_v<T>, "UpdateWith expects trivially copyable type");
            const auto* raw = reinterpret_cast<const std::byte*>(&value);
            return detail::Crc32Update(crc, std::span<const std::byte>(raw, sizeof(T)));
        }

        //! 長さ + 中身バイトの順で文字列を hash する
        std::uint32_t UpdateWithString(std::uint32_t crc, const std::string& text) noexcept
        {
            crc = UpdateWith(crc, static_cast<std::uint64_t>(text.size()));
            if (!text.empty())
            {
                const auto* raw = reinterpret_cast<const std::byte*>(text.data());
                crc = detail::Crc32Update(crc, std::span<const std::byte>(raw, text.size()));
            }
            return crc;
        }

        //! JSON 木を「型のタグ + 値」の順で再帰 hash する。 object はキー昇順で並ぶので決定的
        //! 整数は符号付きと符号無しを 1 つのタグにまとめる。 保存は符号付きで組み読込は非負を符号無しで返すので、
        //! 分けると往復で CRC が変わり読込直後から dirty になる
        std::uint32_t UpdateWithJson(std::uint32_t crc, const nlohmann::json& value) noexcept
        {
            if (value.is_object())
            {
                crc = UpdateWith(crc, static_cast<std::uint8_t>(1));
                crc = UpdateWith(crc, static_cast<std::uint64_t>(value.size()));
                for (auto it = value.begin(); it != value.end(); ++it)
                {
                    crc = UpdateWithString(crc, it.key());
                    crc = UpdateWithJson(crc, it.value());
                }
                return crc;
            }
            if (value.is_array())
            {
                crc = UpdateWith(crc, static_cast<std::uint8_t>(2));
                crc = UpdateWith(crc, static_cast<std::uint64_t>(value.size()));
                for (const nlohmann::json& element : value)
                    crc = UpdateWithJson(crc, element);
                return crc;
            }
            if (value.is_string())
            {
                crc = UpdateWith(crc, static_cast<std::uint8_t>(3));
                return UpdateWithString(crc, value.get_ref<const std::string&>());
            }
            if (value.is_boolean())
            {
                crc = UpdateWith(crc, static_cast<std::uint8_t>(4));
                return UpdateWith(crc, value.get<bool>());
            }
            if (value.is_number_integer())
            {
                // is_number_integer は符号付き / 無しの両方に真
                crc = UpdateWith(crc, static_cast<std::uint8_t>(5));
                return UpdateWith(crc, value.get<std::int64_t>());
            }
            if (value.is_number_float())
            {
                crc = UpdateWith(crc, static_cast<std::uint8_t>(6));
                return UpdateWith(crc, value.get<double>());
            }
            // null / binary / discarded は tag だけ流す
            return UpdateWith(crc, static_cast<std::uint8_t>(0));
        }

        //! ObjectData のスカラ部を宣言順で hash し、 続けて components を hash する
        std::uint32_t UpdateWithObject(std::uint32_t crc, const ObjectData& object) noexcept
        {
            // スカラ部を宣言順で hash。 transform は components 内の TransformComponent として混ざる
            crc = UpdateWith(crc, object.objectId);
            crc = UpdateWithString(crc, object.className);
            crc = UpdateWithString(crc, object.name);
            crc = UpdateWith(crc, object.parentId);
            crc = UpdateWith(crc, object.order);
            crc = UpdateWith(crc, object.active);
            return UpdateWithJson(crc, object.components);
        }

    } // namespace

    std::uint32_t SceneData::ComputeCrc32() const noexcept
    {
        std::uint32_t crc = detail::k_Crc32Init;

        // objects の要素数を先に流す。 末尾へ足しただけでも CRC が変わる
        const std::uint64_t objectCount = static_cast<std::uint64_t>(objects.size());
        crc = UpdateWith(crc, objectCount);
        for (const auto& object : objects)
        {
            crc = UpdateWithObject(crc, object);
        }

        // 環境は見た目を確定する永続データなので、 変化が dirty 検知に必ず出るよう hash する
        crc = UpdateWithString(crc, environment.skyboxCubemapPath);

        // nextObjectId は意図して hash しない。採番カウンタは undo で巻き戻さないため、入れると
        // 「置いて undo しただけで dirty」が残り続ける。カウンタだけが進んだ状態は保存しなくても
        // 未保存 object への参照が残らず整合が壊れないので、内容の変化検知からは外す

        return detail::Crc32Finalize(crc);
    }

    std::size_t FindObjectIndexById(const SceneData& scene, std::uint32_t id) noexcept
    {
        if (id == k_NoObjectId)
            return k_NoObjectIndex;
        for (std::size_t i = 0; i < scene.objects.size(); ++i)
        {
            if (scene.objects[i].objectId == id)
                return i;
        }
        return k_NoObjectIndex;
    }

    void EnsureUniqueObjectIds(SceneData& scene)
    {
        // 先にカウンタを既存最大 id の先へ進め、これから振る id が既存と衝突しないようにする
        for (const ObjectData& object : scene.objects)
        {
            if (object.objectId >= scene.nextObjectId)
                scene.nextObjectId = object.objectId + 1;
        }

        // component の id も同じ空間なので、カウンタを進める段から一緒に見る
        for (const ObjectData& object : scene.objects)
        {
            if (!object.components.is_array())
                continue;
            for (const nlohmann::json& entry : object.components)
            {
                const std::uint32_t id = ComponentEntryId(entry);
                if (id >= scene.nextObjectId)
                    scene.nextObjectId = id + 1;
            }
        }

        // 未割当や重複は手編集・複製バグで入り得る。先勝ちで後続へ新 id を振る
        std::unordered_set<std::uint32_t> seen;
        seen.reserve(scene.objects.size());
        for (ObjectData& object : scene.objects)
        {
            if (object.objectId == 0 || !seen.insert(object.objectId).second)
            {
                object.objectId = scene.nextObjectId++;
                seen.insert(object.objectId);
            }
        }

        // component も同じ表で見るので、object と component の間でも id が重ならない
        for (ObjectData& object : scene.objects)
        {
            if (!object.components.is_array())
                continue;
            for (nlohmann::json& entry : object.components)
            {
                const std::uint32_t id = ComponentEntryId(entry);
                if (id == 0 || !seen.insert(id).second)
                {
                    const std::uint32_t fresh = scene.nextObjectId++;
                    SetComponentEntryId(entry, fresh);
                    seen.insert(fresh);
                }
            }
        }
    }

    std::size_t PruneDanglingObjectRefs(SceneData& scene)
    {
        std::unordered_set<std::uint32_t> validIds;
        validIds.reserve(scene.objects.size());
        for (const ObjectData& object : scene.objects)
            validIds.insert(object.objectId);

        std::size_t prunedCount = 0;
        for (ObjectData& object : scene.objects)
        {
            if (!object.components.is_array())
                continue;
            for (nlohmann::json& entry : object.components)
            {
                if (!entry.is_object())
                    continue;
                const auto fieldsIt = entry.find("fields");
                if (fieldsIt == entry.end() || !fieldsIt->is_object())
                    continue;
                for (auto& fieldValue : *fieldsIt)
                {
                    if (!fieldValue.is_object())
                        continue;
                    const auto refIt = fieldValue.find("ref");
                    if (refIt == fieldValue.end() || !refIt->is_number_unsigned())
                        continue;
                    const std::uint32_t id = refIt->get<std::uint32_t>();
                    if (id == k_NoObjectId || validIds.contains(id))
                        continue;
                    *refIt = 0u;
                    ++prunedCount;
                }
            }
        }
        return prunedCount;
    }

    std::size_t PruneInvalidParents(SceneData& scene)
    {
        std::unordered_map<std::uint32_t, std::size_t> indexById;
        indexById.reserve(scene.objects.size());
        for (std::size_t i = 0; i < scene.objects.size(); ++i)
            indexById.emplace(scene.objects[i].objectId, i);

        std::size_t prunedCount = 0;
        for (ObjectData& object : scene.objects)
        {
            if (object.parentId == k_NoObjectId)
                continue;

            bool valid = object.parentId != object.objectId && indexById.contains(object.parentId);
            // 祖先を辿って自分へ戻れば循環。 その場で root へ落とすので、輪の残りは正当な親子として通る
            std::uint32_t ancestor = object.parentId;
            for (std::size_t step = 0; valid && step < scene.objects.size(); ++step)
            {
                const auto it = indexById.find(ancestor);
                if (it == indexById.end())
                {
                    valid = false;
                    break;
                }
                ancestor = scene.objects[it->second].parentId;
                if (ancestor == k_NoObjectId)
                    break;
                if (ancestor == object.objectId)
                    valid = false;
            }

            if (!valid)
            {
                object.parentId = k_NoObjectId;
                ++prunedCount;
            }
        }
        return prunedCount;
    }

    std::vector<ObjectRefLocation> FindReferencesTo(const SceneData& scene, std::uint32_t targetId)
    {
        std::vector<ObjectRefLocation> result;
        if (targetId == k_NoObjectId)
            return result;
        for (const ObjectData& object : scene.objects)
        {
            if (!object.components.is_array())
                continue;
            for (std::size_t c = 0; c < object.components.size(); ++c)
            {
                const nlohmann::json* fields = ComponentEntryFields(object.components[c]);
                if (fields == nullptr)
                    continue;
                for (auto it = fields->begin(); it != fields->end(); ++it)
                {
                    const nlohmann::json& fieldValue = it.value();
                    if (!fieldValue.is_object())
                        continue;
                    const auto refIt = fieldValue.find("ref");
                    if (refIt == fieldValue.end() || !refIt->is_number_unsigned())
                        continue;
                    if (refIt->get<std::uint32_t>() != targetId)
                        continue;
                    result.push_back(ObjectRefLocation{object.objectId, c, it.key()});
                }
            }
        }
        return result;
    }

} // namespace NS::Object
