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

#include "graphics.h"

#if defined(XR_USE_GRAPHICS_API_D3D11) || defined(XR_USE_GRAPHICS_API_D3D12)

namespace {

    using namespace openxr_api_layer::utils::graphics;

    bool isSRGBFormat(DXGI_FORMAT format) {
        return format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
               format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB || format == DXGI_FORMAT_BC1_UNORM_SRGB ||
               format == DXGI_FORMAT_BC2_UNORM_SRGB || format == DXGI_FORMAT_BC3_UNORM_SRGB ||
               format == DXGI_FORMAT_BC7_UNORM_SRGB;
    }

    bool isDepthFormat(DXGI_FORMAT format) {
        return format == DXGI_FORMAT_D16_UNORM || format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
               format == DXGI_FORMAT_D32_FLOAT || format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    }

    struct SwapchainImage : ISwapchainImage {
        SwapchainImage(std::shared_ptr<IGraphicsTexture> textureOnApplicationDevice,
                       std::shared_ptr<IGraphicsTexture> textureOnCompositionDevice,
                       uint32_t index)
            : m_textureOnApplicationDevice(textureOnApplicationDevice), m_textureForRead(textureOnCompositionDevice),
              m_textureForWrite(textureOnCompositionDevice), m_index(index) {
        }

        IGraphicsTexture* getApplicationTexture() const override {
            return m_textureOnApplicationDevice.get();
        }

        IGraphicsTexture* getTextureForRead() const override {
            return m_textureForRead.get();
        }

        IGraphicsTexture* getTextureForWrite() const override {
            return m_textureForWrite.get();
        }

        uint32_t getIndex() const override {
            return m_index;
        }

        const std::shared_ptr<IGraphicsTexture> m_textureOnApplicationDevice;
        const std::shared_ptr<IGraphicsTexture> m_textureForRead;
        const std::shared_ptr<IGraphicsTexture> m_textureForWrite;
        const uint32_t m_index;
    };

