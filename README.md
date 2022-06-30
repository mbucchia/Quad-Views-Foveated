# OpenXR API Layer template

This repository contains the source project for a basic [OpenXR API layer](https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#api-layers) template that can be customized easily.

More about [OpenXR](https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html) and the [OpenXR Loader](https://www.khronos.org/registry/OpenXR/specs/1.0/loader.html).

Prerequisites:

- Visual Studio 2019 or above;
- NuGet package manager (installed via Visual Studio Installer);
- Python 3 interpreter (installed via Visual Studio Installer or externally available in your PATH).

Limitations:

- The template is made for Windows 64-bit only.

DISCLAIMER: This software is distributed as-is, without any warranties or conditions of any kind. Use at your own risks.

## Customization

### Make sure to pull all 3 Git submodules dependencies:

- `external/OpenXR-SDK-Source`: This repository contains some critical definition files for generating and building your API layer.
- `external/OpenXR-SDK`: This repository contains the official Khronos SDK (pre-built), with the OpenXR header files needed for OpenXR development.
- `external/OpenXR-MixedReality`: This repository contains (among other things) a collection of utilities to conveniently write OpenXR code.

### Rename the project accordingly. You do not want your API layer to be called `XR_APILAYER_NOVENDOR_template`!

### Update the JSON description file `XR_APILAYER_NOVENDOR_template\XR_APILAYER_NOVENDOR_template.json`.

Rename the file accordingly, then modify the file to update the `name` and `library_path`. Update the `description` as well.

### Update the developer's install/uninstall scripts under `script\[Un]install-layer.ps1`.

You will be using these scripts to enable/disable the API layer from within your output folder (eg: run `bin\x64\Debug\Install-Layer.ps1` to activate the debug version of your layer during development).

WARNING: Keep track of the content of your registry under `HKEY_LOCAL_MACHINE\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit`. You don't want to have multiple copies of your API layer active at once!

### Update the Windows Performance Recorder Profile (WPRP) tracelogging GUID in both `scripts\Tracing.wprp` and `XR_APILAYER_NOVENDOR_template\framework\log.cpp`.

[Tracelogging](https://docs.microsoft.com/en-us/windows/win32/tracelogging/trace-logging-portal) can become very useful for debugging locally and to investigate user issues. Update the GUID associate with your traces:

In the tracing profile (used to capture logs):

```
    <EventProvider Name="CBF3ADCD-42B1-4C38-830C-91980AF201F8" Id="OpenXRTemplate" />
```

In the layer's code:

```
    // {cbf3adcd-42b1-4c38-830c-91980af201f8}
    TRACELOGGING_DEFINE_PROVIDER(g_traceProvider,
                                 "OpenXRTemplate",
                                 (0xcbf3adcd, 0x42b1, 0x4c38, 0x83, 0x0c, 0x91, 0x98, 0x0a, 0xf2, 0x01, 0xf8));
```

To capture a trace for your API layer:

- Open a command line prompt or powershell in administrator mode and in a folder where you have write permissions
- Begin recording a trace with the command: `wpr -start path\to\Tracing.wprp -filemode`
- Leave that command prompt open
- Reproduce the crash/issue
- Back to the command prompt, finish the recording with: `wpr -stop output.etl`
- These files are highly compressible!

Use an application such as [Tabnalysis](https://apps.microsoft.com/store/detail/tabnalysis/9NQLK2M4RP4J?hl=en-id&gl=ID) to inspect the content of the trace file.

### Customize the layer code

In the project's properties, update the Preprocessor Definition for `LAYER_NAMESPACE` to a namespace that fits your project better. Remember to do this for both the Debug and Release profile.

Update the `namespace` and `using namespace` declarations accordingly in the `XR_APILAYER_NOVENDOR_template\layer.cpp` and `XR_APILAYER_NOVENDOR_template\layer.h` files.

Update the pretty name for your API layer in `XR_APILAYER_NOVENDOR_template\layer.h`, by changing the string for `LayerName` and `VersionString`.

NOTE: Because an OpenXR API layer is tied to a particular instance, you may retrieve the `XrInstance` handle at any time by invoking `OpenXrApi::GetXrInstance()`.

### Declare OpenXR functions to override in `XR_APILAYER_NOVENDOR_template\framework\layer_apis.py`

Update the definitions as follows:

- `override_functions`: the list of OpenXR functions to intercept and to populate a "chain" for. To override the implementation of each function, simply implement a virtual method with the same name and prototype in the `OpenXrLayer` class in `XR_APILAYER_NOVENDOR_template\layer.cpp`. To call the real OpenXR implementation for the function, simply invoke the corresponding method in the `OpenXrApi` base class.
- `requested_functions`: the list of OpenXR functinos that your API layer may use. This list is used to create wrappers to the real OpenXR implementation without overriding the function. To invoke a function, simply call the corresponding method in the `OpenXrApi` base class.
- `extensions`: if any of the function declared in the lists above is not part of the OpenXR core spec, you must specify the corresponding extensions to search in (eg: `XR_KHR_D3D11_enable`).

### (Optional) Import definitions for graphics libraries in `XR_APILAYER_NOVENDOR_template\pch.h`

If your API layer uses graphics functions, such as the ones for Direct3D, you must add the corresponding `XR_USE_GRAPHICS_API_xxx` definition and include the necessary headers in `XR_APILAYER_NOVENDOR_template\pch.h`:

```
// Direct3D 11
#include <d3d11.h>

// OpenXR + Windows-specific definitions.
#define XR_NO_PROTOTYPES
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
```

### (Optional) Require OpenXR extensions in `XR_APILAYER_NOVENDOR_template\framework\dispatch.cpp`

If your API layer requires a specific OpenXR extension, update the `implicitExtensions` vector in `XR_APILAYER_NOVENDOR_template\framework\dispatch.cpp` to list the extensions to request.

WARNING: Not all OpenXR runtimes may support all extensions. It is recommended to query the list of supported extensions from the OpenXR runtime and only add to the `implicitExtensions` the extensions that are advertised by the platform. You can use the following code to query the list of extensions:
```
    CHECK_XRCMD(apiLayerInfo->nextInfo->nextGetInstanceProcAddr(
        XR_NULL_HANDLE,
        "xrEnumerateInstanceExtensionProperties",
        reinterpret_cast<PFN_xrVoidFunction*>(&xrEnumerateInstanceExtensionProperties)));

    uint32_t extensionsCount = 0;
    CHECK_XRCMD(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionsCount, nullptr));
    std::vector<XrExtensionProperties> extensions(extensionsCount, {XR_TYPE_EXTENSION_PROPERTIES});
    CHECK_XRCMD(xrEnumerateInstanceExtensionProperties(
        nullptr, extensionsCount, &extensionsCount, extensions.data()));
```

### (Optional) Expose an OpenXR extension

If your API layer exposes an OpenXR extension that may not be advertised by the OpenXR runtime (for example, your API layer implements an extension that may not be present on the platform, as opposed to overriding the behavior of an extension that must be advertised by the platform), update the JSON description file `XR_APILAYER_NOVENDOR_template\XR_APILAYER_NOVENDOR_template.json`:

```
    ...
    "description": "Vulkan to Direct3D 12 interop",
    "instance_extensions": [
      {
        "name": "XR_KHR_vulkan_enable",
        "extension_version": 2,
        "entrypoints": []
      },
      {
        "name": "XR_KHR_vulkan_enable2",
        "extension_version": 8,
        "entrypoints": []
      }
    ],
    ...
```

You must then "block" these extensions from being requested to the OpenXR runtime. Update the `blockedExtensions` vector in `XR_APILAYER_NOVENDOR_template\framework\dispatch.cpp` to list the extensions to block.

### (Optional) Use general tracelogging patterns

Writing the calls to `TraceLoggingWrite()` in each function implemented by your layer may be tedious... You can look at the source code of the [PimaxXR runtime](https://github.com/mbucchia/Pimax-OpenXR/tree/main/pimax-openxr), which implements the tracelogging calls for all functions of the core OpenXR specification plus a handful of extensions. You may copy/paste the `TraceLoggingWrite()` calls from it.
