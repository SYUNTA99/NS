#include "NSlib/CoreObject/Asset/AssetManager.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/CoreObject/Asset/EffectAsset.h"
#include "NSlib/CoreObject/Asset/EffectCatalog.h"
#include "NSlib/CoreObject/Asset/EffectSet.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <random>
#include <span>

namespace NS
{
    namespace
    {
        // 複製の名前に使えるか。前からある素材は charge.curl のように . で分けた名前なので、. は通す
        // Windows が名前の端の . と空白を黙って落とす名前と、上の階層を指す .. は断る
        [[nodiscard]] bool IsEffectFileName(std::string_view name) noexcept
        {
            return !name.empty() && name.find_first_of("\\/:*?\"<>|") == std::string_view::npos &&
                   name.find("..") == std::string_view::npos && name.front() != '.' && name.back() != '.' &&
                   name.front() != ' ' && name.back() != ' ';
        }

        std::optional<Gfx::EffectResourceDesc> ReadEffectResource(const EffectAssetFile& file, const std::string& root)
        {
            const nlohmann::json& document = file.document;
            if ((document.contains("recordName") && !document["recordName"].is_string()) ||
                !document.contains("source") || !document["source"].is_string() || !document.contains("seed") ||
                !document["seed"].is_number_integer() || document["seed"].get<double>() < 0.0 ||
                document["seed"].get<double>() > std::numeric_limits<uint32>::max())
            {
                NS_LOG_ERROR(Scene, "エフェクト素材 {} のファイルか乱数の基準が不正", file.path);
                return std::nullopt;
            }
            const std::optional<std::string> source =
                OS::FileSystem::ResolveUnder(root, document["source"].get<std::string>());
            if (!source || !OS::FileSystem::Exists(*source))
            {
                NS_LOG_ERROR(Scene, "エフェクト素材 {} の参照先が読めない", file.path);
                return std::nullopt;
            }
            return Gfx::EffectResourceDesc{
                file.id, document.value("recordName", file.label), *source, document["seed"].get<uint32>()};
        }
    } // namespace

    void AssetManager::ReloadEffectAssets()
    {
        m_effectCatalogLoaded = true;
        for (const std::pair<const std::string, std::unique_ptr<EffectAsset>>& entry : m_effectAssets)
        {
            entry.second->Invalidate();
        }
        for (const std::pair<const std::string, std::unique_ptr<EffectSet>>& entry : m_effectSets)
        {
            entry.second->Invalidate();
        }
        for (const EffectAssetFile& file : ReadEffectAssetFiles(m_baseDir))
        {
            if (file.isSet)
            {
                std::unique_ptr<EffectSet>& asset = m_effectSets[file.id];
                if (!asset)
                {
                    asset = std::make_unique<EffectSet>(file.id);
                }
                asset->Replace(file.path, file.document);
                continue;
            }
            const std::optional<Gfx::EffectResourceDesc> resource = ReadEffectResource(file, m_baseDir);
            if (!resource)
            {
                continue;
            }
            std::unique_ptr<EffectAsset>& asset = m_effectAssets[file.id];
            if (!asset)
            {
                asset = std::make_unique<EffectAsset>(file.id);
            }
            asset->Replace(file.path, *resource, file.label);
        }
    }

    EffectAsset* AssetManager::LoadEffectAsset(std::string_view id)
    {
        if (!m_effectCatalogLoaded)
        {
            ReloadEffectAssets();
        }
        std::unique_ptr<EffectAsset>& asset = m_effectAssets[std::string{id}];
        if (!asset)
        {
            asset = std::make_unique<EffectAsset>(std::string{id});
            if (!id.empty())
            {
                for (const EffectAssetFile& file : ReadEffectAssetFiles(m_baseDir))
                {
                    if (file.id == id && !file.isSet)
                    {
                        const std::optional<Gfx::EffectResourceDesc> resource = ReadEffectResource(file, m_baseDir);
                        if (resource)
                        {
                            asset->Replace(file.path, *resource, file.label);
                        }
                        break;
                    }
                }
            }
            if (!id.empty() && !asset->IsValid())
            {
                NS_LOG_ERROR(Scene, "エフェクト素材 {} が見つからない", id);
            }
        }
        return asset.get();
    }

    EffectSet* AssetManager::LoadEffectSet(std::string_view id)
    {
        if (!m_effectCatalogLoaded)
        {
            ReloadEffectAssets();
        }
        std::unique_ptr<EffectSet>& asset = m_effectSets[std::string{id}];
        if (!asset)
        {
            asset = std::make_unique<EffectSet>(std::string{id});
            if (!id.empty())
            {
                for (const EffectAssetFile& file : ReadEffectAssetFiles(m_baseDir))
                {
                    if (file.id == id && file.isSet)
                    {
                        asset->Replace(file.path, file.document);
                        break;
                    }
                }
            }
            if (!id.empty() && !asset->IsValid())
            {
                NS_LOG_ERROR(Scene, "演出セット {} が見つからない", id);
            }
        }
        return asset.get();
    }