    struct SubmittableSwapchain : ISwapchain {
        SubmittableSwapchain(PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr,
                             XrInstance instance,
                             XrSwapchain swapchain,
                             const XrSwapchainCreateInfo& infoOnApplicationDevice,
                             IGraphicsDevice* applicationDevice,
                             IGraphicsDevice* compositionDevice,
                             SwapchainMode mode,
                             bool hasOwnership = true)
            : m_swapchain(swapchain), m_infoOnCompositionDevice(infoOnApplicationDevice),
              m_formatOnApplicationDevice(infoOnApplicationDevice.format), m_applicationDevice(applicationDevice),
              m_compositionDevice(compositionDevice),
              m_accessForRead((mode & SwapchainMode::Read) == SwapchainMode::Read),
              m_accessForWrite((mode & SwapchainMode::Write) == SwapchainMode::Write) {
            CHECK_XRCMD(xrGetInstanceProcAddr(
                instance, "xrAcquireSwapchainImage", reinterpret_cast<PFN_xrVoidFunction*>(&xrAcquireSwapchainImage)));
            CHECK_XRCMD(xrGetInstanceProcAddr(
                instance, "xrWaitSwapchainImage", reinterpret_cast<PFN_xrVoidFunction*>(&xrWaitSwapchainImage)));
            CHECK_XRCMD(xrGetInstanceProcAddr(
                instance, "xrReleaseSwapchainImage", reinterpret_cast<PFN_xrVoidFunction*>(&xrReleaseSwapchainImage)));
            CHECK_XRCMD(xrGetInstanceProcAddr(instance,
                                              "xrEnumerateSwapchainImages",
                                              reinterpret_cast<PFN_xrVoidFunction*>(&xrEnumerateSwapchainImages)));
            if (hasOwnership) {
                CHECK_XRCMD(xrGetInstanceProcAddr(
                    instance, "xrDestroySwapchain", reinterpret_cast<PFN_xrVoidFunction*>(&xrDestroySwapchain)));
            }

            // Translate from the app device format to the composition device format.
            m_infoOnCompositionDevice.format = m_compositionDevice->translateFromGenericFormat(
                m_applicationDevice->translateToGenericFormat(m_infoOnCompositionDevice.format));

            // Enumerate all the swapchain images.
            uint32_t imagesCount;
            CHECK_XRCMD(xrEnumerateSwapchainImages(m_swapchain, 0, &imagesCount, nullptr));
            std::vector<std::shared_ptr<IGraphicsTexture>> textures;
            switch (m_applicationDevice->getApi()) {
#ifdef XR_USE_GRAPHICS_API_D3D11
            case Api::D3D11: {
                std::vector<XrSwapchainImageD3D11KHR> images(imagesCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
                CHECK_XRCMD(xrEnumerateSwapchainImages(m_swapchain,
                                                       imagesCount,
                                                       &imagesCount,
                                                       reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())));
                for (const XrSwapchainImageD3D11KHR& image : images) {
                    textures.push_back(m_applicationDevice->openTexture<D3D11>(image.texture, infoOnApplicationDevice));
                }
            } break;
#endif
#ifdef XR_USE_GRAPHICS_API_D3D12
            case Api::D3D12: {
                std::vector<XrSwapchainImageD3D12KHR> images(imagesCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
                CHECK_XRCMD(xrEnumerateSwapchainImages(m_swapchain,
                                                       imagesCount,
                                                       &imagesCount,
                                                       reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())));
                for (const XrSwapchainImageD3D12KHR& image : images) {
                    textures.push_back(m_applicationDevice->openTexture<D3D12>(image.texture, infoOnApplicationDevice));
                }
            } break;
#endif
            default:
                throw std::runtime_error("Composition graphics API is not supported");
            }

            // Make the images available on the composition device.
            uint32_t index = 0;
            for (std::shared_ptr<IGraphicsTexture>& textureOnApplicationDevice : textures) {
                // TODO: Varjo D3D12 support does not seem to truly shareable and requires a quirk.
                if (textureOnApplicationDevice->isShareable()) {
                    const std::shared_ptr<IGraphicsTexture> textureOnCompositionDevice =
                        m_compositionDevice->openTexture(textureOnApplicationDevice->getTextureHandle(),
                                                         m_infoOnCompositionDevice);
                    m_images.push_back(std::make_unique<SwapchainImage>(
                        textureOnApplicationDevice, textureOnCompositionDevice, index));
                } else {
                    // If the swapchain image isn't shareable, we will need to create a copy accessible on both the
                    // application and composition device, and make sure to perform copy operations as needed.
                    // TODO: Reduce memory occupation by using a shared texture at the IGraphicsDevice level.
                    if (!m_bounceBufferOnApplicationDevice) {
                        m_bounceBufferOnCompositionDevice =
                            m_compositionDevice->createTexture(m_infoOnCompositionDevice, true /* shareable */);
                        m_bounceBufferOnApplicationDevice = m_applicationDevice->openTexture(
                            m_bounceBufferOnCompositionDevice->getTextureHandle(), infoOnApplicationDevice);
                    }
                    m_images.push_back(std::make_unique<SwapchainImage>(
                        textureOnApplicationDevice, m_bounceBufferOnCompositionDevice, index));
                }

                index++;
            }

            // A fence to be used to synchronize between the application/runtime and the composition device.
            m_fenceOnCompositionDevice = m_compositionDevice->createFence();
            m_fenceOnApplicationDevice = m_applicationDevice->openFence(m_fenceOnCompositionDevice->getFenceHandle());
        }

        ~SubmittableSwapchain() override {
            if (m_fenceOnApplicationDevice) {
                m_fenceOnApplicationDevice->waitOnCpu(m_fenceValue);
            }
            if (m_fenceOnCompositionDevice) {
                m_fenceOnCompositionDevice->waitOnCpu(m_fenceValue);
            }
            if (xrDestroySwapchain) {
                xrDestroySwapchain(m_swapchain);
            }
        }

