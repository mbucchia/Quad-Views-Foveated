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

namespace xr {

    namespace QuadView {
        constexpr uint32_t Left = 0;
        constexpr uint32_t Right = 1;
        constexpr uint32_t FocusLeft = 2;
        constexpr uint32_t FocusRight = 3;
        constexpr uint32_t Count = 4;
    } // namespace QuadView

    namespace math {

        namespace Fov {

            static inline std::pair<float, float> Scale(std::pair<float, float> angles, float scale) {
                assert(angles.second > angles.first);
                const float angleCenter = (angles.first + angles.second) / 2;
                const float angleSpread = angles.second - angles.first;
                const float angleSpreadScaled = angleSpread * scale;
                const float angleLowerScaled = angleCenter - (angleSpreadScaled / 2);
                const float angleUpperScaled = angleCenter + (angleSpreadScaled / 2);

                return std::make_pair(angleLowerScaled, angleUpperScaled);
            };

            static inline std::pair<float, float> Lerp(std::pair<float, float> range,
                                                       std::pair<float, float> angles,
                                                       float lerp) {
                assert(angles.second > angles.first);
                assert(range.second > range.first);
                const float rangeSpread = range.second - range.first;
                const float angleSpread = angles.second - angles.first;
                const float lerpedCenter = range.first + lerp * rangeSpread;
                float angleLower = lerpedCenter - angleSpread / 2.f;
                float angleUpper = lerpedCenter + angleSpread / 2.f;

                // Clamp to the FOV boundaries.
                if (angleUpper > range.second) {
                    angleUpper = range.second;
                    angleLower = angleUpper - angleSpread;
                } else if (angleLower < range.first) {
                    angleLower = range.first;
                    angleUpper = angleLower + angleSpread;
                }

                return std::make_pair(angleLower, angleUpper);
            };

        } // namespace Fov

        static bool ProjectPoint(const XrView& eyeInViewSpace,
                                 const XrVector3f& forward,
                                 XrVector2f& projectedPosition) {
            // 1) Compute the view space to camera transform for this eye.
            const auto cameraProjection = xr::math::ComposeProjectionMatrix(eyeInViewSpace.fov, {0.001f, 100.f});
            const auto cameraView = xr::math::LoadXrPose(eyeInViewSpace.pose);
            const auto viewToCamera = DirectX::XMMatrixMultiply(cameraProjection, cameraView);

            // 2) Transform the 3D point to camera space.
            const auto projectedInCameraSpace =
                DirectX::XMVector3Transform(DirectX::XMVectorSet(forward.x, forward.y, forward.z, 1.f), viewToCamera);

            // 3) Project the 3D point in camera space to a 2D point in normalized device coordinates.
            // 4) Output NDC (-1,+1)
            XrVector4f point;
            xr::math::StoreXrVector4(&point, projectedInCameraSpace);
            if (std::abs(point.w) < FLT_EPSILON) {
                return false;
            }
            projectedPosition.x = point.x / point.w;
            projectedPosition.y = point.y / point.w;

            return true;
        }

    } // namespace math

} // namespace xr
