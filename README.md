# Meta Foveated Rendering via Quad Views

In layperson's terms:

This software lets you use Eye-Tracked Foveated Rendering (sometimes referred to as Dynamic Foveated Rendering) with your Meta Quest Pro headset in games using the [quad views rendering](https://github.com/mbucchia/Meta-Foveated/wiki/What-is-Quad-Views-rendering%3F) technique like Digital Combat Simulation (DCS) and Pavlov VR.

If you are using a Varjo headset, do not use this software, but instead use [Varjo-Foveated](https://github.com/mbucchia/Varjo-Foveated/wiki).

In technical terms:

This software enables OpenXR apps developed with [`XR_VARJO_quad_views`](https://registry.khronos.org/OpenXR/specs/1.0/html/xrspec.html#XR_VARJO_quad_views) and optionally [`XR_VARJO_foveated_rendering`](https://registry.khronos.org/OpenXR/specs/1.0/html/xrspec.html#XR_VARJO_foveated_rendering) to be used on Meta Quest Pro. It translates each quad view projection layer into two separate stereo projection layers, and uses the eye tracking support on the device to make the inner projection views follow the eye gaze.

DISCLAIMER: This software is distributed as-is, without any warranties or conditions of any kind. Use at your own risks.

# Details and instructions on the [the wiki](https://github.com/mbucchia/Meta-Foveated/wiki)!

## Setup

Download the latest version from the [Releases page](https://github.com/mbucchia/Meta-Foveated/releases). Find the installer program under **Assets**, file `Meta-Foveated-<version>.msi`.

More information on the [the wiki](https://github.com/mbucchia/Meta-Foveated/wiki)!

For troubleshooting, the log file can be found at `%LocalAppData%\XR_APILAYER_MBUCCHIA_meta_foveated\XR_APILAYER_MBUCCHIA_meta_foveated.log`.

## Donate

Donations are welcome and totally optional. Please use [my GitHub sponsorship page](https://github.com/sponsors/mbucchia) to make one-time or recurring donations!

Thank you!

## Special thanks

Thanks to my beta testers for helping throughout development and release (in alphabetical order):

- BARRACUDAS
- xMcCARYx