        ISwapchainImage* acquireImage(bool wait) override {
            std::unique_lock lock(m_mutex);

            uint32_t index;
            CHECK_XRCMD(xrAcquireSwapchainImage(m_swapchain, nullptr, &index));
            if (wait) {
                XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                waitInfo.timeout = XR_INFINITE_DURATION;
                CHECK_XRCMD(xrWaitSwapchainImage(m_swapchain, &waitInfo));
            }

            // Serialize the operations on the application device that might have occurred when acquiring the swapchain
            // image.
            m_fenceValue++;
            m_fenceOnApplicationDevice->signal(m_fenceValue);
            m_fenceOnCompositionDevice->waitOnDevice(m_fenceValue);

            m_acquiredImages.push_back(index);
            return m_images[index].get();
        }

        void waitImage() override {
            // We don't need to check that an image was acquired since OpenXR will do it for us and throw an error
            // below.
            XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            waitInfo.timeout = XR_INFINITE_DURATION;
            CHECK_XRCMD(xrWaitSwapchainImage(m_swapchain, &waitInfo));
        }

        void releaseImage() override {
            std::unique_lock lock(m_mutex);

            // We defer release of the OpenXR swapchain to ensure that we will have an opportunity to peek and/or poke
            // its content. If the same swapchain is released multiple times, then only defer the most recent call.
            if (!(m_accessForRead || m_accessForWrite) || m_lastReleasedImage.has_value()) {
                CHECK_XRCMD(xrReleaseSwapchainImage(m_swapchain, nullptr));
            } else if (m_acquiredImages.empty()) {
                throw std::runtime_error("No image was acquired");
            }

            m_lastReleasedImage = m_acquiredImages.front();
            m_acquiredImages.pop_front();
        }

        ISwapchainImage* getLastReleasedImage() const override {
            if (!m_accessForRead) {
                throw std::runtime_error("Not a readable swapchain");
            }

            // If the swapchain has no new released image, then there should be no work to do by the caller.
            if (!m_lastReleasedImage.has_value()) {
                return nullptr;
            }

            if (m_bounceBufferOnApplicationDevice) {
                // The swapchain image wasn't shareable and we must perform a copy to a shareable texture accessible on
                // the composition device.
                m_applicationDevice->copyTexture(m_images[m_lastReleasedImage.value()]->getApplicationTexture(),
                                                 m_bounceBufferOnApplicationDevice.get());
            }

            // Serialize the operations on the application device before accessing from the composition device.
            m_fenceValue++;
            m_fenceOnApplicationDevice->signal(m_fenceValue);
            m_fenceOnCompositionDevice->waitOnDevice(m_fenceValue);

            return m_images[m_lastReleasedImage.value()].get();
        }

        void commitLastReleasedImage() override {
            if (!m_accessForWrite) {
                throw std::runtime_error("Not a writable swapchain");
            }

            // If the swapchain has no new released image, then there is no work to commit.
            if (!m_lastReleasedImage.has_value()) {
                return;
            }

            // Serialize the operations on the composition device before copying to the application device or releasing
            // the swapchain image.
            m_fenceValue++;
            m_fenceOnCompositionDevice->signal(m_fenceValue);
            m_fenceOnApplicationDevice->waitOnDevice(m_fenceValue);

            if (m_bounceBufferOnApplicationDevice) {
                // The swapchain image wasn't shareable and we must perform a copy from a shareable texture written on
                // the composition device.
                m_applicationDevice->copyTexture(m_bounceBufferOnApplicationDevice.get(),
                                                 m_images[m_lastReleasedImage.value()]->getApplicationTexture());
            }

            CHECK_XRCMD(xrReleaseSwapchainImage(m_swapchain, nullptr));
            m_lastReleasedImage = {};
        }

