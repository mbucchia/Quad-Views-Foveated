// MIT License
//
// Copyright(c) 2022-2023 Matthieu Bucchianeri
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this softwareand associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pch.h"

#ifdef XR_USE_GRAPHICS_API_D3D11

#include "log.h"
#include "graphics.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

namespace {

    using namespace openxr_api_layer::log;
    using namespace openxr_api_layer::utils::graphics;

    constexpr bool PreferNtHandle = false;

    struct D3D11Timer : IGraphicsTimer {
        D3D11Timer(ID3D11Device* device) {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D11Timer_Create");

            device->GetImmediateContext(m_context.ReleaseAndGetAddressOf());

            D3D11_QUERY_DESC queryDesc;
            ZeroMemory(&queryDesc, sizeof(D3D11_QUERY_DESC));
            queryDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
            CHECK_HRCMD(device->CreateQuery(&queryDesc, m_timeStampDis.ReleaseAndGetAddressOf()));
            queryDesc.Query = D3D11_QUERY_TIMESTAMP;
            CHECK_HRCMD(device->CreateQuery(&queryDesc, m_timeStampStart.ReleaseAndGetAddressOf()));
            CHECK_HRCMD(device->CreateQuery(&queryDesc, m_timeStampEnd.ReleaseAndGetAddressOf()));

            TraceLoggingWriteStop(local, "D3D11Timer_Create", TLPArg(this, "Timer"));
        }

        ~D3D11Timer() override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D11Timer_Destroy", TLPArg(this, "Timer"));
            TraceLoggingWriteStop(local, "D3D11Timer_Destroy");
        }

        Api getApi() const override {
            return Api::D3D11;
        }

        void start() override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D11Timer_Start", TLPArg(this, "Timer"));

            m_context->Begin(m_timeStampDis.Get());
            m_context->End(m_timeStampStart.Get());

