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

        static XrFovf ComputeBoundingFov(const XrFovf& fullFov, const XrVector2f& min, const XrVector2f& max) {
            const float width = std::max(0.01f, max.x - min.x);
            const float height = std::max(0.01f, max.y - min.y);
            const XrVector2f center = (min + max) / 2.f;

            const auto fullProjection = ComposeProjectionMatrix(fullFov, {0.001f, 100.f});
            // clang-format off
            const auto boundingFov =
                DirectX::XMMATRIX(2.f / width,                0.f,                          0.f, 0.f,
                                  0.f,                        2.f / height,                 0.f, 0.f,
                                  0.f,                        0.f,                          1.f, 0.f,
                                  -(2.f * center.x) / width,  1*-(2.f * center.y) / height, 0.f, 1.f);
            // clang-format on
            DirectX::XMFLOAT4X4 projection;
            DirectX::XMStoreFloat4x4(&projection, DirectX::XMMatrixMultiply(fullProjection, boundingFov));
            return DecomposeProjectionMatrix(projection);
        }

        static bool ProjectPoint(const XrView& eyeInViewSpace,
                                 const XrVector3f& forward,
                                 XrVector2f& projectedPosition) {
            // 1) Compute the view space to camera transform for this eye.
            const auto cameraProjection = ComposeProjectionMatrix(eyeInViewSpace.fov, {0.001f, 100.f});
            const auto cameraView = LoadXrPose(eyeInViewSpace.pose);
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
