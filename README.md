# Foveated Rendering via Quad Views

In layperson's terms:

This software lets you use Eye-Tracked Foveated Rendering (sometimes referred to as Dynamic Foveated Rendering) with your Pimax Crystal, Meta Quest Pro, and other headsets supporting eye tracking via OpenXR in games using the [quad views rendering](https://github.com/mbucchia/Quad-Views-Foveated/wiki/What-is-Quad-Views-rendering%3F) technique like Digital Combat Simulation (DCS) and Pavlov VR.

In technical terms:

This software enables OpenXR apps developed with [`XR_VARJO_quad_views`](https://registry.khronos.org/OpenXR/specs/1.0/html/xrspec.html#XR_VARJO_quad_views) and optionally [`XR_VARJO_foveated_rendering`](https://registry.khronos.org/OpenXR/specs/1.0/html/xrspec.html#XR_VARJO_foveated_rendering) to be used on platforms that do not typically support those extensions. It composes each quad view projection layer into a stereo projection layer, and uses the eye tracking support on the device to make the inner projection views follow the eye gaze.

DISCLAIMER: This software is distributed as-is, without any warranties or conditions of any kind. Use at your own risks.

# Details and instructions on the [the wiki](https://github.com/mbucchia/Quad-Views-Foveated/wiki)!

## Setup

Download the latest version from the [Releases page](https://github.com/mbucchia/Quad-Views-Foveated/releases). Find the installer program under **Assets**, file `Quad-Views-Foveated-<version>.msi`.

More information on the [the wiki](https://github.com/mbucchia/Quad-Views-Foveated/wiki)!

For troubleshooting, the log file can be found at `%LocalAppData%\Quad-Views-Foveated\Quad-Views-Foveated.log`.

## Donate

Donations are welcome and totally optional. Please use [my GitHub sponsorship page](https://github.com/sponsors/mbucchia) to make one-time or recurring donations!

Thank you!

## Special thanks

Thanks to my beta testers for helping throughout development and release (in alphabetical order):

- BARRACUDAS
- edmuss
- MastahFR
- mfrisby
- Omniwhatever
- xMcCARYx
