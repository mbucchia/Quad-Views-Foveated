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
// The above copyright noticeand this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "general.h"

namespace openxr_api_layer::utils::graphics {

    enum class Api {
    };
    enum class CompositionApi {
    };

    // We (arbitrarily) use DXGI as a common conversion point for all graphics APIs.
    using GenericFormat = DXGI_FORMAT;

    struct ShareableHandle {
        wil::unique_handle ntHandle;
        HANDLE handle{nullptr};
        bool isNtHandle{};
        Api origin{};
    };

    // A timer on the GPU.
    struct IGraphicsTimer : openxr_api_layer::utils::general::ITimer {
        virtual ~IGraphicsTimer() = default;

        virtual Api getApi() const = 0;
    };

    // A fence.
    struct IGraphicsFence {
        virtual ~IGraphicsFence() = default;

        virtual Api getApi() const = 0;
        virtual void* getNativeFencePtr() const = 0;
        virtual ShareableHandle getFenceHandle() const = 0;

        virtual void signal(uint64_t value) = 0;
        virtual void waitOnDevice(uint64_t value) = 0;
        virtual void waitOnCpu(uint64_t value) = 0;

        virtual bool isShareable() const = 0;

        template <typename ApiTraits>
        typename ApiTraits::Fence getNativeFence() const {
            if (ApiTraits::Api != getApi()) {
                throw std::runtime_error("Api mismatch");
            }
            return reinterpret_cast<typename ApiTraits::Fence>(getNativeFencePtr());
        }
    };

    // A texture.
    struct IGraphicsTexture {
        virtual ~IGraphicsTexture() = default;

        virtual Api getApi() const = 0;
        virtual void* getNativeTexturePtr() const = 0;
        virtual ShareableHandle getTextureHandle() const = 0;

        virtual const XrSwapchainCreateInfo& getInfo() const = 0;
        virtual bool isShareable() const = 0;

        template <typename ApiTraits>
        typename ApiTraits::Texture getNativeTexture() const {
            if (ApiTraits::Api != getApi()) {
                throw std::runtime_error("Api mismatch");
            }
            return reinterpret_cast<typename ApiTraits::Texture>(getNativeTexturePtr());
        }
    };

    // A graphics device and execution context.
    struct IGraphicsDevice {
        virtual ~IGraphicsDevice() = default;

        virtual Api getApi() const = 0;
        virtual void* getNativeDevicePtr() const = 0;
        virtual void* getNativeContextPtr() const = 0;

        virtual std::shared_ptr<IGraphicsTimer> createTimer() = 0;
        virtual std::shared_ptr<IGraphicsFence> createFence(bool shareable = true) = 0;
        virtual std::shared_ptr<IGraphicsFence> openFence(const ShareableHandle& handle) = 0;
        virtual std::shared_ptr<IGraphicsTexture> createTexture(const XrSwapchainCreateInfo& info,
                                                                bool shareable = true) = 0;
        virtual std::shared_ptr<IGraphicsTexture> openTexture(const ShareableHandle& handle,
                                                              const XrSwapchainCreateInfo& info) = 0;
        virtual std::shared_ptr<IGraphicsTexture> openTexturePtr(void* nativeTexturePtr,
                                                                 const XrSwapchainCreateInfo& info) = 0;

        virtual void copyTexture(IGraphicsTexture* from, IGraphicsTexture* to) = 0;

        virtual GenericFormat translateToGenericFormat(int64_t format) const = 0;
        virtual int64_t translateFromGenericFormat(GenericFormat format) const = 0;

        virtual LUID getAdapterLuid() const = 0;

        template <typename ApiTraits>
        typename ApiTraits::Device getNativeDevice() const {
            if (ApiTraits::Api != getApi()) {
                throw std::runtime_error("Api mismatch");
            }
            return reinterpret_cast<typename ApiTraits::Device>(getNativeDevicePtr());
        }

        template <typename ApiTraits>
        typename ApiTraits::Context getNativeContext() const {
            if (ApiTraits::Api != getApi()) {
                throw std::runtime_error("Api mismatch");
            }
            return reinterpret_cast<typename ApiTraits::Context>(getNativeContextPtr());
        }

        template <typename ApiTraits>
        std::shared_ptr<IGraphicsTexture> openTexture(typename ApiTraits::Texture nativeTexture,
                                                      const XrSwapchainCreateInfo& info) {
            if (ApiTraits::Api != getApi()) {
                throw std::runtime_error("Api mismatch");
            }
            return openTexturePtr(reinterpret_cast<void*>(nativeTexture), info);
        }
    };

} // namespace openxr_api_layer::utils::graphics