        const XrSwapchainCreateInfo& getInfoOnCompositionDevice() const override {
            return m_infoOnCompositionDevice;
        }

        int64_t getFormatOnApplicationDevice() const override {
            return m_formatOnApplicationDevice;
        }

        ISwapchainImage* getImage(uint32_t index) const override {
            return m_images[index].get();
        }

        uint32_t getLength() const override {
            return (uint32_t)m_images.size();
        }

        XrSwapchain getSwapchainHandle() const override {
            return m_swapchain;
        }

        XrSwapchainSubImage getSubImage() const override {
            XrSwapchainSubImage subImage{};
            subImage.swapchain = m_swapchain;
            subImage.imageArrayIndex = 0;
            subImage.imageRect.offset.x = subImage.imageRect.offset.y = 0;
            subImage.imageRect.extent.width = m_infoOnCompositionDevice.width;
            subImage.imageRect.extent.height = m_infoOnCompositionDevice.height;
            return subImage;
        }

        const XrSwapchain m_swapchain;
        const int64_t m_formatOnApplicationDevice;
        IGraphicsDevice* const m_compositionDevice;
        IGraphicsDevice* const m_applicationDevice;
        const bool m_accessForRead;
        const bool m_accessForWrite;

        PFN_xrAcquireSwapchainImage xrAcquireSwapchainImage{nullptr};
        PFN_xrWaitSwapchainImage xrWaitSwapchainImage{nullptr};
        PFN_xrReleaseSwapchainImage xrReleaseSwapchainImage{nullptr};
        PFN_xrEnumerateSwapchainImages xrEnumerateSwapchainImages{nullptr};
        PFN_xrDestroySwapchain xrDestroySwapchain{nullptr};

        XrSwapchainCreateInfo m_infoOnCompositionDevice;

        std::vector<std::unique_ptr<ISwapchainImage>> m_images;
        std::shared_ptr<IGraphicsTexture> m_bounceBufferOnApplicationDevice;
        std::shared_ptr<IGraphicsTexture> m_bounceBufferOnCompositionDevice;
        std::shared_ptr<IGraphicsFence> m_fenceOnApplicationDevice;
        std::shared_ptr<IGraphicsFence> m_fenceOnCompositionDevice;
        mutable uint64_t m_fenceValue{0};

        std::mutex m_mutex;
        std::deque<uint32_t> m_acquiredImages;
        std::optional<uint32_t> m_lastReleasedImage{};
    };

    // A non-submittable swapchain must be accessible on both the application & composition device, however because it
    // does not need to be submitted, we can create the textures ourselves to ensure shareability and avoid extra
    // copies.
    struct NonSubmittableSwapchain : ISwapchain {
        NonSubmittableSwapchain(const XrSwapchainCreateInfo& infoOnApplicationDevice,
                                IGraphicsDevice* applicationDevice,
                                IGraphicsDevice* compositionDevice,
                                SwapchainMode mode)
            : m_infoOnCompositionDevice(infoOnApplicationDevice),
              m_formatOnApplicationDevice(infoOnApplicationDevice.format),
              m_accessForRead((mode & SwapchainMode::Read) == SwapchainMode::Read),
              m_accessForWrite((mode & SwapchainMode::Write) == SwapchainMode::Write) {
            // Translate from the app device format to the composition device format.
            m_infoOnCompositionDevice.format = compositionDevice->translateFromGenericFormat(
                applicationDevice->translateToGenericFormat(infoOnApplicationDevice.format));

            // We only need 2 textures since OpenXR only allows for 1 frame in-flight and we won't submit textures to a
            // compositor that might need >2 images of history.
            // Make the textures available on the composition device.
            for (uint32_t i = 0; i < 2; i++) {
                const std::shared_ptr<IGraphicsTexture> textureOnCompositionDevice =
                    compositionDevice->createTexture(m_infoOnCompositionDevice, true /* shareable */);
                const std::shared_ptr<IGraphicsTexture> textureOnApplicationDevice = applicationDevice->openTexture(
                    textureOnCompositionDevice->getTextureHandle(), infoOnApplicationDevice);
                m_images.push_back(
                    std::make_unique<SwapchainImage>(textureOnApplicationDevice, textureOnCompositionDevice, i));
            }
        }

