#include "NSlib/Graphics/D3DShader.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Graphics/D3dCommon.h"
#include "NSlib/Graphics/GraphicObject.h"
#include "NSlib/Graphics/Renderer.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/StringUtils.h"

#include <d3dcompiler.h>

#include <string>

namespace NS
{
    namespace Gfx
    {
        namespace
        {
            struct ShaderTypeInfo
            {
                const char* infix;
                const char* entry;
                const char* target;
                ShaderType stage;
            };
            constexpr ShaderTypeInfo k_StageTable[] = {
                {".vs.", "VSMain", "vs_5_0", ShaderType::Vertex},
                {".ps.", "PSMain", "ps_5_0", ShaderType::Pixel},
                {".gs.", "GSMain", "gs_5_0", ShaderType::Geometry},
                {".hs.", "HSMain", "hs_5_0", ShaderType::Hull},
                {".ds.", "DSMain", "ds_5_0", ShaderType::Domain},
                {".cs.", "CSMain", "cs_5_0", ShaderType::Compute},
            };

            // 該当するステージが無ければ nullptr
            [[nodiscard]] const ShaderTypeInfo* DetectStage(std::string_view path) noexcept
            {
                const std::string name = OS::FileSystem::FileName(path);
                for (const ShaderTypeInfo& info : k_StageTable)
                {
                    if (name.find(info.infix) != std::string::npos)
                    {
                        return &info;
                    }
                }
                return nullptr;
            }

            // 読み込みに失敗した時のピンク一色のシェーダ
            constexpr const char* k_FallbackHlsl =
                R"HLSL(struct VSInput  { float3 position : POSITION; };
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

            [[nodiscard]] UINT CompileFlags() noexcept
            {
                UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
                flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
                flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
                return flags;
            }

            void LogCompileFailure(const char* entryPoint,
                                   const char* target,
                                   HRESULT hr,
                                   const ComPtr<ID3DBlob>& errorBlob) noexcept
            {
                const char* msg = [&]() -> const char* {
                    if (errorBlob)
                    {
                        return static_cast<const char*>(errorBlob->GetBufferPointer());
                    }
                    return "(no error blob)";
                }();
                NS_LOG_ERROR(Graphics,
                             "D3DShader compile failed: target={}, entry={}, hr=0x{:X}, msg={}",
                             target,
                             entryPoint,
                             static_cast<uint>(hr),
                             msg);
            }

            // D3DCompile のソース名は char* のみで、非 ASCII を含むパスは #include の基準として扱えない
            // Common.hlsli が解決できずコンパイルが失敗するため、wide のまま渡せる D3DCompileFromFile を使う
            [[nodiscard]] ComPtr<ID3DBlob> CompileStage(std::string_view path,
                                                        const char* entryPoint,
                                                        const char* target) noexcept
            {
                if (path.empty())
                {
                    return nullptr;
                }
                if (!OS::FileSystem::Exists(path))
                {
                    NS_LOG_ERROR(Graphics, "D3DShader file not found: {}", path);
                    return nullptr;
                }
                const std::wstring widePath = OS::StringUtils::WideFromUtf8(path);
                ComPtr<ID3DBlob> blob;
                ComPtr<ID3DBlob> errorBlob;
                const HRESULT hr = D3DCompileFromFile(widePath.c_str(),
                                                      nullptr,
                                                      D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                                      entryPoint,
                                                      target,
                                                      CompileFlags(),
                                                      0u,
                                                      blob.GetAddressOf(),
                                                      errorBlob.GetAddressOf());
                if (FAILED(hr))
                {
                    LogCompileFailure(entryPoint, target, hr, errorBlob);
                    return nullptr;
                }
                return blob;
            }

