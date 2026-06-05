#include "Framework/Graphics/Shader.h"

#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/detail/d3d_context.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <d3dcompiler.h>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace NS::Graphics
{
    using detail::ComPtr;

    namespace
    {
        enum class ShaderType
        {
            Vertex,
            Pixel,
            Geometry,
            Hull,
            Domain,
            Compute,
        };

        struct ShaderTypeInfo
        {
            const char* infix;  // ファイル名に含まれる識別子
            const char* entry;  // エントリポイント (全 HLSL でステージ名 + Main に統一)
            const char* target; // コンパイルターゲットプロファイル
            ShaderType stage;
        };

        constexpr ShaderTypeInfo kStageTable[] = {
            {".vs.", "VSMain", "vs_5_0", ShaderType::Vertex},
            {".ps.", "PSMain", "ps_5_0", ShaderType::Pixel},
            {".gs.", "GSMain", "gs_5_0", ShaderType::Geometry},
            {".hs.", "HSMain", "hs_5_0", ShaderType::Hull},
            {".ds.", "DSMain", "ds_5_0", ShaderType::Domain},
            {".cs.", "CSMain", "cs_5_0", ShaderType::Compute},
        };

        // ファイル名の `.vs.` 等でステージを判定する。 どれも含まなければ nullptr
        [[nodiscard]] const ShaderTypeInfo* DetectStage(const std::filesystem::path& path) noexcept
        {
            const std::string name = path.filename().string();
            for (const ShaderTypeInfo& info : kStageTable)
            {
                if (name.find(info.infix) != std::string::npos)
                {
                    return &info;
                }
            }
            return nullptr;
        }

        // 画面に出る頂点・ピクセルのみ使う magenta fallback。 該当ステージの entry だけコンパイルする
        constexpr const char* kFallbackHlsl = R"HLSL(struct VSInput  { float3 position : POSITION; };
struct VSOutput { float4 position : SV_Position; };
VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.position = float4(input.position, 1.0);
    return o;
}
float4 PSMain() : SV_Target
{
    return float4(1.0, 0.0, 1.0, 1.0);
})HLSL";

        bool CompileFromMemory(const void* bytes,
                               std::size_t size,
                               const char* entryPoint,
                               const char* target,
                               const char* sourceName,
                               ComPtr<ID3DBlob>& outBlob) noexcept
        {
            UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
            flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
            flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

            ComPtr<ID3DBlob> errorBlob;
            const HRESULT hr = D3DCompile(bytes,
                                          size,
                                          sourceName,
                                          nullptr,
                                          D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                          entryPoint,
                                          target,
                                          flags,
                                          0u,
                                          outBlob.GetAddressOf(),
                                          errorBlob.GetAddressOf());
            if (FAILED(hr))
            {
                const char* msg =
                    errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "(no error blob)";
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Shader compile failed: target={}, entry={}, hr=0x{:X}, msg={}",
                             target,
                             entryPoint,
                             static_cast<unsigned>(hr),
                             msg);
                return false;
            }
            return true;
        }

        // .hlsl を読んで 1 ステージをコンパイルし blob を返す。 path 空 / 読込失敗 / コンパイル失敗で nullptr
        [[nodiscard]] ComPtr<ID3DBlob> CompileStage(const std::filesystem::path& path,
                                                    const char* entryPoint,
                                                    const char* target) noexcept
        {
            if (path.empty())
            {
                return nullptr;
            }
            const auto bytes = ::NS::Core::FileSystem::ReadAllBytes(path);
            if (!bytes.has_value())
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Shader file read failed: {}", path.string());
                return nullptr;
            }
            const std::string tag = path.string();
            ComPtr<ID3DBlob> blob;
            if (!CompileFromMemory(bytes->data(), bytes->size(), entryPoint, target, tag.c_str(), blob))
            {
                return nullptr;
            }
            return blob;
        }

        // magenta fallback HLSL から 1 ステージをコンパイルする
        [[nodiscard]] ComPtr<ID3DBlob> CompileFallbackStage(const char* entryPoint, const char* target) noexcept
        {
            constexpr std::string_view kFallback{kFallbackHlsl};
            ComPtr<ID3DBlob> blob;
            CompileFromMemory(kFallback.data(), kFallback.size(), entryPoint, target, "ns_shader_fallback", blob);
            return blob;
        }

        // ステージごとの Create*Shader を呼び、 共通基底 ComPtr に格納する。 失敗で false
        [[nodiscard]] bool CreateStageObject(ID3D11Device* device,
                                             ShaderType stage,
                                             const ComPtr<ID3DBlob>& blob,
                                             ComPtr<ID3D11DeviceChild>& out) noexcept
        {
            const void* code = blob->GetBufferPointer();
            const SIZE_T size = blob->GetBufferSize();
            HRESULT hr = E_FAIL;
            switch (stage)
            {
            case ShaderType::Vertex:
            {
                ComPtr<ID3D11VertexShader> s;
                hr = device->CreateVertexShader(code, size, nullptr, s.GetAddressOf());
                out = s;
                break;
            }
            case ShaderType::Pixel:
            {
                ComPtr<ID3D11PixelShader> s;
                hr = device->CreatePixelShader(code, size, nullptr, s.GetAddressOf());
                out = s;
                break;
            }
            case ShaderType::Geometry:
            {
                ComPtr<ID3D11GeometryShader> s;
                hr = device->CreateGeometryShader(code, size, nullptr, s.GetAddressOf());
                out = s;
                break;
            }
            case ShaderType::Hull:
            {
                ComPtr<ID3D11HullShader> s;
                hr = device->CreateHullShader(code, size, nullptr, s.GetAddressOf());
                out = s;
                break;
            }
            case ShaderType::Domain:
            {
                ComPtr<ID3D11DomainShader> s;
                hr = device->CreateDomainShader(code, size, nullptr, s.GetAddressOf());
                out = s;
                break;
            }
            case ShaderType::Compute:
            {
                ComPtr<ID3D11ComputeShader> s;
                hr = device->CreateComputeShader(code, size, nullptr, s.GetAddressOf());
                out = s;
                break;
            }
            }
            if (FAILED(hr))
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Create*Shader 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
                return false;
            }
            return true;
        }
    } // namespace

    struct Shader::Impl
    {
        ShaderType stage = ShaderType::Vertex;
        ComPtr<ID3D11DeviceChild> shader; // 全ステージ共通の保持先 (取得時に static_cast でダウンキャスト)
        ComPtr<ID3DBlob> vsBytecode;      // 頂点ステージのみ (Mesh の InputLayout 用)
        bool fallback = false;
    };

    Shader::Shader(Renderer& renderer, const std::filesystem::path& hlslPath) : m_pImpl(std::make_unique<Impl>())
    {
        auto* device = detail::GetDevice(renderer);
        if (device == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Shader: Renderer の Device が無効");
            return;
        }

        const ShaderTypeInfo* info = DetectStage(hlslPath);
        if (info == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "Shader: ファイル名からステージを判定できない (.vs./.ps./.gs./.hs./.ds./.cs. を含まない): {}",
                         hlslPath.string());
            return;
        }
        m_pImpl->stage = info->stage;

        ComPtr<ID3DBlob> blob = CompileStage(hlslPath, info->entry, info->target);
        bool fallback = false;
        if (!blob)
        {
            // magenta fallback は画面に出る頂点・ピクセルのみ。 他ステージは代替表示が無いので無効のまま残す
            if (info->stage == ShaderType::Vertex || info->stage == ShaderType::Pixel)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Shader build failed, falling back to magenta: {}",
                             hlslPath.string());
                blob = CompileFallbackStage(info->entry, info->target);
                fallback = true;
            }
        }
        if (!blob)
        {
            return;
        }

        if (!CreateStageObject(device, info->stage, blob, m_pImpl->shader))
        {
            return;
        }
        if (info->stage == ShaderType::Vertex)
        {
            m_pImpl->vsBytecode = std::move(blob);
        }
        m_pImpl->fallback = fallback;
    }

    Shader::~Shader() = default;

    bool Shader::IsValid() const noexcept
    {
        return m_pImpl && static_cast<bool>(m_pImpl->shader);
    }
    bool Shader::IsUsingFallback() const noexcept
    {
        return m_pImpl && m_pImpl->fallback;
    }

    namespace detail
    {
        std::span<const std::byte> GetVertexShaderBytecode(const Shader& shader) noexcept
        {
            const auto& impl = shader.m_pImpl;
            if (!impl || impl->stage != ShaderType::Vertex || !impl->vsBytecode)
            {
                return {};
            }
            return std::span<const std::byte>(static_cast<const std::byte*>(impl->vsBytecode->GetBufferPointer()),
                                              impl->vsBytecode->GetBufferSize());
        }

        void BindShader(ID3D11DeviceContext* context, const Shader& shader) noexcept
        {
            const auto& impl = shader.m_pImpl;
            if (context == nullptr || !impl || !impl->shader)
            {
                return;
            }
            // 生成時のステージで実型は保証済みなので static_cast 下方変換は well-defined
            ID3D11DeviceChild* raw = impl->shader.Get();
            switch (impl->stage)
            {
            case ShaderType::Vertex:
                context->VSSetShader(static_cast<ID3D11VertexShader*>(raw), nullptr, 0u);
                break;
            case ShaderType::Pixel:
                context->PSSetShader(static_cast<ID3D11PixelShader*>(raw), nullptr, 0u);
                break;
            case ShaderType::Geometry:
                context->GSSetShader(static_cast<ID3D11GeometryShader*>(raw), nullptr, 0u);
                break;
            case ShaderType::Hull:
                context->HSSetShader(static_cast<ID3D11HullShader*>(raw), nullptr, 0u);
                break;
            case ShaderType::Domain:
                context->DSSetShader(static_cast<ID3D11DomainShader*>(raw), nullptr, 0u);
                break;
            case ShaderType::Compute:
                context->CSSetShader(static_cast<ID3D11ComputeShader*>(raw), nullptr, 0u);
                break;
            }
        }
    } // namespace detail

} // namespace NS::Graphics