        ISwapchainImage* acquireImage(bool wait) override {
            std::unique_lock lock(m_mutex);

            if (m_acquiredImages.size() == m_images.size()) {
                throw std::runtime_error("No image available to acquire");
            }

            const uint32_t index = m_nextImage++;
            m_acquiredImages.push_back(index);
            return m_images[index].get();
        }

        void waitImage() override {
            std::unique_lock lock(m_mutex);

            if (m_acquiredImages.empty()) {
                throw std::runtime_error("No image was acquired");
            }
        }

        void releaseImage() override {
            std::unique_lock lock(m_mutex);

            if (m_acquiredImages.empty()) {
                throw std::runtime_error("No image was acquired");
            }

            m_lastReleasedImage = m_acquiredImages.front();
            m_acquiredImages.pop_front();
        }

        ISwapchainImage* getLastReleasedImage() const override {
            if (!m_accessForRead) {
                throw std::runtime_error("Not a readable swapchain");
            }

            return m_images[m_lastReleasedImage].get();
        }

        void commitLastReleasedImage() override {
            if (!m_accessForWrite) {
                throw std::runtime_error("Not a writable swapchain");
            }
        }

        const XrSwapchainCreateInfo& getInfoOnCompositionDevice() const override {
            return m_infoOnCompositionDevice;
        }

        int64_t getFormatOnApplicationDevice() const override {
            return m_formatOnApplicationDevice;
        }

        ISwapchainImage* getImage(uint32_t index) const override {
            return m_images[index].get();
        }

        uint32_t getLength() const override {
            return (uint32_t)m_images.size();
        }

        XrSwapchain getSwapchainHandle() const override {
            throw std::runtime_error("Not a submittable swapchain");
        }

        XrSwapchainSubImage getSubImage() const override {
            throw std::runtime_error("Not a submittable swapchain");
        }

        const int64_t m_formatOnApplicationDevice;
        const bool m_accessForRead;
        const bool m_accessForWrite;

        XrSwapchainCreateInfo m_infoOnCompositionDevice;

        std::vector<std::unique_ptr<ISwapchainImage>> m_images;

        std::mutex m_mutex;
        uint32_t m_nextImage{0};
        std::deque<uint32_t> m_acquiredImages;
        uint32_t m_lastReleasedImage{};
    };

