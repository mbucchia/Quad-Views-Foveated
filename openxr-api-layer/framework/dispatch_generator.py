# MIT License
#
# Copyright(c) 2021-2023 Matthieu Bucchianeri
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this softwareand associated documentation files(the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions :
#
# The above copyright noticeand this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
import os
import re
import sys

# Import dependencies from the OpenXR SDK.
cur_dir = os.path.abspath(os.path.dirname(__file__))
base_dir = os.path.abspath(os.path.join(cur_dir, '..', '..'))
sdk_dir = os.path.join(base_dir, 'external', 'OpenXR-SDK-Source')
sys.path.append(os.path.join(sdk_dir, 'specification', 'scripts'))
sys.path.append(os.path.join(sdk_dir, 'src', 'scripts'))

from automatic_source_generator import AutomaticSourceOutputGenerator, AutomaticSourceGeneratorOptions
from reg import Registry
from generator import write
from xrconventions import OpenXRConventions

# Import configuration.
import layer_apis

# Sanity checks on the configuration file
if 'xrCreateInstance' in layer_apis.override_functions:
    raise Exception("xrCreateInstance() is implicitly overriden and shall not be specified in override_functions. Use the xrCreateInstance() virtual method.")
if 'xrCreateInstance' in layer_apis.requested_functions:
    raise Exception("xrCreateInstance() cannot be specified in requested_functions")

if 'xrDestroyInstance' in layer_apis.override_functions:
    raise Exception("xrDestroyInstance() is implicitly overriden and shall not be specified in override_functions. Use the OpenXrApi destructor instead.")
if 'xrCreateInstance' in layer_apis.requested_functions:
    raise Exception("xrDestroyInstance() cannot be specified in requested_functions")

if 'xrGetInstanceProcAddr' in layer_apis.override_functions:
    raise Exception("xrGetInstanceProcAddr() is implicitly overriden and shall not be specified in override_functions. Use the xrGetInstanceProcAddr() virtual method.")
if 'xrGetInstanceProcAddr' in layer_apis.requested_functions:
    raise Exception("xrGetInstanceProcAddr() cannot be specified in requested_functions. Use the m_xrGetInstanceProcAddr() class member.")


class DispatchGenOutputGenerator(AutomaticSourceOutputGenerator):
    '''Common generator utilities and formatting.'''
    def outputGeneratedHeaderWarning(self):
        warning = '''// *********** THIS FILE IS GENERATED - DO NOT EDIT ***********'''
        write(warning, file=self.outFile)

    def outputCopywriteHeader(self):
        copyright = '''// MIT License
//
// Copyright(c) 2021-2023 Matthieu Bucchianeri
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
'''
        write(copyright, file=self.outFile)

    def outputGeneratedAuthorNote(self):
        pass

    def makeParametersList(self, cmd):
        parameters_list = ""
        for param in cmd.params:
            if parameters_list:
                parameters_list += ', '
            parameters_list += param.cdecl.strip()

        return parameters_list

    def makeArgumentsList(self, cmd):
        arguments_list = ""
        for param in cmd.params:
            if arguments_list:
                arguments_list += ', '
            arguments_list += param.name

        return arguments_list

class DispatchGenCppOutputGenerator(DispatchGenOutputGenerator):
    '''Generator for dispatch.gen.cpp.'''
    def beginFile(self, genOpts):
        DispatchGenOutputGenerator.beginFile(self, genOpts)
        preamble = '''#include "pch.h"

#include <layer.h>

#include "dispatch.h"
#include "log.h"

using namespace openxr_api_layer::log;

namespace openxr_api_layer
{'''
        write(preamble, file=self.outFile)

    def endFile(self):
        generated_wrappers = self.genWrappers()
        generated_get_instance_proc_addr = self.genGetInstanceProcAddr()
        generated_create_instance = self.genCreateInstance()

        postamble = '''	std::unique_ptr<OpenXrApi> g_instance;

	void ResetInstance() {
		g_instance.reset();
	}

} // namespace openxr_api_layer
'''

        contents = f'''
	// Auto-generated wrappers for the requested APIs.
{generated_wrappers}

	// Auto-generated dispatcher handler.
{generated_get_instance_proc_addr}

	// Auto-generated create instance handler.
{generated_create_instance}

{postamble}'''

        write(contents, file=self.outFile)
        DispatchGenOutputGenerator.endFile(self)

    def genWrappers(self):
        generated = ''

        for cur_cmd in self.core_commands + self.ext_commands:
            if cur_cmd.name in (layer_apis.override_functions + ['xrDestroyInstance']):
                parameters_list = self.makeParametersList(cur_cmd)
                arguments_list = self.makeArgumentsList(cur_cmd)

                if cur_cmd.return_type is not None:
                    generated += f'''
	XrResult XRAPI_CALL {cur_cmd.name}({parameters_list})
	{{
		TraceLocalActivity(local);
		TraceLoggingWriteStart(local, "{cur_cmd.name}");

		XrResult result;
		try
		{{
			result = openxr_api_layer::GetInstance()->{cur_cmd.name}({arguments_list});
		}}
		catch (std::exception& exc)
		{{
			TraceLoggingWriteTagged(local, "{cur_cmd.name}_Error", TLArg(exc.what(), "Error"));
			ErrorLog(fmt::format("{cur_cmd.name}: {{}}\\n", exc.what()));
			result = XR_ERROR_RUNTIME_FAILURE;
		}}

		TraceLoggingWriteStop(local, "{cur_cmd.name}", TLArg(xr::ToCString(result), "Result"));
		if (XR_FAILED(result)) {{
			ErrorLog(fmt::format("{cur_cmd.name} failed with {{}}\\n", xr::ToCString(result)));
		}}

		return result;
	}}
'''
                else:
                    generated += f'''
	void XRAPI_CALL {cur_cmd.name}({parameters_list})
	{{
		TraceLocalActivity(local);
		TraceLoggingWriteStart(local, "{cur_cmd.name}");

		try
		{{
			openxr_api_layer::GetInstance()->{cur_cmd.name}({arguments_list});
		}}
		catch (std::exception& exc)
		{{
			TraceLoggingWriteTagged(local, "{cur_cmd.name}_Error", TLArg(exc.what(), "Error"));
			ErrorLog(fmt::format("{cur_cmd.name}: {{}}\\n", exc.what()));
		}}

		TraceLoggingWriteStop(local, "{cur_cmd.name}"));
	}}
'''
                
        return generated

    def genCreateInstance(self):
        generated = '''	XrResult OpenXrApi::xrCreateInstance(const XrInstanceCreateInfo* createInfo)
    {
'''

        for cur_cmd in self.core_commands:
            if cur_cmd.name in layer_apis.requested_functions:
                generated += f'''		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "{cur_cmd.name}", reinterpret_cast<PFN_xrVoidFunction*>(&m_{cur_cmd.name}))))
		{{
			throw std::runtime_error("Failed to resolve {cur_cmd.name}");
		}}
'''

        # Functions from extensions are allowed to be null.
        for cur_cmd in self.ext_commands:
            if cur_cmd.name in layer_apis.requested_functions:
                generated += f'''		m_xrGetInstanceProcAddr(m_instance, "{cur_cmd.name}", reinterpret_cast<PFN_xrVoidFunction*>(&m_{cur_cmd.name}));
'''

        generated += '''		m_applicationName = createInfo->applicationInfo.applicationName;
		return XR_SUCCESS;
	}'''

        return generated

    def genGetInstanceProcAddr(self):
        generated = '''	XrResult OpenXrApi::xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function)
	{
		XrResult result = m_xrGetInstanceProcAddr(instance, name, function);

		const std::string apiName(name);

		if (apiName == "xrDestroyInstance")
		{
			m_xrDestroyInstance = reinterpret_cast<PFN_xrDestroyInstance>(*function);
			*function = reinterpret_cast<PFN_xrVoidFunction>(openxr_api_layer::xrDestroyInstance);
		}
'''

        for cur_cmd in self.core_commands:
            if cur_cmd.name in layer_apis.override_functions:
                generated += f'''		else if (apiName == "{cur_cmd.name}")
		{{
			m_{cur_cmd.name} = reinterpret_cast<PFN_{cur_cmd.name}>(*function);
			*function = reinterpret_cast<PFN_xrVoidFunction>(openxr_api_layer::{cur_cmd.name});
		}}
'''

        # Always advertise extension functions.
        for cur_cmd in self.ext_commands:
            if cur_cmd.name in layer_apis.override_functions:
                generated += f'''		else if (apiName == "{cur_cmd.name}")
		{{
			m_{cur_cmd.name} = reinterpret_cast<PFN_{cur_cmd.name}>(*function);
			*function = reinterpret_cast<PFN_xrVoidFunction>(openxr_api_layer::{cur_cmd.name});
			result = XR_SUCCESS;
		}}
'''

        generated += '''

		return result;
	}'''

        return generated


class DispatchGenHOutputGenerator(DispatchGenOutputGenerator):
    '''Generator for dispatch.gen.h.'''
    def beginFile(self, genOpts):
        DispatchGenOutputGenerator.beginFile(self, genOpts)
        preamble = '''#pragma once

namespace openxr_api_layer
{

	void ResetInstance();

	class OpenXrApi
	{
	private:
		XrInstance m_instance{ XR_NULL_HANDLE };
		std::string m_applicationName;
		std::vector<std::string> m_grantedExtensions;

	protected:
		OpenXrApi() = default;

		PFN_xrGetInstanceProcAddr m_xrGetInstanceProcAddr{ nullptr };

	public:
		virtual ~OpenXrApi() = default;

		XrInstance GetXrInstance() const
		{
			return m_instance;
		}

		const std::string& GetApplicationName() const
		{
			return m_applicationName;
		}

		const std::vector<std::string>& GetGrantedExtensions() const
		{
			return m_grantedExtensions;
		}

		void SetGetInstanceProcAddr(PFN_xrGetInstanceProcAddr pfn_xrGetInstanceProcAddr, XrInstance instance)
		{
			m_xrGetInstanceProcAddr = pfn_xrGetInstanceProcAddr;
			m_instance = instance;
		}

		void SetGrantedExtensions(const std::vector<std::string>& grantedExtensions)
		{
			m_grantedExtensions = grantedExtensions;
		}

		// Specially-handled by the auto-generated code.
		virtual XrResult xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function);
		virtual XrResult xrCreateInstance(const XrInstanceCreateInfo* createInfo);

		// Specially-handled destruction code.
		virtual XrResult xrDestroyInstance(XrInstance instance) {
			// Invoking ResetInstance() is equivalent to `delete this;' so we must take precautions.
			PFN_xrDestroyInstance finalDestroyInstance = m_xrDestroyInstance;
			ResetInstance();
			return finalDestroyInstance(instance);
		}
	private:
		PFN_xrDestroyInstance m_xrDestroyInstance{nullptr};
'''
        write(preamble, file=self.outFile)

    def endFile(self):
        generated_virtual_methods = self.genVirtualMethods()

        postamble = '''
	};

	extern std::unique_ptr<OpenXrApi> g_instance;

} // namespace openxr_api_layer
'''

        contents = f'''
		// Auto-generated entries for the requested APIs.
{generated_virtual_methods}

{postamble}'''

        write(contents, file=self.outFile)

        DispatchGenOutputGenerator.endFile(self)

    def genVirtualMethods(self):
        generated = ''

        commands_to_include = list(set(layer_apis.override_functions + layer_apis.requested_functions))
        for cur_cmd in self.core_commands + self.ext_commands:
            if cur_cmd.name in commands_to_include:
                parameters_list = self.makeParametersList(cur_cmd)
                arguments_list = self.makeArgumentsList(cur_cmd)

                generated += '''
	public:'''

                if cur_cmd.return_type is not None:
                    generated += f'''
		virtual XrResult {cur_cmd.name}({parameters_list})
		{{
			return m_{cur_cmd.name}({arguments_list});
		}}
'''
                else:
                    generated += f'''
		virtual void {cur_cmd.name}({parameters_list})
		{{
			m_{cur_cmd.name}({arguments_list});
		}}
'''

                generated += f'''	private:
		PFN_{cur_cmd.name} m_{cur_cmd.name}{{ nullptr }};
'''
                
        return generated

def makeREstring(strings, default=None):
    """Turn a list of strings into a regexp string matching exactly those strings."""
    if strings or default is None:
        return '^(' + '|'.join((re.escape(s) for s in strings)) + ')$'
    return default

if __name__ == '__main__':
    conventions = OpenXRConventions()
    featuresPat = 'XR_VERSION_1_0'
    extensionsPat = makeREstring(layer_apis.extensions)

    registry = Registry(DispatchGenCppOutputGenerator(diagFile=None),
                        AutomaticSourceGeneratorOptions(conventions       = conventions,
                                                        filename          = 'dispatch.gen.cpp',
                                                        directory         = cur_dir,
                                                        apiname           = 'openxr',
                                                        profile           = None,
                                                        versions          = featuresPat,
                                                        emitversions      = featuresPat,
                                                        defaultExtensions = 'openxr',
                                                        addExtensions     = None,
                                                        removeExtensions  = None,
                                                        emitExtensions    = extensionsPat))
    registry.loadFile(os.path.join(sdk_dir, 'specification', 'registry', 'xr.xml'))
    registry.apiGen()

    registry = Registry(DispatchGenHOutputGenerator(diagFile=None),
                        AutomaticSourceGeneratorOptions(conventions       = conventions,
                                                        filename          = 'dispatch.gen.h',
                                                        directory         = cur_dir,
                                                        apiname           = 'openxr',
                                                        profile           = None,
                                                        versions          = featuresPat,
                                                        emitversions      = featuresPat,
                                                        defaultExtensions = 'openxr',
                                                        addExtensions     = None,
                                                        removeExtensions  = None,
                                                        emitExtensions    = extensionsPat))
    registry.loadFile(os.path.join(sdk_dir, 'specification', 'registry', 'xr.xml'))
    registry.apiGen()