            TraceLoggingWriteStop(local, "D3D11Timer_Start");
        }

        void stop() override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D11Timer_Stop", TLPArg(this, "Timer"));

            m_context->End(m_timeStampEnd.Get());
            m_context->End(m_timeStampDis.Get());
            m_valid = true;

            TraceLoggingWriteStop(local, "D3D11Timer_Stop");
        }

        uint64_t query() const override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D11Timer_Query", TLPArg(this, "Timer"), TLArg(m_valid, "Valid"));

            uint64_t duration = 0;
            if (m_valid) {
                UINT64 startime = 0, endtime = 0;
                D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disData = {0};

                if (m_context->GetData(m_timeStampStart.Get(), &startime, sizeof(UINT64), 0) == S_OK &&
                    m_context->GetData(m_timeStampEnd.Get(), &endtime, sizeof(UINT64), 0) == S_OK &&
                    m_context->GetData(
                        m_timeStampDis.Get(), &disData, sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT), 0) == S_OK &&
                    !disData.Disjoint) {
                    duration = static_cast<uint64_t>(((endtime - startime) * 1e6) / disData.Frequency);
                }
                m_valid = false;
            }

            TraceLoggingWriteStop(local, "D3D11Timer_Query", TLArg(duration, "Duration"));

            return duration;
        }

        ComPtr<ID3D11DeviceContext> m_context;
        ComPtr<ID3D11Query> m_timeStampDis;
        ComPtr<ID3D11Query> m_timeStampStart;
        ComPtr<ID3D11Query> m_timeStampEnd;

        // Can the timer be queried (it might still only read 0).
        mutable bool m_valid{false};
    };

    struct D3D11Fence : IGraphicsFence {
        D3D11Fence(ID3D11Fence* fence, bool shareable) : m_fence(fence), m_isShareable(shareable) {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(
                local, "D3D11Fence_Create", TLPArg(fence, "D3D11Fence"), TLArg(shareable, "Shareable"));

            // Query the necessary flavors of device context which will let us use fences.
            ComPtr<ID3D11Device> device;
            m_fence->GetDevice(device.ReleaseAndGetAddressOf());
            ComPtr<ID3D11DeviceContext> context;
            device->GetImmediateContext(context.ReleaseAndGetAddressOf());
            CHECK_HRCMD(context->QueryInterface(m_context.ReleaseAndGetAddressOf()));

            TraceLoggingWriteStop(local, "D3D11Fence_Create", TLPArg(this, "Fence"));
        }

        ~D3D11Fence() override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D11Fence_Destroy", TLPArg(this, "Fence"));
            TraceLoggingWriteStop(local, "D3D11Fence_Destroy");
        }

        Api getApi() const override {
            return Api::D3D11;
        }

        void* getNativeFencePtr() const override {
            return m_fence.Get();
        }

        ShareableHandle getFenceHandle() const override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D11Fence_Export", TLPArg(this, "Fence"));

            if (!m_isShareable) {
                throw std::runtime_error("Fence is not shareable");
            }

            ShareableHandle handle{};
            CHECK_HRCMD(m_fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, handle.ntHandle.put()));
            handle.isNtHandle = true;
            handle.origin = Api::D3D11;

            TraceLoggingWriteStop(local, "D3D11Fence_Export", TLPArg(handle.ntHandle.get(), "Handle"));

            return handle;
        }

        void signal(uint64_t value) override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D11Fence_Signal", TLPArg(this, "Fence"), TLArg(value, "Value"));

            CHECK_HRCMD(m_context->Signal(m_fence.Get(), value));
            m_context->Flush();

            TraceLoggingWriteStop(local, "D3D11Fence_Signal");
        }

        void waitOnDevice(uint64_t value) override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(
                local, "D3D11Fence_Wait", TLPArg(this, "Fence"), TLArg("Device", "WaitType"), TLArg(value, "Value"));

            CHECK_HRCMD(m_context->Wait(m_fence.Get(), value));

            TraceLoggingWriteStop(local, "D3D11Fence_Wait");
        }

        void waitOnCpu(uint64_t value) override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(
                local, "D3D11Fence_Wait", TLPArg(this, "Fence"), TLArg("Host", "WaitType"), TLArg(value, "Value"));

            wil::unique_handle eventHandle;
            CHECK_HRCMD(m_context->Signal(m_fence.Get(), value));
            m_context->Flush();
            *eventHandle.put() = CreateEventEx(nullptr, L"D3D Fence", 0, EVENT_ALL_ACCESS);
            CHECK_HRCMD(m_fence->SetEventOnCompletion(value, eventHandle.get()));
            WaitForSingleObject(eventHandle.get(), INFINITE);
            ResetEvent(eventHandle.get());

            TraceLoggingWriteStop(local, "D3D11Fence_Wait");
        }

        bool isShareable() const override {
            return m_isShareable;
        }

        const ComPtr<ID3D11Fence> m_fence;
        const bool m_isShareable;

        ComPtr<ID3D11DeviceContext4> m_context;
    };

    struct D3D11Texture : IGraphicsTexture {
        D3D11Texture(ID3D11Texture2D* texture) : m_texture(texture) {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D11Texture_Create", TLPArg(texture, "D3D11Texture"));

            D3D11_TEXTURE2D_DESC desc;
            m_texture->GetDesc(&desc);
            TraceLoggingWriteTagged(local,
                                    "D3D11Texture_Create",
                                    TLArg(desc.Width, "Width"),
                                    TLArg(desc.Height, "Height"),
                                    TLArg(desc.ArraySize, "ArraySize"),
                                    TLArg(desc.MipLevels, "MipCount"),
                                    TLArg(desc.SampleDesc.Count, "SampleCount"),
                                    TLArg((int)desc.Format, "Format"),
                                    TLArg((int)desc.Usage, "Usage"),
                                    TLArg(desc.BindFlags, "BindFlags"),
                                    TLArg(desc.CPUAccessFlags, "CPUAccessFlags"),
                                    TLArg(desc.MiscFlags, "MiscFlags"));

            // Construct the API-agnostic info descriptor.
            m_info.format = (int64_t)desc.Format;
            m_info.width = desc.Width;
            m_info.height = desc.Height;
            m_info.arraySize = desc.ArraySize;
            m_info.mipCount = desc.MipLevels;
            m_info.sampleCount = desc.SampleDesc.Count;
            m_info.faceCount = 1;
            m_info.usageFlags = 0;
            if (desc.BindFlags & D3D11_BIND_RENDER_TARGET) {
                m_info.usageFlags |= XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
            }
            if (desc.BindFlags & D3D11_BIND_DEPTH_STENCIL) {
                m_info.usageFlags |= XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            }
            if (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
                m_info.usageFlags |= XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
            }
            if (desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) {
                m_info.usageFlags |= XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT;
            }

            // Identify the shareability.
            if (desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED) {
                m_isShareable = true;
                if (desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) {
                    m_useNtHandle = true;
                }
            }

            TraceLoggingWriteStop(local,
                                  "D3D11Texture_Create",
                                  TLPArg(this, "Texture"),
                                  TLArg(m_isShareable, "Shareable"),
                                  TLArg(m_useNtHandle, "IsNTHandle"));
        }

        ~D3D11Texture() override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D11Texture_Destroy", TLPArg(this, "Texture"));
            TraceLoggingWriteStop(local, "D3D11Texture_Destroy");
        }

        Api getApi() const override {
            return Api::D3D11;
        }

        void* getNativeTexturePtr() const override {
            return m_texture.Get();
        }

        ShareableHandle getTextureHandle() const override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D11Texture_Export", TLPArg(this, "Texture"));

            if (!m_isShareable) {
                throw std::runtime_error("Texture is not shareable");
            }

            ShareableHandle handle{};
            ComPtr<IDXGIResource1> dxgiResource;
            CHECK_HRCMD(m_texture->QueryInterface(IID_PPV_ARGS(dxgiResource.ReleaseAndGetAddressOf())));
            if (!m_useNtHandle) {
                CHECK_HRCMD(dxgiResource->GetSharedHandle(&handle.handle));
            } else {
                CHECK_HRCMD(dxgiResource->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, handle.ntHandle.put()));
            }
            handle.isNtHandle = m_useNtHandle;
            handle.origin = Api::D3D11;

            TraceLoggingWriteStop(
                local, "D3D11Texture_Export", TLPArg(!m_useNtHandle ? handle.handle : handle.ntHandle.get(), "Handle"));

            return handle;
        }

        const XrSwapchainCreateInfo& getInfo() const override {
            return m_info;
        }

        bool isShareable() const override {
            return m_isShareable;
        }

        const ComPtr<ID3D11Texture2D> m_texture;

        XrSwapchainCreateInfo m_info{};
        bool m_isShareable{false};
        bool m_useNtHandle{false};
    };

    struct D3D11GraphicsDevice : IGraphicsDevice {
        D3D11GraphicsDevice(ID3D11Device* device) : m_device(device) {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D11GraphicsDevice_Create", TLPArg(device, "D3D11Device"));

            {
                ComPtr<IDXGIDevice> dxgiDevice;
                CHECK_HRCMD(m_device->QueryInterface(IID_PPV_ARGS(dxgiDevice.ReleaseAndGetAddressOf())));
                ComPtr<IDXGIAdapter> dxgiAdapter;
                CHECK_HRCMD(dxgiDevice->GetAdapter(dxgiAdapter.ReleaseAndGetAddressOf()));
                DXGI_ADAPTER_DESC desc;
                CHECK_HRCMD(dxgiAdapter->GetDesc(&desc));
                m_adapterLuid = desc.AdapterLuid;

                TraceLoggingWriteTagged(
                    local,
                    "D3D11GraphicsDevice_Create",
                    TLArg(desc.Description, "Adapter"),
                    TLArg(fmt::format("{}:{}", m_adapterLuid.HighPart, m_adapterLuid.LowPart).c_str(), " Luid"));
            }

            // Query the necessary flavors of device which will let us use fences.
            CHECK_HRCMD(m_device->QueryInterface(m_deviceForFencesAndNtHandles.ReleaseAndGetAddressOf()));
            m_device->GetImmediateContext(m_context.ReleaseAndGetAddressOf());

            TraceLoggingWriteStop(local, "D3D11GraphicsDevice_Create", TLPArg(this, "Device"));
        }

        ~D3D11GraphicsDevice() override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D11GraphicsDevice_Destroy", TLPArg(this, "Device"));
            TraceLoggingWriteStop(local, "D3D11GraphicsDevice_Destroy");
        }

        Api getApi() const override {
            return Api::D3D11;
        }

        void* getNativeDevicePtr() const override {
            return m_device.Get();
        }

        void* getNativeContextPtr() const override {
            return m_context.Get();
        }

        std::shared_ptr<IGraphicsTimer> createTimer() override {
            return std::make_shared<D3D11Timer>(m_device.Get());
        }

        std::shared_ptr<IGraphicsFence> createFence(bool shareable) override {
            ComPtr<ID3D11Fence> fence;
            CHECK_HRCMD(
                m_deviceForFencesAndNtHandles->CreateFence(0,
                                                           shareable ? D3D11_FENCE_FLAG_SHARED : D3D11_FENCE_FLAG_NONE,
                                                           IID_PPV_ARGS(fence.ReleaseAndGetAddressOf())));
            return std::make_shared<D3D11Fence>(fence.Get(), shareable);
        }

        std::shared_ptr<IGraphicsFence> openFence(const ShareableHandle& handle) override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local,
                                   "D3D11Fence_Import",
                                   TLArg(!handle.isNtHandle ? handle.handle : handle.ntHandle.get(), "Handle"),
                                   TLArg(handle.isNtHandle, "IsNTHandle"));

            if (!handle.isNtHandle) {
                throw std::runtime_error("Must be NTHANDLE");
            }

            ComPtr<ID3D11Fence> fence;
            CHECK_HRCMD(m_deviceForFencesAndNtHandles->OpenSharedFence(handle.isNtHandle ? handle.ntHandle.get()
                                                                                         : handle.handle,
                                                                       IID_PPV_ARGS(fence.ReleaseAndGetAddressOf())));
            std::shared_ptr<IGraphicsFence> result = std::make_shared<D3D11Fence>(fence.Get(), false /* shareable */);

            TraceLoggingWriteStop(local, "D3D11Fence_Import", TLPArg(result.get(), "Fence"));

            return result;
        }

        std::shared_ptr<IGraphicsTexture> createTexture(const XrSwapchainCreateInfo& info, bool shareable) override {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Format = (DXGI_FORMAT)info.format;
            desc.Width = info.width;
            desc.Height = info.height;
            desc.ArraySize = info.arraySize;
            desc.MipLevels = info.mipCount;
            desc.SampleDesc.Count = info.sampleCount;
            desc.Usage = D3D11_USAGE_DEFAULT;
            if (info.usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) {
                desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
            }
            if (info.usageFlags & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                desc.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
            }
            if (info.usageFlags & XR_SWAPCHAIN_USAGE_SAMPLED_BIT) {
                desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
            }
            if (info.usageFlags & XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT) {
                desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
            }
            // Mark as shareable if needed.
            desc.MiscFlags =
                shareable ? (D3D11_RESOURCE_MISC_SHARED | (PreferNtHandle ? D3D11_RESOURCE_MISC_SHARED_NTHANDLE : 0))
                          : 0;

            ComPtr<ID3D11Texture2D> texture;
            CHECK_HRCMD(m_device->CreateTexture2D(&desc, nullptr, texture.ReleaseAndGetAddressOf()));
            return std::make_shared<D3D11Texture>(texture.Get());
        }

        std::shared_ptr<IGraphicsTexture> openTexture(const ShareableHandle& handle,
                                                      const XrSwapchainCreateInfo& info) override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local,
                                   "D3D11Texture_Import",
                                   TLArg(!handle.isNtHandle ? handle.handle : handle.ntHandle.get(), "Handle"),
                                   TLArg(handle.isNtHandle, "IsNTHandle"));

            ComPtr<ID3D11Texture2D> texture;
            if (!handle.isNtHandle) {
                CHECK_HRCMD(
                    m_device->OpenSharedResource(handle.handle, IID_PPV_ARGS(texture.ReleaseAndGetAddressOf())));
            } else {
                CHECK_HRCMD(m_deviceForFencesAndNtHandles->OpenSharedResource1(
                    handle.ntHandle.get(), IID_PPV_ARGS(texture.ReleaseAndGetAddressOf())));
            }

            std::shared_ptr<IGraphicsTexture> result = std::make_shared<D3D11Texture>(texture.Get());

            TraceLoggingWriteStop(local, "D3D11Texture_Import", TLPArg(result.get(), "Texture"));

            return result;
        }

        std::shared_ptr<IGraphicsTexture> openTexturePtr(void* nativeTexturePtr,
                                                         const XrSwapchainCreateInfo& info) override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D11Texture_Import", TLPArg(nativeTexturePtr, "D3D11Texture"));

            ID3D11Texture2D* texture = reinterpret_cast<ID3D11Texture2D*>(nativeTexturePtr);

            std::shared_ptr<IGraphicsTexture> result = std::make_shared<D3D11Texture>(texture);

            TraceLoggingWriteStop(local, "D3D11Texture_Import", TLPArg(result.get(), "Texture"));

            return result;
        }

        void copyTexture(IGraphicsTexture* from, IGraphicsTexture* to) override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D11Texture_Copy", TLPArg(from, "Source"), TLPArg(to, "Destination"));

            m_context->CopyResource(to->getNativeTexture<D3D11>(), from->getNativeTexture<D3D11>());

            TraceLoggingWriteStop(local, "D3D11Texture_Copy");
        }

        GenericFormat translateToGenericFormat(int64_t format) const override {
            return (DXGI_FORMAT)format;
        }

        int64_t translateFromGenericFormat(GenericFormat format) const override {
            return (int64_t)format;
        }

        LUID getAdapterLuid() const override {
            return m_adapterLuid;
        }

        const ComPtr<ID3D11Device> m_device;
        LUID m_adapterLuid{};

        ComPtr<ID3D11Device5> m_deviceForFencesAndNtHandles;
        ComPtr<ID3D11DeviceContext> m_context;
    };

} // namespace