    struct CompositionFramework : ICompositionFramework {
        CompositionFramework(const XrInstanceCreateInfo& instanceInfo,
                             XrInstance instance,
                             PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr_,
                             const XrSessionCreateInfo& sessionInfo,
                             XrSession session,
                             CompositionApi compositionApi)
            : m_instance(instance), xrGetInstanceProcAddr(xrGetInstanceProcAddr_), m_session(session) {
            CHECK_XRCMD(xrGetInstanceProcAddr(
                m_instance, "xrCreateSwapchain", reinterpret_cast<PFN_xrVoidFunction*>(&xrCreateSwapchain)));

            // Detect which graphics bindings to look for.
#ifdef XR_USE_GRAPHICS_API_D3D11
            bool has_XR_KHR_D3D11_enable = false;
#endif
#ifdef XR_USE_GRAPHICS_API_D3D12
            bool has_XR_KHR_D3D12_enable = false;
#endif
            for (uint32_t i = 0; i < instanceInfo.enabledExtensionCount; i++) {
                const std::string_view extensionName(instanceInfo.enabledExtensionNames[i]);

#ifdef XR_USE_GRAPHICS_API_D3D11
                if (extensionName == XR_KHR_D3D11_ENABLE_EXTENSION_NAME) {
                    has_XR_KHR_D3D11_enable = true;
                }
#endif
#ifdef XR_USE_GRAPHICS_API_D3D12
                if (extensionName == XR_KHR_D3D12_ENABLE_EXTENSION_NAME) {
                    has_XR_KHR_D3D12_enable = true;
                }
#endif
            }

            // Wrap the application device.
            const XrBaseInStructure* entry = reinterpret_cast<const XrBaseInStructure*>(sessionInfo.next);
            while (entry && !m_applicationDevice) {
#ifdef XR_USE_GRAPHICS_API_D3D11
                if (has_XR_KHR_D3D11_enable && entry->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
                    m_applicationDevice =
                        internal::wrapApplicationDevice(*reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(entry));
                    break;
                }
#endif
#ifdef XR_USE_GRAPHICS_API_D3D12
                if (has_XR_KHR_D3D12_enable && entry->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR) {
                    m_applicationDevice =
                        internal::wrapApplicationDevice(*reinterpret_cast<const XrGraphicsBindingD3D12KHR*>(entry));
                    break;
                }
#endif
                entry = entry->next;
            }

            if (!m_applicationDevice) {
                throw std::runtime_error("Application graphics API is not supported");
            }

            // Create the device for composition according to the API layer's request.
            switch (compositionApi) {
#ifdef XR_USE_GRAPHICS_API_D3D11
            case CompositionApi::D3D11:
                m_compositionDevice = internal::createD3D11CompositionDevice(m_applicationDevice->getAdapterLuid());
                break;
#endif
            default:
                throw std::runtime_error("Composition graphics API is not supported");
            }

            m_fenceOnCompositionDevice = m_compositionDevice->createFence();
            m_fenceOnApplicationDevice = m_applicationDevice->openFence(m_fenceOnCompositionDevice->getFenceHandle());

            // Get the preferred formats for swapchains.
            PFN_xrEnumerateSwapchainFormats xrEnumerateSwapchainFormats;
            CHECK_XRCMD(xrGetInstanceProcAddr(m_instance,
                                              "xrEnumerateSwapchainFormats",
                                              reinterpret_cast<PFN_xrVoidFunction*>(&xrEnumerateSwapchainFormats)));
            uint32_t formatsCount;
            CHECK_XRCMD(xrEnumerateSwapchainFormats(m_session, 0, &formatsCount, nullptr));
            std::vector<int64_t> formats(formatsCount);
            CHECK_XRCMD(xrEnumerateSwapchainFormats(m_session, formatsCount, &formatsCount, formats.data()));
            for (const int64_t formatOnApplicationDevice : formats) {
                const DXGI_FORMAT format = m_applicationDevice->translateToGenericFormat(formatOnApplicationDevice);
                const bool isDepth = isDepthFormat(format);
                const bool isColor = !isDepth;
                const bool isSRGB = isColor && isSRGBFormat(format);

                if (m_preferredColorFormat == DXGI_FORMAT_UNKNOWN && isColor && !isSRGB) {
                    m_preferredColorFormat = format;
                }
                if (m_preferredSRGBColorFormat == DXGI_FORMAT_UNKNOWN && isColor && isSRGB) {
                    m_preferredSRGBColorFormat = format;
                }
                if (m_preferredDepthFormat == DXGI_FORMAT_UNKNOWN && isDepth) {
                    m_preferredDepthFormat = format;
                }
            }
        }

        ~CompositionFramework() {
            if (m_fenceOnCompositionDevice) {
                m_fenceOnCompositionDevice->waitOnCpu(m_fenceValue);
            }
        }

        XrSession getSessionHandle() const override {
            return m_session;
        }

        void setSessionData(std::unique_ptr<ICompositionSessionData> sessionData) override {
            m_sessionData = std::move(sessionData);
        }

