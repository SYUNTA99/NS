#include "Editor/AssetPicker.h"

#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/Windows/Filesystem.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace NS
{
    namespace Editor
    {
        std::vector<AssetOption> CollectAssetOptions(AssetKind kind, std::string_view contentRoot)
        {
            std::vector<AssetOption> options;
            std::span<const std::string_view> builtins;
            if (kind == AssetKind::Mesh)
            {
                builtins = AssetManager::BuiltinMeshNames();
            }
            else if (kind == AssetKind::Material)
            {
                builtins = AssetManager::SharedMaterialNames();
            }
            for (const std::string_view name : builtins)
            {
                options.push_back(AssetOption{std::string{name}, std::string{name}, "組み込み"});
            }

            const std::string root = OS::FileSystem::Normalize(contentRoot);
            std::string directory = OS::FileSystem::Combine(root, "Assets");
            if (kind == AssetKind::Effect)
            {
                directory = OS::FileSystem::Combine(directory, "Effects");
            }
            if (kind == AssetKind::None || !OS::FileSystem::IsDirectory(directory))
            {
                return options;
            }
            for (const std::string& file : OS::FileSystem::ListFilesRecursive(directory))
            {
                std::string extension = OS::FileSystem::Extension(file);
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                const bool modelFile = extension == ".gltf" || extension == ".glb";
                if ((kind == AssetKind::Material && extension != ".mat") ||
                    (kind == AssetKind::Effect && extension != ".efkefc") ||
                    ((kind == AssetKind::Mesh || kind == AssetKind::Model || kind == AssetKind::Animation) && !modelFile))
                {
                    continue;
                }
                const std::string relative = file.substr(root.size() + 1);
                const std::string label = OS::FileSystem::FileName(relative);
                const std::string location = OS::FileSystem::ParentDirectory(relative);
                if (kind == AssetKind::Animation)
                {
                    const AnimSource source = LoadGltfAnimationSource(file);
                    for (const Anim::AnimClip& clip : source.animations)
                    {
                        if (clip.IsValid())
                        {
                            std::string clipLabel = clip.name;
                            if (clipLabel.empty())
                            {
                                clipLabel = label + " (名前なし)";
                            }
                            options.push_back(AssetOption{relative + "#" + clip.name, std::move(clipLabel), relative});
                        }
                    }
                    continue;
                }
                if (kind == AssetKind::Effect)
                {
                    std::string name = file.substr(directory.size() + 1);
                    name.resize(name.size() - extension.size());
                    options.push_back(AssetOption{name, name, location});
                    continue;
                }
                options.push_back(AssetOption{relative, label, location});
            }
            std::sort(options.begin(), options.end(), [](const AssetOption& a, const AssetOption& b) {
                if (a.label != b.label)
                {
                    return a.label < b.label;
                }
                return a.value < b.value;
            });
            return options;
        }
    }
}