namespace openxr_api_layer::utils::graphics::internal {

    std::shared_ptr<IGraphicsDevice> createD3D11CompositionDevice(LUID adapterLuid) {
        // Find the adapter.
        ComPtr<IDXGIFactory1> dxgiFactory;
        CHECK_HRCMD(CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.ReleaseAndGetAddressOf())));
        ComPtr<IDXGIAdapter1> dxgiAdapter;
        for (UINT adapterIndex = 0;; adapterIndex++) {
            // EnumAdapters1 will fail with DXGI_ERROR_NOT_FOUND when there are no more adapters to
            // enumerate.
            CHECK_HRCMD(dxgiFactory->EnumAdapters1(adapterIndex, dxgiAdapter.ReleaseAndGetAddressOf()));

            DXGI_ADAPTER_DESC1 desc;
            CHECK_HRCMD(dxgiAdapter->GetDesc1(&desc));
            if (!memcmp(&desc.AdapterLuid, &adapterLuid, sizeof(LUID))) {
                break;
            }
        }

        // Create our own device on the same adapter.
        ComPtr<ID3D11Device> device;
        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        CHECK_HRCMD(D3D11CreateDevice(dxgiAdapter.Get(),
                                      D3D_DRIVER_TYPE_UNKNOWN,
                                      0,
                                      flags,
                                      &featureLevel,
                                      1,
                                      D3D11_SDK_VERSION,
                                      device.ReleaseAndGetAddressOf(),
                                      nullptr,
                                      nullptr));

        return std::make_shared<D3D11GraphicsDevice>(device.Get());
    }

    std::shared_ptr<IGraphicsDevice> wrapApplicationDevice(const XrGraphicsBindingD3D11KHR& bindings) {
        return std::make_shared<D3D11GraphicsDevice>(bindings.device);
    }

} // namespace openxr_api_layer::utils::graphics::internal

#endif