        ICompositionSessionData* getSessionDataPtr() const override {
            return m_sessionData.get();
        }

        std::shared_ptr<ISwapchain> createSwapchain(const XrSwapchainCreateInfo& infoOnApplicationDevice,
                                                    SwapchainMode mode) override {
            if ((mode & SwapchainMode::Submit) == SwapchainMode::Submit) {
                XrSwapchain swapchain;
                CHECK_XRCMD(xrCreateSwapchain(m_session, &infoOnApplicationDevice, &swapchain));
                return std::make_shared<SubmittableSwapchain>(xrGetInstanceProcAddr,
                                                              m_instance,
                                                              swapchain,
                                                              infoOnApplicationDevice,
                                                              m_applicationDevice.get(),
                                                              m_compositionDevice.get(),
                                                              mode);
            } else {
                return std::make_shared<NonSubmittableSwapchain>(
                    infoOnApplicationDevice, m_applicationDevice.get(), m_compositionDevice.get(), mode);
            }
        }

        void serializePreComposition() override {
            std::unique_lock lock(m_fenceMutex);

            m_fenceValue++;
            m_fenceOnApplicationDevice->signal(m_fenceValue);
            m_fenceOnCompositionDevice->waitOnDevice(m_fenceValue);
        }

        void serializePostComposition() override {
            std::unique_lock lock(m_fenceMutex);

            m_fenceValue++;
            m_fenceOnCompositionDevice->signal(m_fenceValue);
            m_fenceOnApplicationDevice->waitOnDevice(m_fenceValue);
        }

        IGraphicsDevice* getCompositionDevice() const override {
            return m_compositionDevice.get();
        }

        IGraphicsDevice* getApplicationDevice() const override {
            return m_applicationDevice.get();
        }

        int64_t getPreferredSwapchainFormatOnApplicationDevice(XrSwapchainUsageFlags usageFlags,
                                                               bool preferSRGB) const override {
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            if (usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) {
                format = preferSRGB ? m_preferredSRGBColorFormat : m_preferredColorFormat;
            } else if (usageFlags & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                format = m_preferredDepthFormat;
            }
            return m_applicationDevice->translateFromGenericFormat(format);
        }

        const XrInstance m_instance;
        const PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr;
        const XrSession m_session;

        std::unique_ptr<ICompositionSessionData> m_sessionData;

        std::shared_ptr<IGraphicsDevice> m_compositionDevice;
        std::shared_ptr<IGraphicsDevice> m_applicationDevice;
        DXGI_FORMAT m_preferredColorFormat{DXGI_FORMAT_UNKNOWN};
        DXGI_FORMAT m_preferredSRGBColorFormat{DXGI_FORMAT_UNKNOWN};
        DXGI_FORMAT m_preferredDepthFormat{DXGI_FORMAT_UNKNOWN};

        std::mutex m_fenceMutex;
        std::shared_ptr<IGraphicsFence> m_fenceOnApplicationDevice;
        std::shared_ptr<IGraphicsFence> m_fenceOnCompositionDevice;
        uint64_t m_fenceValue{0};

        PFN_xrCreateSwapchain xrCreateSwapchain{nullptr};
    };

    struct CompositionFrameworkFactory : ICompositionFrameworkFactory {
        CompositionFrameworkFactory(const XrInstanceCreateInfo& instanceInfo,
                                    XrInstance instance,
                                    PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr_,
                                    CompositionApi compositionApi)
            : m_instanceInfo(instanceInfo), m_instance(instance), xrGetInstanceProcAddr(xrGetInstanceProcAddr_),
              m_compositionApi(compositionApi) {
            {
                std::unique_lock lock(factoryMutex);
                if (factory) {
                    throw std::runtime_error("There can only be one CompositionFramework factory");
                }
                factory = this;
            }

            // Deep-copy the instance extensions strings.
            m_instanceExtensions.reserve(m_instanceInfo.enabledExtensionCount);
            for (uint32_t i = 0; i < m_instanceInfo.enabledExtensionCount; i++) {
                m_instanceExtensions.push_back(m_instanceInfo.enabledExtensionNames[i]);
                m_instanceExtensionsArray.push_back(m_instanceExtensions.back().c_str());
            }
            m_instanceInfo.enabledExtensionNames = m_instanceExtensionsArray.data();

            // xrCreateSession() and xrDestroySession() function pointers are chained.
        }