    Object* AssetManager::LoadEffectFile(std::string_view path)
    {
        if (!m_effectCatalogLoaded)
        {
            ReloadEffectAssets();
        }
        const std::string normalized = OS::FileSystem::Normalize(path);
        for (const std::pair<const std::string, std::unique_ptr<EffectAsset>>& entry : m_effectAssets)
        {
            if (entry.second->Path() == normalized)
            {
                return entry.second.get();
            }
        }
        for (const std::pair<const std::string, std::unique_ptr<EffectSet>>& entry : m_effectSets)
        {
            if (entry.second->Path() == normalized)
            {
                return entry.second.get();
            }
        }
        for (const EffectAssetFile& file : ReadEffectAssetFiles(m_baseDir))
        {
            if (file.path != normalized)
            {
                continue;
            }
            if (file.isSet)
            {
                return LoadEffectSet(file.id);
            }
            return LoadEffectAsset(file.id);
        }
        return nullptr;
    }

    bool AssetManager::SaveAllEffectSets()
    {
        const std::set<std::string> pending = m_unsavedEffectSets;
        bool saved = true;
        for (const std::string& id : pending)
        {
            saved = SaveEffectSet(*LoadEffectSet(id)) && saved;
        }
        return saved;
    }

    std::optional<std::string> AssetManager::DuplicateEffectFile(std::string_view path, std::string_view newName)
    {
        if (!IsEffectFileName(newName))
        {
            return std::nullopt;
        }
        std::string extension = OS::FileSystem::Extension(path);
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (extension != ".effect" && extension != ".effectset")
        {
            return std::nullopt;
        }
        const std::optional<std::string> source = OS::FileSystem::ResolveUnder(m_baseDir, path);
        const std::string relative = "Assets/Effects/" + std::string{newName} + extension;
        const std::optional<std::string> destination = OS::FileSystem::ResolveUnder(m_baseDir, relative);
        if (!source || !destination || OS::FileSystem::Exists(*destination) || HasUnsavedEffectSets())
        {
            return std::nullopt;
        }
        const std::optional<std::string> text = OS::FileSystem::ReadAllText(*source);
        if (!text)
        {
            return std::nullopt;
        }
        nlohmann::json document = nlohmann::json::parse(*text, nullptr, false);
        if (!document.is_object() || !document.contains("id") || !document["id"].is_string() ||
            !IsEffectAssetId(document["id"].get<std::string>()) ||
            (extension == ".effectset" && (!document.contains("settings") || !document["settings"].is_object())))
        {
            return std::nullopt;
        }
        std::random_device random;
        std::uniform_int_distribution<unsigned int> digit(0, 15);
        std::string id;
        for (int i = 0; i < 32; ++i)
        {
            id.push_back("0123456789abcdef"[digit(random)]);
        }
        document["id"] = id;
        document["label"] = std::string{newName};
        std::optional<std::string> copiedBinary;
        if (extension == ".effect")
        {
            const EffectAssetFile file{id, std::string{newName}, std::string{path}, false, document};
            const std::optional<Gfx::EffectResourceDesc> resource = ReadEffectResource(file, m_baseDir);
            if (!resource)
            {
                return std::nullopt;
            }
            const std::string binaryReference =
                OS::FileSystem::Combine(OS::FileSystem::ParentDirectory(document["source"].get<std::string>()),
                                        std::string{newName} + ".efkefc");
            copiedBinary = OS::FileSystem::ResolveUnder(m_baseDir, binaryReference);
            if (!copiedBinary || OS::FileSystem::Exists(*copiedBinary))
            {
                return std::nullopt;
            }
            const std::optional<std::vector<std::byte>> bytes = OS::FileSystem::ReadAllBytes(resource->path);
            if (!bytes || !OS::FileSystem::ReplaceAllBytes(*copiedBinary, *bytes))
            {
                return std::nullopt;
            }
            document["source"] = binaryReference;
        }
        const std::string copy = document.dump(2) + "\n";
        if (!OS::FileSystem::ReplaceAllBytes(*destination,
                                             std::as_bytes(std::span<const char>{copy.data(), copy.size()})))
        {
            if (copiedBinary)
            {
                static_cast<void>(OS::FileSystem::RemoveFile(*copiedBinary));
            }
            return std::nullopt;
        }
        ReloadEffectAssets();
        return relative;
    }

    bool AssetManager::SaveEffectSet(const EffectSet& asset)
    {
        // ファイルが消えた演出セットと、コンテンツの外を指す演出セットは、書き直しても書けない
        // 未保存に残すと終了の確認と複製をずっと止めるので、外して ERROR を出す
        const std::optional<std::string> path = OS::FileSystem::ResolveUnder(m_baseDir, asset.Path());
        if (!asset.IsValid() || !path)
        {
            NS_LOG_ERROR(Scene, "演出セット {} は書き先が無いので保存できない", asset.Id());
            m_unsavedEffectSets.erase(asset.Id());
            return false;
        }
        const std::string text = asset.Document().dump(2) + "\n";
        // 書き込みの失敗は未保存に残し、終了の確認と SaveAllEffectSets の書き直しの対象にする
        if (!OS::FileSystem::ReplaceAllBytes(*path, std::as_bytes(std::span<const char>{text.data(), text.size()})))
        {
            m_unsavedEffectSets.insert(asset.Id());
            return false;
        }
        m_unsavedEffectSets.erase(asset.Id());
        return true;
    }
} // namespace NS
