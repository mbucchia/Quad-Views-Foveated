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

namespace openxr_api_layer::utils::inputs {

    // Input methods to use.
    enum class InputMethod {
    };
    DEFINE_ENUM_FLAG_OPERATORS(InputMethod);

    // A container for user session data.
    // This class is meant to be extended by a caller before use with IInputFramework::setSessionData() and
    // IInputFramework::getSessionData().
    struct IInputSessionData {
        virtual ~IInputSessionData() = default;
    };

    // A collection of hooks and utilities to perform Input in the layer.
    struct IInputFramework {
        virtual ~IInputFramework() = default;

        virtual XrSession getSessionHandle() const = 0;

        virtual void setSessionData(std::unique_ptr<IInputSessionData> sessionData) = 0;
        virtual IInputSessionData* getSessionDataPtr() const = 0;

        template <typename SessionData>
        typename SessionData* getSessionData() const {
            return reinterpret_cast<SessionData*>(getSessionDataPtr());
        }
    };

    // A factory to create Input frameworks for each session.
    struct IInputFrameworkFactory {
        virtual ~IInputFrameworkFactory() = default;

        // Must be called after chaining to the upstream xrGetInstanceProcAddr() implementation.
        virtual void xrGetInstanceProcAddr_post(XrInstance instance,
                                                const char* name,
                                                PFN_xrVoidFunction* function) = 0;

        virtual IInputFramework* getInputFramework(XrSession session) = 0;
    };

    std::shared_ptr<IInputFrameworkFactory> createInputFrameworkFactory(const XrInstanceCreateInfo& info,
                                                                        XrInstance instance,
                                                                        PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr,
                                                                        InputMethod methods);

} // namespace openxr_api_layer::utils::inputs
