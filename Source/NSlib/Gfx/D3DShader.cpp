#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Gfx/D3dCommon.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/StringUtils.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>
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

            constexpr const char* k_MaterialParamsName = "MaterialParams";

            // 配列と行列と構造体は欄で触れる形にしないので Unsupported
            [[nodiscard]] ShaderParamType ToParamType(const D3D11_SHADER_TYPE_DESC& type) noexcept
            {
                if (type.Elements != 0 || (type.Class != D3D_SVC_SCALAR && type.Class != D3D_SVC_VECTOR))
                {
                    return ShaderParamType::Unsupported;
                }
                if (type.Type == D3D_SVT_FLOAT)
                {
                    switch (type.Columns)
                    {
                    case 1:
                        return ShaderParamType::Float;
                    case 2:
                        return ShaderParamType::Float2;
                    case 3:
                        return ShaderParamType::Float3;
                    case 4:
                        return ShaderParamType::Float4;
                    default:
                        return ShaderParamType::Unsupported;
                    }
                }
                if (type.Columns != 1)
                {
                    return ShaderParamType::Unsupported;
                }
                switch (type.Type)
                {
                case D3D_SVT_INT:
                    return ShaderParamType::Int;
                case D3D_SVT_UINT:
                    return ShaderParamType::Uint;
                case D3D_SVT_BOOL:
                    return ShaderParamType::Bool;
                default:
                    return ShaderParamType::Unsupported;
                }
            }

            [[nodiscard]] bool ReadParam(ID3D11ShaderReflectionVariable* variable, ShaderParam& out) noexcept
            {
                D3D11_SHADER_VARIABLE_DESC desc{};
                D3D11_SHADER_TYPE_DESC type{};
                if (FAILED(variable->GetDesc(&desc)) || FAILED(variable->GetType()->GetDesc(&type)))
                {
                    return false;
                }
                out.name = desc.Name;
                out.offset = desc.StartOffset;
                out.size = desc.Size;
                out.used = (desc.uFlags & D3D_SVF_USED) != 0;
                out.type = ToParamType(type);
                if (out.type != ShaderParamType::Unsupported && desc.DefaultValue != nullptr)
                {
                    const std::size_t bytes = std::min<std::size_t>(desc.Size, sizeof(out.initialBits));
                    std::memcpy(out.initialBits.data(), desc.DefaultValue, bytes);
                }
                return true;
            }

            [[nodiscard]] bool IsSameParam(const ShaderParam& a, const ShaderParam& b) noexcept
            {
                return a.name == b.name && a.type == b.type && a.offset == b.offset && a.size == b.size;
            }
        } // namespace

        std::optional<ShaderParamLayout> ReflectMaterialParams(std::span<const std::byte> bytecode)
        {
            ComPtr<ID3D11ShaderReflection> reflection;
            const HRESULT hr = D3DReflect(bytecode.data(), bytecode.size(), IID_PPV_ARGS(reflection.GetAddressOf()));
            if (FAILED(hr))
            {
                NS_LOG_ERROR(Graphics, "MaterialParams: バイトコードを読めない (hr=0x{:X})", static_cast<uint>(hr));
                return std::nullopt;
            }
            // 宣言が無いか、どの項目も読まれずに消えたシェーダーは、材質の値を持たない
            D3D11_SHADER_INPUT_BIND_DESC bind{};
            if (FAILED(reflection->GetResourceBindingDescByName(k_MaterialParamsName, &bind)))
            {
                return ShaderParamLayout{};
            }
            if (bind.Type != D3D_SIT_CBUFFER || bind.BindPoint != k_MaterialParamsSlot)
            {
                NS_LOG_ERROR(Graphics,
                             "MaterialParams が b{} にある。材質の値は b{} の cbuffer にだけ書ける",
                             bind.BindPoint,
                             k_MaterialParamsSlot);
                return std::nullopt;
            }
            ID3D11ShaderReflectionConstantBuffer* buffer = reflection->GetConstantBufferByName(k_MaterialParamsName);
            D3D11_SHADER_BUFFER_DESC bufferDesc{};
            if (FAILED(buffer->GetDesc(&bufferDesc)))
            {
                NS_LOG_ERROR(Graphics, "MaterialParams: cbuffer の記述を読めない");
                return std::nullopt;
            }

            ShaderParamLayout layout;
            layout.bufferSize = bufferDesc.Size;
            layout.params.reserve(bufferDesc.Variables);
            for (UINT i = 0; i < bufferDesc.Variables; ++i)
            {
                ShaderParam param;
                if (!ReadParam(buffer->GetVariableByIndex(i), param))
                {
                    NS_LOG_ERROR(Graphics, "MaterialParams: {} 番目の項目の記述を読めない", i);
                    return std::nullopt;
                }
                layout.params.push_back(std::move(param));
            }
            return layout;
        }

        std::optional<ShaderParamLayout> MergeMaterialParams(const ShaderParamLayout& vertex,
                                                             const ShaderParamLayout& pixel)
        {
            if (vertex.bufferSize == 0)
            {
                return pixel;
            }
            if (pixel.bufferSize == 0)
            {
                return vertex;
            }
            if (vertex.bufferSize != pixel.bufferSize || vertex.params.size() != pixel.params.size())
            {
                NS_LOG_ERROR(Graphics,
                             "MaterialParams: 頂点 ({} バイト・{} 項目) と画素 ({} バイト・{} 項目) で並びが違う",
                             vertex.bufferSize,
                             vertex.params.size(),
                             pixel.bufferSize,
                             pixel.params.size());
                return std::nullopt;
            }
            ShaderParamLayout merged = vertex;
            for (std::size_t i = 0; i < merged.params.size(); ++i)
            {
                if (!IsSameParam(vertex.params[i], pixel.params[i]))
                {
                    NS_LOG_ERROR(Graphics,
                                 "MaterialParams: 頂点の {} (位置 {}) と画素の {} (位置 {}) で、名前・型・位置のどれかが違う",
                                 vertex.params[i].name,
                                 vertex.params[i].offset,
                                 pixel.params[i].name,
                                 pixel.params[i].offset);
                    return std::nullopt;
                }
                merged.params[i].used = vertex.params[i].used || pixel.params[i].used;
            }
            return merged;
        }

        std::unique_ptr<D3DShader> D3DShader::Create(std::string_view hlslPath)
        {
            return std::unique_ptr<D3DShader>(new D3DShader(hlslPath));
        }

        D3DShader::D3DShader(std::string_view hlslPath)
        {
            const ShaderTypeInfo* info = DetectStage(hlslPath);
            if (info == nullptr)
            {
                NS_LOG_ERROR(
                    Graphics,
                    "D3DShader: ファイル名からステージを判定できない (.vs./.ps./.gs./.hs./.ds./.cs. を含まない): {}",
                    hlslPath);
                return;
            }
            m_type = info->stage;

            // D3DDevice の無い場 (GPU の無い試し) では、ERROR を出さずに空のシェーダを返す
            // ゲームでは D3DDevice を先に作るので、ここで返らない。Reload の Compile は ERROR を出す
            ID3D11Device* device = Gpu().device;
            if (device == nullptr)
            {
                return;
            }

            ComPtr<ID3D11DeviceChild> shader;
            ComPtr<ID3DBlob> bytecode;
            if (Compile(hlslPath, shader, bytecode))
            {
                m_shader = std::move(shader);
                TakeBytecode(std::move(bytecode), hlslPath);
                return;
            }

            // 読み込み失敗時は、頂点/ピクセルシェーダであれば代替表示に切り替える
            if (m_type != ShaderType::Vertex && m_type != ShaderType::Pixel)
            {
                return;
            }
            NS_LOG_ERROR(Graphics, "D3DShader build failed, falling back to magenta: {}", hlslPath);
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

        bool D3DShader::Compile(std::string_view hlslPath,
                                ComPtr<ID3D11DeviceChild>& outShader,
                                ComPtr<ID3DBlob>& outBytecode) const noexcept
        {
            ID3D11Device* device = Gpu().device;
            if (device == nullptr)
            {
                NS_LOG_ERROR(Graphics, "D3DShader: 装置が無効");
                return false;
            }
            const ShaderTypeInfo* info = DetectStage(hlslPath);
            if (info == nullptr)
            {
                return false;
            }
            if (info->stage != m_type)
            {
                NS_LOG_ERROR(Graphics, "D3DShader: 作った時とステージの違うファイルは読まない: {}", hlslPath);
                return false;
            }
            ComPtr<ID3DBlob> blob = CompileStage(hlslPath, info->entry, info->target);
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
            outBytecode = std::move(blob);
            return true;
        }

        void D3DShader::TakeBytecode(ComPtr<ID3DBlob> bytecode, std::string_view hlslPath)
        {
            const std::span<const std::byte> bytes(static_cast<const std::byte*>(bytecode->GetBufferPointer()),
                                                   bytecode->GetBufferSize());
            // 並びを取り出せないシェーダーも描く事はできるので、材質の値なしで使う
            std::optional<ShaderParamLayout> layout = ReflectMaterialParams(bytes);
            if (!layout)
            {
                NS_LOG_ERROR(Graphics, "{} の MaterialParams を取り出せないので、材質の値なしで描く", hlslPath);
            }
            m_materialParams = std::move(layout).value_or(ShaderParamLayout{});
            if (m_type == ShaderType::Vertex)
            {
                m_vsBytecode = std::move(bytecode);
            }
        }

        bool D3DShader::Reload(std::string_view hlslPath)
        {
            ComPtr<ID3D11DeviceChild> shader;
            ComPtr<ID3DBlob> bytecode;
            if (!Compile(hlslPath, shader, bytecode))
            {
                return false;
            }
            m_shader = std::move(shader);
            TakeBytecode(std::move(bytecode), hlslPath);
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
        const ShaderParamLayout& D3DShader::MaterialParams() const noexcept
        {
            return m_materialParams;
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
