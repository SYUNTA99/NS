#include "Framework/Graphics/Shader.h"

#include "Framework/Graphics/D3dCommon.h"
#include "Framework/Graphics/GraphicObject.h"
#include "Framework/Graphics/Renderer.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <d3dcompiler.h>

#include <filesystem>

namespace NS::Graphics
{

    namespace
    {
        struct ShaderTypeInfo
        {
            const char* infix;
            const char* entry; // 全 HLSL でステージ名 + Main に統一したエントリポイント
            const char* target;
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

        // 該当するステージが無ければ nullptr
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
                const char* msg = [&]() -> const char* {
                    if (errorBlob)
                    {
                        return static_cast<const char*>(errorBlob->GetBufferPointer());
                    }
                    return "(no error blob)";
                }();
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

        [[nodiscard]] ComPtr<ID3DBlob> CompileFallbackStage(const char* entryPoint, const char* target) noexcept
        {
            constexpr std::string_view kFallback{kFallbackHlsl};
            ComPtr<ID3DBlob> blob;
            CompileFromMemory(kFallback.data(), kFallback.size(), entryPoint, target, "ns_shader_fallback", blob);
            return blob;
        }

        // stage 別の Create*Shader 結果を共通基底 ComPtr に格納する
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

    std::unique_ptr<Shader> Shader::Create(const std::filesystem::path& hlslPath)
    {
        return std::unique_ptr<Shader>(new Shader(hlslPath));
    }

    Shader::Shader(const std::filesystem::path& hlslPath) : m_sourcePath(hlslPath)
    {
        const ShaderTypeInfo* info = DetectStage(m_sourcePath);
        if (info == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "Shader: ファイル名からステージを判定できない (.vs./.ps./.gs./.hs./.ds./.cs. を含まない): {}",
                         m_sourcePath.string());
            return;
        }
        m_type = info->stage;

        ComPtr<ID3D11DeviceChild> shader;
        ComPtr<ID3DBlob> bytecode;
        if (Compile(shader, bytecode))
        {
            m_shader = std::move(shader);
            m_vsBytecode = std::move(bytecode);
            return;
        }

        // 初回ロード失敗時のみ、 画面に出る頂点・ピクセルを magenta fallback にして「壊れて見える」状態にする
        // Reload は fallback せず旧物を保持するので、 編集中の typo では画面が壊れない
        if (m_type != ShaderType::Vertex && m_type != ShaderType::Pixel)
        {
            return;
        }
        auto* device = Gpu().device;
        if (device == nullptr)
        {
            return;
        }
        NS_LOG_ERROR(
            ::NS::Core::LogCat::Graphics, "Shader build failed, falling back to magenta: {}", m_sourcePath.string());
        ComPtr<ID3DBlob> fb = CompileFallbackStage(info->entry, info->target);
        if (!fb || !CreateStageObject(device, m_type, fb, m_shader))
        {
            return;
        }
        if (m_type == ShaderType::Vertex)
        {
            m_vsBytecode = std::move(fb);
        }
        m_fallback = true;
    }

    bool Shader::Compile(ComPtr<ID3D11DeviceChild>& outShader, ComPtr<ID3DBlob>& outVsBytecode) const noexcept
    {
        auto* device = Gpu().device;
        if (device == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "Shader: Renderer の Device が無効");
            return false;
        }
        const ShaderTypeInfo* info = DetectStage(m_sourcePath);
        if (info == nullptr)
        {
            return false;
        }
        ComPtr<ID3DBlob> blob = CompileStage(m_sourcePath, info->entry, info->target);
        if (!blob)
        {
            return false;
        }
        ComPtr<ID3D11DeviceChild> shader;
        if (!CreateStageObject(device, info->stage, blob, shader))
        {
            return false;
        }
        outShader = std::move(shader);
        if (info->stage == ShaderType::Vertex)
        {
            outVsBytecode = std::move(blob);
        }
        return true;
    }

    bool Shader::Reload()
    {
        ComPtr<ID3D11DeviceChild> shader;
        ComPtr<ID3DBlob> bytecode;
        if (!Compile(shader, bytecode))
        {
            return false; // 旧 m_shader / m_vsBytecode を保持する
        }
        m_shader = std::move(shader);
        if (m_type == ShaderType::Vertex)
        {
            m_vsBytecode = std::move(bytecode);
        }
        m_fallback = false; // 実ファイルの再コンパイル成功は fallback ではない
        return true;
    }

    bool Shader::IsValid() const noexcept
    {
        return static_cast<bool>(m_shader);
    }
    bool Shader::IsUsingFallback() const noexcept
    {
        return m_fallback;
    }
    ShaderType Shader::Type() const noexcept
    {
        return m_type;
    }
    ID3D11DeviceChild* Shader::Native() const noexcept
    {
        return m_shader.Get();
    }
    std::span<const std::byte> Shader::VertexShaderBytecode() const noexcept
    {
        if (m_type != ShaderType::Vertex || !m_vsBytecode)
        {
            return {};
        }
        return std::span<const std::byte>(static_cast<const std::byte*>(m_vsBytecode->GetBufferPointer()),
                                          m_vsBytecode->GetBufferSize());
    }

    namespace detail
    {
        std::span<const std::byte> GetVertexShaderBytecode(const Shader& shader) noexcept
        {
            return shader.VertexShaderBytecode();
        }
    } // namespace detail

} // namespace NS::Graphics
