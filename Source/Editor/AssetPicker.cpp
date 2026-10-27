#include "Editor/AssetPicker.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/Gfx/Render/Skybox.h"
#include "NSlib/Core/Windows/Filesystem.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <optional>
#include <utility>

namespace NS
{
    namespace Editor
    {
        namespace
        {
            std::string LowerExtension(std::string_view path)
            {
                std::string extension = OS::FileSystem::Extension(path);
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                return extension;
            }

            bool IsAssetOfKind(AssetKind kind, std::string_view path)
            {
                if (kind == AssetKind::Material)
                {
                    return IsMaterialAssetPath(path);
                }
                if (kind == AssetKind::Effect)
                {
                    return LowerExtension(path) == ".efkefc";
                }
                if (kind == AssetKind::Sound)
                {
                    return LowerExtension(path) == ".wav";
                }
                if (kind == AssetKind::Mesh || kind == AssetKind::Model || kind == AssetKind::Animation)
                {
                    return IsModelAssetPath(path);
                }
                return true;
            }

            bool IsCubemapDds(std::string_view path)
            {
                const std::optional<std::vector<std::byte>> bytes = OS::FileSystem::ReadAllBytes(path);
                if (!bytes || bytes->size() < 128)
                {
                    return false;
                }
                const auto word = [&](std::size_t offset) {
                    uint32 value = 0;
                    std::memcpy(&value, bytes->data() + offset, sizeof(value));
                    return value;
                };
                // DDS_HEADER の各欄をファイル上の位置から読む。
                if (word(0) != 0x20534444 || word(4) != 124 || word(76) != 32 ||
                    word(12) == 0 || word(12) != word(16))
                {
                    return false;
                }
                if (word(84) == 0x30315844)
                {
                    // DDS_HEADER_DXT10 の配列数は、面の数ではなく立方体の数。
                    return bytes->size() >= 148 && word(132) == 3 && (word(136) & 4) != 0 && word(140) == 1;
                }
                return (word(112) & 0xfe00) == 0xfe00;
            }
        }

        bool IsSkyboxAssetPath(std::string_view absolutePath)
        {
            if (OS::FileSystem::IsDirectory(absolutePath))
            {
                const std::array<std::string_view, 6>& faces = Gfx::Skybox::FaceFileNames();
                return std::all_of(faces.begin(), faces.end(), [&](std::string_view face) {
                    const std::string file = OS::FileSystem::Combine(absolutePath, face);
                    return OS::FileSystem::Exists(file) && !OS::FileSystem::IsDirectory(file);
                });
            }
            return LowerExtension(absolutePath) == ".dds" && OS::FileSystem::Exists(absolutePath) &&
                   IsCubemapDds(absolutePath);
        }

        std::vector<AssetOption> CollectSkyboxOptions(std::string_view contentRoot)
        {
            std::vector<AssetOption> options;
            const std::string root = OS::FileSystem::Normalize(contentRoot);
            const std::string assets = OS::FileSystem::Combine(root, "Assets");
            if (!OS::FileSystem::IsDirectory(assets))
            {
                return options;
            }
            for (const std::string& file : OS::FileSystem::ListFilesRecursive(assets))
            {
                std::string candidate = file;
                if (OS::FileSystem::FileName(file) == Gfx::Skybox::FaceFileNames().front())
                {
                    candidate = OS::FileSystem::ParentDirectory(file);
                }
                if (!IsSkyboxAssetPath(candidate))
                {
                    continue;
                }
                const std::optional<std::string> relative = OS::FileSystem::RelativeUnder(root, candidate);
                if (!relative)
                {
                    NS_LOG_ERROR(App, "空の素材 {} が ContentRoot の外にあるので候補に出さない", candidate);
                    continue;
                }
                options.push_back(AssetOption{*relative, OS::FileSystem::FileName(candidate),
                                              OS::FileSystem::ParentDirectory(*relative)});
            }
            std::sort(options.begin(), options.end(), [](const AssetOption& a, const AssetOption& b) {
                return a.value < b.value;
            });
            return options;
        }

        bool IsMaterialAssetPath(std::string_view path)
        {
            return LowerExtension(path) == ".mat";
        }

        bool IsModelAssetPath(std::string_view path)
        {
            const std::string extension = LowerExtension(path);
            return extension == ".gltf" || extension == ".glb";
        }

        bool IsTextureAssetPath(std::string_view path)
        {
            const std::string extension = LowerExtension(path);
            return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
                   extension == ".bmp" || extension == ".tif" || extension == ".tiff" || extension == ".gif";
        }

        bool IsActorFileAssetPath(std::string_view path)
        {
            return LowerExtension(path) == ".actor";
        }

        std::vector<AssetOption> CollectAssetOptions(AssetKind kind, std::string_view contentRoot)
        {
            std::vector<AssetOption> options;
            std::span<const std::string_view> builtins;
            if (kind == AssetKind::Mesh)
            {
                builtins = AssetManager::BuiltinMeshNames();
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
            if (kind == AssetKind::Sound)
            {
                directory = OS::FileSystem::Combine(directory, "Sounds");
            }
            if (kind == AssetKind::None || !OS::FileSystem::IsDirectory(directory))
            {
                return options;
            }
            for (const std::string& file : OS::FileSystem::ListFilesRecursive(directory))
            {
                if (!IsAssetOfKind(kind, file))
                {
                    continue;
                }
                const std::optional<std::string> found = OS::FileSystem::RelativeUnder(root, file);
                if (!found)
                {
                    NS_LOG_ERROR(App, "素材 {} が ContentRoot の外にあるので候補に出さない", file);
                    continue;
                }
                const std::string& relative = *found;
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
                if (kind == AssetKind::Effect || kind == AssetKind::Sound)
                {
                    // エフェクトは Effects、音は Sounds のフォルダからの道を、拡張子を除いて名前にする
                    const std::optional<std::string> underFolder = OS::FileSystem::RelativeUnder(directory, file);
                    if (!underFolder)
                    {
                        NS_LOG_ERROR(App, "素材 {} が {} の外にあるので候補に出さない", file, directory);
                        continue;
                    }
                    std::string name = *underFolder;
                    name.resize(name.size() - OS::FileSystem::Extension(name).size());
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
