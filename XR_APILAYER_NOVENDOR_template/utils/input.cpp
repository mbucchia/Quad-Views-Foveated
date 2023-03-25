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

#include "inputs.h"

namespace {

    using namespace openxr_api_layer::utils::inputs;

    struct InputFramework : IInputFramework {
        InputFramework(const XrInstanceCreateInfo& instanceInfo,
                       XrInstance instance,
                       PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr_,
                       const XrSessionCreateInfo& sessionInfo,
                       XrSession session,
                       InputMethod methods)
            : m_instance(instance), xrGetInstanceProcAddr(xrGetInstanceProcAddr_), m_session(session) {
        }

        ~InputFramework() override {
        }

        XrSession getSessionHandle() const override {
            return m_session;
        }

        void setSessionData(std::unique_ptr<IInputSessionData> sessionData) override {
            m_sessionData = std::move(sessionData);
        }

        IInputSessionData* getSessionDataPtr() const override {
            return m_sessionData.get();
        }

        const XrInstance m_instance;
        const PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr;
        const XrSession m_session;

        std::unique_ptr<IInputSessionData> m_sessionData;
    };

    struct InputFrameworkFactory : IInputFrameworkFactory {
        InputFrameworkFactory(const XrInstanceCreateInfo& instanceInfo,
                              XrInstance instance,
                              PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr_,
                              InputMethod methods)
            : m_instanceInfo(instanceInfo), m_instance(instance), xrGetInstanceProcAddr(xrGetInstanceProcAddr_),
              m_methods(methods) {
            {
                std::unique_lock lock(factoryMutex);
                if (factory) {
                    throw std::runtime_error("There can only be one InputFramework factory");
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

            CHECK_XRCMD(xrGetInstanceProcAddr(
                instance, "xrCreateSession", reinterpret_cast<PFN_xrVoidFunction*>(&xrCreateSession)));
            CHECK_XRCMD(xrGetInstanceProcAddr(
                instance, "xrDestroySession", reinterpret_cast<PFN_xrVoidFunction*>(&xrDestroySession)));
        }

        void xrGetInstanceProcAddr_post(XrInstance instance, const char* name, PFN_xrVoidFunction* function) override {
            const std::string_view functionName(name);
            if (functionName == "xrCreateSession") {
                *function = reinterpret_cast<PFN_xrVoidFunction>(hookCreateSession);
            } else if (functionName == "xrCreateSession") {
                *function = reinterpret_cast<PFN_xrVoidFunction>(hookCreateSession);
            }
        }

        IInputFramework* getInputFramework(XrSession session) override {
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
                    std::move(std::make_unique<InputFramework>(
                        m_instanceInfo, m_instance, xrGetInstanceProcAddr, *createInfo, *session, m_methods)));
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
        const InputMethod m_methods;
        XrInstanceCreateInfo m_instanceInfo;
        std::vector<std::string> m_instanceExtensions;
        std::vector<const char*> m_instanceExtensionsArray;

        std::mutex m_sessionsMutex;
        std::unordered_map<XrSession, std::unique_ptr<InputFramework>> m_sessions;

        PFN_xrCreateSession xrCreateSession{nullptr};
        PFN_xrDestroySession xrDestroySession{nullptr};

        static inline std::mutex factoryMutex;
        static inline InputFrameworkFactory* factory{nullptr};

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

namespace openxr_api_layer::utils::inputs {

    std::shared_ptr<IInputFrameworkFactory> createInputFrameworkFactory(const XrInstanceCreateInfo& instanceInfo,
                                                                        XrInstance instance,
                                                                        PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr,
                                                                        InputMethod methods) {
        return std::make_shared<InputFrameworkFactory>(instanceInfo, instance, xrGetInstanceProcAddr, methods);
    }

} // namespace openxr_api_layer::utils::inputs
