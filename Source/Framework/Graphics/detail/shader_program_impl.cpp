#include "Framework/Graphics/ShaderProgram.h"

#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/detail/d3d_context.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <d3dcompiler.h>

#include <string>
#include <string_view>
#include <vector>

namespace NS::Graphics
{
    using detail::ComPtr;

    struct ShaderProgram::Impl
    {
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11PixelShader> ps;
        ComPtr<ID3D11InputLayout> layout;
        ComPtr<ID3D11DeviceContext> context;
        bool fallback = false;
    };

    namespace
    {
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

        [[nodiscard]] DXGI_FORMAT ToDxgiFormat(InputElementFormat fmt) noexcept
        {
            switch (fmt)
            {
            case InputElementFormat::Float2:
                return DXGI_FORMAT_R32G32_FLOAT;
            case InputElementFormat::Float3:
                return DXGI_FORMAT_R32G32B32_FLOAT;
            case InputElementFormat::Float4:
                return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case InputElementFormat::UInt32:
                return DXGI_FORMAT_R32_UINT;
            }
            return DXGI_FORMAT_UNKNOWN;
        }

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

        bool CreateInputLayoutFromDesc(ID3D11Device* device,
                                       ID3DBlob* vsBlob,
                                       const std::vector<InputElement>& elements,
                                       ComPtr<ID3D11InputLayout>& outLayout) noexcept
        {
            if (device == nullptr || vsBlob == nullptr || elements.empty())
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "CreateInputLayoutFromDesc: 引数不正 (device={}, vsBlob={}, elements={})",
                             static_cast<const void*>(device),
                             static_cast<const void*>(vsBlob),
                             elements.size());
                return false;
            }
            std::vector<D3D11_INPUT_ELEMENT_DESC> descs;
            descs.reserve(elements.size());
            for (const auto& e : elements)
            {
                D3D11_INPUT_ELEMENT_DESC d{};
                d.SemanticName = e.semanticName.c_str();
                d.SemanticIndex = 0u;
                d.Format = ToDxgiFormat(e.format);
                d.InputSlot = 0u;
                d.AlignedByteOffset = e.byteOffset;
                d.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
                d.InstanceDataStepRate = 0u;
                descs.push_back(d);
            }
            const HRESULT hr = device->CreateInputLayout(descs.data(),
                                                         static_cast<UINT>(descs.size()),
                                                         vsBlob->GetBufferPointer(),
                                                         vsBlob->GetBufferSize(),
                                                         outLayout.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "CreateInputLayout 失敗 (hr=0x{:X}, elements={})",
                             static_cast<unsigned>(hr),
                             elements.size());
                return false;
            }
            return true;
        }

        /// 全成功時のみ out* に commit するため、部分成功時のリーク扱いが呼出側に漏れない
        bool BuildShaderPair(ID3D11Device* device,
                             const void* vsBytes,
                             std::size_t vsSize,
                             const char* vsEntry,
                             const char* vsSourceTag,
                             const void* psBytes,
                             std::size_t psSize,
                             const char* psEntry,
                             const char* psSourceTag,
                             const std::vector<InputElement>& inputLayout,
                             ComPtr<ID3D11VertexShader>& outVs,
                             ComPtr<ID3D11PixelShader>& outPs,
                             ComPtr<ID3D11InputLayout>& outLayout) noexcept
        {
            ComPtr<ID3DBlob> vsBlob;
            ComPtr<ID3DBlob> psBlob;
            if (!CompileFromMemory(vsBytes, vsSize, vsEntry, "vs_5_0", vsSourceTag, vsBlob))
            {
                return false;
            }
            if (!CompileFromMemory(psBytes, psSize, psEntry, "ps_5_0", psSourceTag, psBlob))
            {
                return false;
            }

            ComPtr<ID3D11VertexShader> vs;
            ComPtr<ID3D11PixelShader> ps;
            ComPtr<ID3D11InputLayout> layout;

            HRESULT hr = device->CreateVertexShader(
                vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, vs.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(
                    ::NS::Core::LogCat::Graphics, "CreateVertexShader 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
                return false;
            }
            hr = device->CreatePixelShader(
                psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, ps.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(
                    ::NS::Core::LogCat::Graphics, "CreatePixelShader 失敗 (hr=0x{:X})", static_cast<unsigned>(hr));
                return false;
            }
            if (!CreateInputLayoutFromDesc(device, vsBlob.Get(), inputLayout, layout))
            {
                return false;
            }

            outVs = std::move(vs);
            outPs = std::move(ps);
            outLayout = std::move(layout);
            return true;
        }

        bool BuildFallback(ID3D11Device* device,
                           const std::vector<InputElement>& inputLayout,
                           ComPtr<ID3D11VertexShader>& outVs,
                           ComPtr<ID3D11PixelShader>& outPs,
                           ComPtr<ID3D11InputLayout>& outLayout) noexcept
        {
            constexpr std::string_view kFallback{kFallbackHlsl};
            return BuildShaderPair(device,
                                   kFallback.data(),
                                   kFallback.size(),
                                   "VSMain",
                                   "ns_shader_program_fallback",
                                   kFallback.data(),
                                   kFallback.size(),
                                   "PSMain",
                                   "ns_shader_program_fallback",
                                   inputLayout,
                                   outVs,
                                   outPs,
                                   outLayout);
        }
    } // namespace

    ShaderProgram::ShaderProgram(Renderer& renderer, const ShaderProgramDesc& desc) : m_pImpl(std::make_unique<Impl>())
    {
        auto* device = detail::GetDevice(renderer);
        auto* context = detail::GetContext(renderer);
        if (device == nullptr || context == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "ShaderProgram: Renderer の Device / Context が無効");
            return;
        }
        m_pImpl->context = context;

        bool built = false;
        if (!desc.vertexShaderPath.empty() && !desc.pixelShaderPath.empty())
        {
            auto vsBytes = ::NS::Core::FileSystem::ReadAllBytes(desc.vertexShaderPath);
            auto psBytes = ::NS::Core::FileSystem::ReadAllBytes(desc.pixelShaderPath);
            if (vsBytes.has_value() && psBytes.has_value())
            {
                const auto& vsBuf = vsBytes.value();
                const auto& psBuf = psBytes.value();
                const std::string vsTag = desc.vertexShaderPath.string();
                const std::string psTag = desc.pixelShaderPath.string();
                built = BuildShaderPair(device,
                                        vsBuf.data(),
                                        vsBuf.size(),
                                        desc.vertexEntryPoint.c_str(),
                                        vsTag.c_str(),
                                        psBuf.data(),
                                        psBuf.size(),
                                        desc.pixelEntryPoint.c_str(),
                                        psTag.c_str(),
                                        desc.inputLayout,
                                        m_pImpl->vs,
                                        m_pImpl->ps,
                                        m_pImpl->layout);
                if (!built)
                {
                    NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                                 "Shader build failed, falling back to magenta: vs={}, ps={}",
                                 vsTag,
                                 psTag);
                }
            }
            else
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "Shader file read failed: vs={}, ps={}",
                             desc.vertexShaderPath.string(),
                             desc.pixelShaderPath.string());
            }
        }

        if (built)
        {
            return;
        }

        if (!BuildFallback(device, desc.inputLayout, m_pImpl->vs, m_pImpl->ps, m_pImpl->layout))
        {
            m_pImpl->vs.Reset();
            m_pImpl->ps.Reset();
            m_pImpl->layout.Reset();
            m_pImpl->context.Reset();
            return;
        }
        m_pImpl->fallback = true;
    }

    ShaderProgram::~ShaderProgram() = default;

    bool ShaderProgram::IsValid() const noexcept
    {
        return m_pImpl && m_pImpl->vs && m_pImpl->ps && m_pImpl->layout;
    }
    bool ShaderProgram::IsUsingFallback() const noexcept
    {
        return m_pImpl && m_pImpl->fallback;
    }

    void ShaderProgram::Bind() noexcept
    {
        if (!IsValid() || !m_pImpl->context)
        {
            return;
        }
        m_pImpl->context->VSSetShader(m_pImpl->vs.Get(), nullptr, 0u);
        m_pImpl->context->PSSetShader(m_pImpl->ps.Get(), nullptr, 0u);
        m_pImpl->context->IASetInputLayout(m_pImpl->layout.Get());
    }

    namespace detail
    {
        ID3D11VertexShader* GetVertexShader(ShaderProgram& sp) noexcept
        {
            return sp.m_pImpl ? sp.m_pImpl->vs.Get() : nullptr;
        }
        ID3D11PixelShader* GetPixelShader(ShaderProgram& sp) noexcept
        {
            return sp.m_pImpl ? sp.m_pImpl->ps.Get() : nullptr;
        }
        ID3D11InputLayout* GetInputLayout(ShaderProgram& sp) noexcept
        {
            return sp.m_pImpl ? sp.m_pImpl->layout.Get() : nullptr;
        }
    } // namespace detail

} // namespace NS::Graphics