            [[nodiscard]] ComPtr<ID3DBlob> CompileFallbackStage(const char* entryPoint, const char* target) noexcept
            {
                constexpr std::string_view k_Fallback{k_FallbackHlsl};
                ComPtr<ID3DBlob> blob;
                ComPtr<ID3DBlob> errorBlob;
                const HRESULT hr = D3DCompile(k_Fallback.data(),
                                              k_Fallback.size(),
                                              "ns_shader_fallback",
                                              nullptr,
                                              D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                              entryPoint,
                                              target,
                                              CompileFlags(),
                                              0u,
                                              blob.GetAddressOf(),
                                              errorBlob.GetAddressOf());
                if (FAILED(hr))
                {
                    LogCompileFailure(entryPoint, target, hr, errorBlob);
                }
                return blob;
            }

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
                    ComPtr<ID3D11VertexShader> shader;
                    hr = device->CreateVertexShader(code, size, nullptr, shader.GetAddressOf());
                    out = shader;
                    break;
                }
                case ShaderType::Pixel:
                {
                    ComPtr<ID3D11PixelShader> shader;
                    hr = device->CreatePixelShader(code, size, nullptr, shader.GetAddressOf());
                    out = shader;
                    break;
                }
                case ShaderType::Geometry:
                {
                    ComPtr<ID3D11GeometryShader> shader;
                    hr = device->CreateGeometryShader(code, size, nullptr, shader.GetAddressOf());
                    out = shader;
                    break;
                }
                case ShaderType::Hull:
                {
                    ComPtr<ID3D11HullShader> shader;
                    hr = device->CreateHullShader(code, size, nullptr, shader.GetAddressOf());
                    out = shader;
                    break;
                }
                case ShaderType::Domain:
                {
                    ComPtr<ID3D11DomainShader> shader;
                    hr = device->CreateDomainShader(code, size, nullptr, shader.GetAddressOf());
                    out = shader;
                    break;
                }
                case ShaderType::Compute:
                {
                    ComPtr<ID3D11ComputeShader> shader;
                    hr = device->CreateComputeShader(code, size, nullptr, shader.GetAddressOf());
                    out = shader;
                    break;
                }
                }
                if (FAILED(hr))
                {
                    NS_LOG_ERROR(Graphics, "Create*Shader 失敗 (hr=0x{:X})", static_cast<uint>(hr));
                    return false;
                }
                return true;
            }
        } // namespace

        std::unique_ptr<D3DShader> D3DShader::Create(std::string_view hlslPath)
        {
            return std::unique_ptr<D3DShader>(new D3DShader(hlslPath));
        }

        std::unique_ptr<D3DShader> D3DShader::CreateBuiltin(std::string_view fileName)
        {
            const std::string shaderDir = OS::FileSystem::Combine(OS::FileSystem::ContentRoot(), "Shaders");
            return Create(OS::FileSystem::Combine(shaderDir, fileName));
        }

        D3DShader::D3DShader(std::string_view hlslPath) : m_sourcePath(hlslPath)
        {
            const ShaderTypeInfo* info = DetectStage(m_sourcePath);
            if (info == nullptr)
            {
                NS_LOG_ERROR(
                    Graphics,
                    "D3DShader: ファイル名からステージを判定できない (.vs./.ps./.gs./.hs./.ds./.cs. を含まない): {}",
                    m_sourcePath);
                return;
            }
            m_type = info->stage;

            // Renderer の無い場 (GPU の無い試し) では、ERROR を出さずに空のシェーダを返す
            // ゲームでは Renderer を先に作るので、ここで返らない。Reload の Compile は ERROR を出す
            ID3D11Device* device = Gpu().device;
            if (device == nullptr)
            {
                return;
            }

            ComPtr<ID3D11DeviceChild> shader;
            ComPtr<ID3DBlob> bytecode;
            if (Compile(shader, bytecode))
            {
                m_shader = std::move(shader);
                m_vsBytecode = std::move(bytecode);
                return;
            }

            // 読み込み失敗時は、頂点/ピクセルシェーダであれば代替表示に切り替える
            if (m_type != ShaderType::Vertex && m_type != ShaderType::Pixel)
            {
                return;
            }
            NS_LOG_ERROR(Graphics, "D3DShader build failed, falling back to magenta: {}", m_sourcePath);
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

        bool D3DShader::Compile(ComPtr<ID3D11DeviceChild>& outShader, ComPtr<ID3DBlob>& outVsBytecode) const noexcept
        {
            ID3D11Device* device = Gpu().device;
            if (device == nullptr)
            {
                NS_LOG_ERROR(Graphics, "D3DShader: Renderer の Device が無効");
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

        bool D3DShader::Reload()
        {
            ComPtr<ID3D11DeviceChild> shader;
            ComPtr<ID3DBlob> bytecode;
            if (!Compile(shader, bytecode))
            {
                return false;
            }
            m_shader = std::move(shader);
            if (m_type == ShaderType::Vertex)
            {
                m_vsBytecode = std::move(bytecode);
            }
            m_fallback = false;
            return true;
        }

        bool D3DShader::IsValid() const noexcept
        {
            return static_cast<bool>(m_shader);
        }
        bool D3DShader::IsUsingFallback() const noexcept
        {
            return m_fallback;
        }
        ShaderType D3DShader::Type() const noexcept
        {
            return m_type;
        }
        ID3D11DeviceChild* D3DShader::Native() const noexcept
        {
            return m_shader.Get();
        }
        std::span<const std::byte> D3DShader::VertexShaderBytecode() const noexcept
        {
            if (m_type != ShaderType::Vertex || !m_vsBytecode)
            {
                return {};
            }
            return std::span<const std::byte>(static_cast<const std::byte*>(m_vsBytecode->GetBufferPointer()),
                                              m_vsBytecode->GetBufferSize());
        }
    } // namespace Gfx
} // namespace NS
