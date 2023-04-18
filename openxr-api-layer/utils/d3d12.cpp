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

#ifdef XR_USE_GRAPHICS_API_D3D12

#include "log.h"
#include "graphics.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")

namespace {

    using namespace openxr_api_layer::log;
    using namespace openxr_api_layer::utils::graphics;

    struct D3D12Timer : IGraphicsTimer {
        D3D12Timer(ID3D12Device* device, ID3D12CommandQueue* queue) : m_queue(queue) {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D12Timer_Create");

            // Create the command context.
            for (uint32_t i = 0; i < 2; i++) {
                CHECK_HRCMD(device->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_commandAllocator[i].ReleaseAndGetAddressOf())));
                m_commandAllocator[i]->SetName(L"Timer Command Allocator");
                CHECK_HRCMD(device->CreateCommandList(0,
                                                      D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                      m_commandAllocator[i].Get(),
                                                      nullptr,
                                                      IID_PPV_ARGS(m_commandList[i].ReleaseAndGetAddressOf())));
                m_commandList[i]->SetName(L"Timer Command List");
                CHECK_HRCMD(m_commandList[i]->Close());
            }
            CHECK_HRCMD(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.ReleaseAndGetAddressOf())));
            m_fence->SetName(L"Timer Readback Fence");

            // Create the query heap and readback resources.
            D3D12_QUERY_HEAP_DESC heapDesc{};
            heapDesc.Count = 2;
            heapDesc.NodeMask = 0;
            heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            CHECK_HRCMD(device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(m_queryHeap.ReleaseAndGetAddressOf())));
            m_queryHeap->SetName(L"Timestamp Query Heap");

            D3D12_HEAP_PROPERTIES heapType{};
            heapType.Type = D3D12_HEAP_TYPE_READBACK;
            heapType.CreationNodeMask = heapType.VisibleNodeMask = 1;
            D3D12_RESOURCE_DESC readbackDesc{};
            readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            readbackDesc.Width = heapDesc.Count * sizeof(uint64_t);
            readbackDesc.Height = readbackDesc.DepthOrArraySize = readbackDesc.MipLevels =
                readbackDesc.SampleDesc.Count = 1;
            readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            CHECK_HRCMD(device->CreateCommittedResource(&heapType,
                                                        D3D12_HEAP_FLAG_NONE,
                                                        &readbackDesc,
                                                        D3D12_RESOURCE_STATE_COPY_DEST,
                                                        nullptr,
                                                        IID_PPV_ARGS(m_queryReadbackBuffer.ReleaseAndGetAddressOf())));
            m_queryReadbackBuffer->SetName(L"Query Readback Buffer");

            TraceLoggingWriteStop(local, "D3D12Timer_Create", TLPArg(this, "Timer"));
        }

        ~D3D12Timer() override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D12Timer_Destroy", TLPArg(this, "Timer"));
            TraceLoggingWriteStop(local, "D3D12Timer_Destroy");
        }

        Api getApi() const override {
            return Api::D3D12;
        }

        void start() override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D12Timer_Start", TLPArg(this, "Timer"));

            CHECK_HRCMD(m_commandAllocator[0]->Reset());
            CHECK_HRCMD(m_commandList[0]->Reset(m_commandAllocator[0].Get(), nullptr));
            m_commandList[0]->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
            CHECK_HRCMD(m_commandList[0]->Close());
            ID3D12CommandList* const lists[] = {m_commandList[0].Get()};
            m_queue->ExecuteCommandLists(1, lists);

            TraceLoggingWriteStop(local, "D3D12Timer_Start");
        }

        void stop() override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D12Timer_Stop", TLPArg(this, "Timer"));

            CHECK_HRCMD(m_commandAllocator[1]->Reset());
            CHECK_HRCMD(m_commandList[1]->Reset(m_commandAllocator[1].Get(), nullptr));
            m_commandList[1]->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
            m_commandList[1]->ResolveQueryData(
                m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, m_queryReadbackBuffer.Get(), 0);
            CHECK_HRCMD(m_commandList[1]->Close());
            ID3D12CommandList* const lists[] = {m_commandList[1].Get()};
            m_queue->ExecuteCommandLists(1, lists);

            // Signal a fence for completion.
            m_queue->Signal(m_fence.Get(), ++m_fenceValue);
            m_valid = true;

            TraceLoggingWriteStop(local, "D3D12Timer_Stop");
        }

        uint64_t query() const override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D12Timer_Query", TLPArg(this, "Timer"), TLArg(m_valid, "Valid"));

            uint64_t duration = 0;
            if (m_valid) {
                uint64_t gpuTickFrequency;
                if (m_fence->GetCompletedValue() >= m_fenceValue &&
                    SUCCEEDED(m_queue->GetTimestampFrequency(&gpuTickFrequency))) {
                    uint64_t* mappedBuffer;
                    D3D12_RANGE range{0, 2 * sizeof(uint64_t)};
                    CHECK_HRCMD(m_queryReadbackBuffer->Map(0, &range, reinterpret_cast<void**>(&mappedBuffer)));
                    duration = ((mappedBuffer[1] - mappedBuffer[0]) * 1000000) / gpuTickFrequency;
                    m_queryReadbackBuffer->Unmap(0, nullptr);
                }
                m_valid = false;
            }

            TraceLoggingWriteStop(local, "D3D12Timer_Query", TLArg(duration, "Duration"));

            return duration;
        }

        ComPtr<ID3D12CommandQueue> m_queue;
        ComPtr<ID3D12CommandAllocator> m_commandAllocator[2];
        ComPtr<ID3D12GraphicsCommandList> m_commandList[2];
        ComPtr<ID3D12Fence> m_fence;
        uint64_t m_fenceValue{0};
        ComPtr<ID3D12QueryHeap> m_queryHeap;
        ComPtr<ID3D12Resource> m_queryReadbackBuffer;

        // Can the timer be queried (it might still only read 0).
        mutable bool m_valid{false};
    };

    struct D3D12Fence : IGraphicsFence {
        D3D12Fence(ID3D12Fence* fence, ID3D12CommandQueue* commandQueue, bool shareable)
            : m_fence(fence), m_commandQueue(commandQueue), m_isShareable(shareable) {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(
                local, "D3D12Fence_Create", TLPArg(fence, "D3D12Fence"), TLArg(shareable, "Shareable"));

            m_fence->GetDevice(IID_PPV_ARGS(m_device.ReleaseAndGetAddressOf()));

            TraceLoggingWriteStop(local, "D3D12Fence_Create", TLPArg(this, "Fence"));
        }

        ~D3D12Fence() override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D12Fence_Destroy", TLPArg(this, "Fence"));
            TraceLoggingWriteStop(local, "D3D12Fence_Destroy");
        }

        Api getApi() const override {
            return Api::D3D12;
        }

        void* getNativeFencePtr() const override {
            return m_fence.Get();
        }

        ShareableHandle getFenceHandle() const override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D12Fence_Export", TLPArg(this, "Fence"));

            if (!m_isShareable) {
                throw std::runtime_error("Fence is not shareable");
            }

            ShareableHandle handle{};
            CHECK_HRCMD(
                m_device->CreateSharedHandle(m_fence.Get(), nullptr, GENERIC_ALL, nullptr, handle.ntHandle.put()));
            handle.isNtHandle = true;
            handle.origin = Api::D3D12;

            TraceLoggingWriteStop(local, "D3D12Fence_Export", TLPArg(handle.ntHandle.get(), "Handle"));

            return handle;
        }

        void signal(uint64_t value) override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D12Fence_Signal", TLPArg(this, "Fence"), TLArg(value, "Value"));

            CHECK_HRCMD(m_commandQueue->Signal(m_fence.Get(), value));

            TraceLoggingWriteStop(local, "D3D12Fence_Signal");
        }

        void waitOnDevice(uint64_t value) override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(
                local, "D3D12Fence_Wait", TLPArg(this, "Fence"), TLArg("Device", "WaitType"), TLArg(value, "Value"));

            CHECK_HRCMD(m_commandQueue->Wait(m_fence.Get(), value));

            TraceLoggingWriteStop(local, "D3D12Fence_Wait");
        }

        void waitOnCpu(uint64_t value) override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(
                local, "D3D12Fence_Wait", TLPArg(this, "Fence"), TLArg("Host", "WaitType"), TLArg(value, "Value"));

            wil::unique_handle eventHandle;
            CHECK_HRCMD(m_commandQueue->Signal(m_fence.Get(), value));
            *eventHandle.put() = CreateEventEx(nullptr, L"D3D Fence", 0, EVENT_ALL_ACCESS);
            CHECK_HRCMD(m_fence->SetEventOnCompletion(value, eventHandle.get()));
            WaitForSingleObject(eventHandle.get(), INFINITE);
            ResetEvent(eventHandle.get());

            TraceLoggingWriteStop(local, "D3D12Fence_Wait");
        }

        bool isShareable() const override {
            return m_isShareable;
        }

        const ComPtr<ID3D12Fence> m_fence;
        const ComPtr<ID3D12CommandQueue> m_commandQueue;
        const bool m_isShareable;

        ComPtr<ID3D12Device> m_device;
    };

    struct D3D12Texture : IGraphicsTexture {
        D3D12Texture(ID3D12Resource* texture) : m_texture(texture) {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D12Texture_Create", TLPArg(texture, "D3D12Texture"));

            m_texture->GetDevice(IID_PPV_ARGS(m_device.ReleaseAndGetAddressOf()));

            D3D12_RESOURCE_DESC desc = m_texture->GetDesc();
            TraceLoggingWriteTagged(local,
                                    "D3D12Texture_Create",
                                    TLArg(desc.Width, "Width"),
                                    TLArg(desc.Height, "Height"),
                                    TLArg(desc.DepthOrArraySize, "ArraySize"),
                                    TLArg(desc.MipLevels, "MipCount"),
                                    TLArg(desc.SampleDesc.Count, "SampleCount"),
                                    TLArg((int)desc.Format, "Format"),
                                    TLArg((int)desc.Flags, "Flags"));

            // Construct the API-agnostic info descriptor.
            m_info.format = (int64_t)desc.Format;
            m_info.width = (uint32_t)desc.Width;
            m_info.height = desc.Height;
            m_info.arraySize = desc.DepthOrArraySize;
            m_info.mipCount = desc.MipLevels;
            m_info.sampleCount = desc.SampleDesc.Count;
            m_info.faceCount = 1;
            m_info.usageFlags = 0;
            if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) {
                m_info.usageFlags |= XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
            }
            if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
                m_info.usageFlags |= XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            }
            if (!(desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
                m_info.usageFlags |= XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
            }
            if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) {
                m_info.usageFlags |= XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT;
            }

            // Identify the shareability.
            D3D12_HEAP_FLAGS heapFlags;
            CHECK_HRCMD(m_texture->GetHeapProperties(nullptr, &heapFlags));
            m_isShareable = heapFlags & D3D12_HEAP_FLAG_SHARED;

            TraceLoggingWriteStop(
                local, "D3D12Texture_Create", TLPArg(this, "Texture"), TLArg(m_isShareable, "Shareable"));
        }

        ~D3D12Texture() override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D12Texture_Destroy", TLPArg(this, "Texture"));
            TraceLoggingWriteStop(local, "D3D12Texture_Destroy");
        }

        Api getApi() const override {
            return Api::D3D12;
        }

        void* getNativeTexturePtr() const override {
            return m_texture.Get();
        }

        ShareableHandle getTextureHandle() const override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D12Texture_Export", TLPArg(this, "Texture"));

            if (!m_isShareable) {
                throw std::runtime_error("Texture is not shareable");
            }

            ShareableHandle handle{};
            CHECK_HRCMD(
                m_device->CreateSharedHandle(m_texture.Get(), nullptr, GENERIC_ALL, nullptr, handle.ntHandle.put()));
            handle.isNtHandle = true;
            handle.origin = Api::D3D12;

            TraceLoggingWriteStop(local, "D3D12Texture_Export", TLPArg(handle.ntHandle.get(), "Handle"));

            return handle;
        }

        const XrSwapchainCreateInfo& getInfo() const override {
            return m_info;
        }

        bool isShareable() const override {
            return m_isShareable;
        }

        const ComPtr<ID3D12Resource> m_texture;
        ComPtr<ID3D12Device> m_device;

        XrSwapchainCreateInfo m_info{};
        bool m_isShareable{false};
    };

    struct D3D12ReusableCommandList {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commandList;
        uint32_t completedFenceValue{0};
    };

    struct D3D12GraphicsDevice : IGraphicsDevice {
        D3D12GraphicsDevice(ID3D12Device* device, ID3D12CommandQueue* commandQueue)
            : m_device(device), m_commandQueue(commandQueue) {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(
                local, "D3D12GraphicsDevice_Create", TLPArg(device, "D3D12Device"), TLPArg(commandQueue, "Queue"));

            {
                const LUID adapterLuid = m_device->GetAdapterLuid();

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
                        TraceLoggingWriteTagged(
                            local,
                            "D3D12GraphicsDevice_Create",
                            TLArg(desc.Description, "Adapter"),
                            TLArg(fmt::format("{}:{}", adapterLuid.HighPart, adapterLuid.LowPart).c_str(), " Luid"));
                        break;
                    }
                }
            }

            CHECK_HRCMD(m_device->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_commandListPoolFence.ReleaseAndGetAddressOf())));

            TraceLoggingWriteStop(local, "D3D12GraphicsDevice_Create", TLPArg(this, "Device"));
        }

        ~D3D12GraphicsDevice() override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D12GraphicsDevice_Destroy", TLPArg(this, "Device"));
            TraceLoggingWriteStop(local, "D3D12GraphicsDevice_Destroy");
        }

        Api getApi() const override {
            return Api::D3D12;
        }

        void* getNativeDevicePtr() const override {
            return m_device.Get();
        }

        void* getNativeContextPtr() const override {
            return m_commandQueue.Get();
        }

        std::shared_ptr<IGraphicsTimer> createTimer() override {
            return std::make_shared<D3D12Timer>(m_device.Get(), m_commandQueue.Get());
        }

        std::shared_ptr<IGraphicsFence> createFence(bool shareable) override {
            ComPtr<ID3D12Fence> fence;
            CHECK_HRCMD(m_device->CreateFence(0,
                                              shareable ? D3D12_FENCE_FLAG_SHARED : D3D12_FENCE_FLAG_NONE,
                                              IID_PPV_ARGS(fence.ReleaseAndGetAddressOf())));
            return std::make_shared<D3D12Fence>(fence.Get(), m_commandQueue.Get(), shareable);
        }

        std::shared_ptr<IGraphicsFence> openFence(const ShareableHandle& handle) override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local,
                                   "D3D12Fence_Import",
                                   TLArg(!handle.isNtHandle ? handle.handle : handle.ntHandle.get(), "Handle"),
                                   TLArg(handle.isNtHandle, "IsNTHandle"));

            if (!handle.isNtHandle) {
                throw std::runtime_error("Must be NTHANDLE");
            }

            ComPtr<ID3D12Fence> fence;
            CHECK_HRCMD(m_device->OpenSharedHandle(handle.isNtHandle ? handle.ntHandle.get() : handle.handle,
                                                   IID_PPV_ARGS(fence.ReleaseAndGetAddressOf())));

            std::shared_ptr<IGraphicsFence> result =
                std::make_shared<D3D12Fence>(fence.Get(), m_commandQueue.Get(), false /* shareable */);

            TraceLoggingWriteStop(local, "D3D12Fence_Import", TLPArg(result.get(), "Fence"));

            return result;
        }

        std::shared_ptr<IGraphicsTexture> createTexture(const XrSwapchainCreateInfo& info, bool shareable) override {
            D3D12_RESOURCE_DESC desc{};
            desc.Format = (DXGI_FORMAT)info.format;
            desc.Width = info.width;
            desc.Height = info.height;
            desc.DepthOrArraySize = info.arraySize;
            desc.MipLevels = info.mipCount;
            desc.SampleDesc.Count = info.sampleCount;
            desc.Flags = D3D12_RESOURCE_FLAG_NONE;
            D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
            if (info.usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) {
                desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
                initialState = D3D12_RESOURCE_STATE_RENDER_TARGET;
            }
            if (info.usageFlags & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
                initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            }
            if (!(info.usageFlags & XR_SWAPCHAIN_USAGE_SAMPLED_BIT)) {
                desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
            }
            if (info.usageFlags & XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT) {
                desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            }

            ComPtr<ID3D12Resource> texture;
            D3D12_HEAP_PROPERTIES heapType{};
            heapType.Type = D3D12_HEAP_TYPE_DEFAULT;
            heapType.CreationNodeMask = heapType.VisibleNodeMask = 1;
            CHECK_HRCMD(m_device->CreateCommittedResource(&heapType,
                                                          shareable ? D3D12_HEAP_FLAG_SHARED : D3D12_HEAP_FLAG_NONE,
                                                          &desc,
                                                          initialState,
                                                          nullptr,
                                                          IID_PPV_ARGS(texture.ReleaseAndGetAddressOf())));
            return std::make_shared<D3D12Texture>(texture.Get());
        }

        std::shared_ptr<IGraphicsTexture> openTexture(const ShareableHandle& handle,
                                                      const XrSwapchainCreateInfo& info) override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local,
                                   "D3D12Texture_Import",
                                   TLArg(!handle.isNtHandle ? handle.handle : handle.ntHandle.get(), "Handle"),
                                   TLArg(handle.isNtHandle, "IsNTHandle"));

            ComPtr<ID3D12Resource> texture;
            CHECK_HRCMD(m_device->OpenSharedHandle(handle.isNtHandle ? handle.ntHandle.get() : handle.handle,
                                                   IID_PPV_ARGS(texture.ReleaseAndGetAddressOf())));

            std::shared_ptr<IGraphicsTexture> result = std::make_shared<D3D12Texture>(texture.Get());

            TraceLoggingWriteStop(local, "D3D12Texture_Import", TLPArg(result.get(), "Texture"));

            return result;
        }

        std::shared_ptr<IGraphicsTexture> openTexturePtr(void* nativeTexturePtr,
                                                         const XrSwapchainCreateInfo& info) override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D12Texture_Import", TLPArg(nativeTexturePtr, "D3D12Texture"));

            ID3D12Resource* texture = reinterpret_cast<ID3D12Resource*>(nativeTexturePtr);

            std::shared_ptr<IGraphicsTexture> result = std::make_shared<D3D12Texture>(texture);

            TraceLoggingWriteStop(local, "D3D12Texture_Import", TLPArg(result.get(), "Texture"));

            return result;
        }

        void copyTexture(IGraphicsTexture* from, IGraphicsTexture* to) override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "D3D12Texture_Copy", TLPArg(from, "Source"), TLPArg(to, "Destination"));

            D3D12ReusableCommandList commandList = getCommandList();
            commandList.commandList->CopyResource(to->getNativeTexture<D3D12>(), from->getNativeTexture<D3D12>());
            submitCommandList(std::move(commandList));

            TraceLoggingWriteStop(local, "D3D12Texture_Copy");
        }

        GenericFormat translateToGenericFormat(int64_t format) const override {
            return (DXGI_FORMAT)format;
        }

        int64_t translateFromGenericFormat(GenericFormat format) const override {
            return (int64_t)format;
        }

        LUID getAdapterLuid() const override {
            return m_device->GetAdapterLuid();
        }

        D3D12ReusableCommandList getCommandList() {
            std::unique_lock lock(m_commandListPoolMutex);

            if (m_availableCommandList.empty()) {
                // Recycle completed command lists.
                while (!m_pendingCommandList.empty() && m_commandListPoolFence->GetCompletedValue() >=
                                                            m_pendingCommandList.front().completedFenceValue) {
                    m_availableCommandList.push_back(std::move(m_pendingCommandList.front()));
                    m_pendingCommandList.pop_front();
                }
            }

            D3D12ReusableCommandList commandList;
            if (m_availableCommandList.empty()) {
                // Allocate a new command list if needed.
                CHECK_HRCMD(m_device->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandList.allocator.ReleaseAndGetAddressOf())));
                CHECK_HRCMD(
                    m_device->CreateCommandList(0,
                                                D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                commandList.allocator.Get(),
                                                nullptr,
                                                IID_PPV_ARGS(commandList.commandList.ReleaseAndGetAddressOf())));
            } else {
                commandList = m_availableCommandList.front();
                m_availableCommandList.pop_front();

                // Reset the command list before reuse.
                CHECK_HRCMD(commandList.commandList->Reset(commandList.allocator.Get(), nullptr));
            }
            return commandList;
        }

        void submitCommandList(D3D12ReusableCommandList commandList) {
            std::unique_lock lock(m_commandListPoolMutex);

            CHECK_HRCMD(commandList.commandList->Close());
            m_commandQueue->ExecuteCommandLists(
                1, reinterpret_cast<ID3D12CommandList**>(commandList.commandList.GetAddressOf()));
            commandList.completedFenceValue = m_commandListPoolFenceValue + 1;
            m_commandQueue->Signal(m_commandListPoolFence.Get(), commandList.completedFenceValue);
            m_pendingCommandList.push_back(std::move(commandList));
        }

        const ComPtr<ID3D12Device> m_device;
        const ComPtr<ID3D12CommandQueue> m_commandQueue;

        std::mutex m_commandListPoolMutex;
        std::deque<D3D12ReusableCommandList> m_availableCommandList;
        std::deque<D3D12ReusableCommandList> m_pendingCommandList;
        ComPtr<ID3D12Fence> m_commandListPoolFence;
        uint32_t m_commandListPoolFenceValue{0};
    };

} // namespace

namespace openxr_api_layer::utils::graphics::internal {

    std::shared_ptr<IGraphicsDevice> wrapApplicationDevice(const XrGraphicsBindingD3D12KHR& bindings) {
        return std::make_shared<D3D12GraphicsDevice>(bindings.device, bindings.queue);
    }

} // namespace openxr_api_layer::utils::graphics::internal

#endif