        ~CompositionFrameworkFactory() override {
            std::unique_lock lock(factoryMutex);

            factory = nullptr;
        }

        void xrGetInstanceProcAddr_post(XrInstance instance, const char* name, PFN_xrVoidFunction* function) override {
            const std::string_view functionName(name);
            if (functionName == "xrCreateSession") {
                xrCreateSession = reinterpret_cast<PFN_xrCreateSession>(*function);
                *function = reinterpret_cast<PFN_xrVoidFunction>(hookCreateSession);
            } else if (functionName == "xrDestroySession") {
                xrDestroySession = reinterpret_cast<PFN_xrDestroySession>(*function);
                *function = reinterpret_cast<PFN_xrVoidFunction>(hookDestroySession);
            }
        }

        ICompositionFramework* getCompositionFramework(XrSession session) override {
            std::unique_lock lock(m_sessionsMutex);

            auto it = m_sessions.find(session);
            if (it == m_sessions.end()) {
                throw std::runtime_error("No session found");
            }

            return it->second.get();
        }

        XrResult xrCreateSession_subst(XrInstance instance, const XrSessionCreateInfo* createInfo, XrSession* session) {
            const XrResult result = xrCreateSession(instance, createInfo, session);
            if (XR_SUCCEEDED(result)) {
                std::unique_lock lock(m_sessionsMutex);

                m_sessions.insert_or_assign(
                    *session,
                    std::move(std::make_unique<CompositionFramework>(
                        m_instanceInfo, m_instance, xrGetInstanceProcAddr, *createInfo, *session, m_compositionApi)));
            }
            return result;
        }

        XrResult xrDestroySession_subst(XrSession session) {
            {
                std::unique_lock lock(m_sessionsMutex);

                auto it = m_sessions.find(session);
                if (it != m_sessions.end()) {
                    m_sessions.erase(it);
                }
            }
            return xrDestroySession(session);
        }

        const XrInstance m_instance;
        const PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr;
        const CompositionApi m_compositionApi;
        XrInstanceCreateInfo m_instanceInfo;
        std::vector<std::string> m_instanceExtensions;
        std::vector<const char*> m_instanceExtensionsArray;

        std::mutex m_sessionsMutex;
        std::unordered_map<XrSession, std::unique_ptr<CompositionFramework>> m_sessions;

        PFN_xrCreateSession xrCreateSession{nullptr};
        PFN_xrDestroySession xrDestroySession{nullptr};

        static inline std::mutex factoryMutex;
        static inline CompositionFrameworkFactory* factory{nullptr};

        static XrResult hookCreateSession(XrInstance instance,
                                          const XrSessionCreateInfo* createInfo,
                                          XrSession* session) {
            return factory->xrCreateSession_subst(instance, createInfo, session);
        }

        static XrResult hookDestroySession(XrSession session) {
            return factory->xrDestroySession_subst(session);
        }
    };

} // namespace

namespace openxr_api_layer::utils::graphics {

    std::shared_ptr<ICompositionFrameworkFactory>
    createCompositionFrameworkFactory(const XrInstanceCreateInfo& instanceInfo,
                                      XrInstance instance,
                                      PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr,
                                      CompositionApi compositionApi) {
        return std::make_shared<CompositionFrameworkFactory>(
            instanceInfo, instance, xrGetInstanceProcAddr, compositionApi);
    }

} // namespace openxr_api_layer::utils::graphics

#endif
