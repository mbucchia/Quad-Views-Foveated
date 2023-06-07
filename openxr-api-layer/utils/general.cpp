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

#include "general.h"

namespace {

    using namespace openxr_api_layer::utils;
    using namespace DirectX;

    class CpuTimer : public general::ITimer {
        using clock = std::chrono::high_resolution_clock;

      public:
        void start() override {
            m_timeStart = clock::now();
        }

        void stop() override {
            m_duration += clock::now() - m_timeStart;
        }

        uint64_t query() const override {
            const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(m_duration);
            m_duration = clock::duration::zero();
            return duration.count();
        }

      private:
        clock::time_point m_timeStart;
        mutable clock::duration m_duration{0};
    };

    // Taken from
    // https://github.com/microsoft/OpenXR-MixedReality/blob/main/samples/SceneUnderstandingUwp/Scene_Placement.cpp
    bool XM_CALLCONV rayIntersectQuad(DirectX::FXMVECTOR rayPosition,
                                      DirectX::FXMVECTOR rayDirection,
                                      DirectX::FXMVECTOR v0,
                                      DirectX::FXMVECTOR v1,
                                      DirectX::FXMVECTOR v2,
                                      DirectX::FXMVECTOR v3,
                                      XrPosef* hitPose,
                                      float& distance) {
        // Not optimal. Should be possible to determine which triangle to test.
        bool hit = TriangleTests::Intersects(rayPosition, rayDirection, v0, v1, v2, distance);
        if (!hit) {
            hit = TriangleTests::Intersects(rayPosition, rayDirection, v3, v2, v0, distance);
        }
        if (hit && hitPose != nullptr) {
            FXMVECTOR hitPosition = XMVectorAdd(rayPosition, XMVectorScale(rayDirection, distance));
            FXMVECTOR plane = XMPlaneFromPoints(v0, v2, v1);

            // p' = p - (n . p + d) * n
            // Project the ray position onto the plane
            float t = XMVectorGetX(XMVector3Dot(plane, rayPosition)) + XMVectorGetW(plane);
            FXMVECTOR projPoint = XMVectorSubtract(rayPosition, XMVectorMultiply(XMVectorSet(t, t, t, 0), plane));

            // From the projected ray position, look towards the hit position and make the plane's normal "up"
            FXMVECTOR forward = XMVectorSubtract(hitPosition, projPoint);
            XMMATRIX virtualToGazeOrientation = XMMatrixLookToRH(hitPosition, forward, plane);
            xr::math::StoreXrPose(hitPose, XMMatrixInverse(nullptr, virtualToGazeOrientation));
        }
        return hit;
    }

} // namespace

namespace openxr_api_layer::utils::general {

    std::shared_ptr<ITimer> createTimer() {
        return std::make_shared<CpuTimer>();
    }

    bool hitTest(const XrPosef& ray, const XrPosef& quadCenter, const XrExtent2Df& quadSize, XrPosef& hitPose) {
        using namespace DirectX;

        // Taken from
        // https://github.com/microsoft/OpenXR-MixedReality/blob/main/samples/SceneUnderstandingUwp/Scene_Placement.cpp

        // Clockwise order
        const float halfWidth = quadSize.width / 2.0f;
        const float halfHeight = quadSize.height / 2.0f;
        auto v0 = XMVectorSet(-halfWidth, -halfHeight, 0, 1);
        auto v1 = XMVectorSet(-halfWidth, halfHeight, 0, 1);
        auto v2 = XMVectorSet(halfWidth, halfHeight, 0, 1);
        auto v3 = XMVectorSet(halfWidth, -halfHeight, 0, 1);
        const auto matrix = xr::math::LoadXrPose(quadCenter);
        v0 = XMVector4Transform(v0, matrix);
        v1 = XMVector4Transform(v1, matrix);
        v2 = XMVector4Transform(v2, matrix);
        v3 = XMVector4Transform(v3, matrix);

        XMVECTOR rayPosition = xr::math::LoadXrVector3(ray.position);

        const auto forward = XMVectorSet(0, 0, -1, 0);
        const auto rotation = xr::math::LoadXrQuaternion(ray.orientation);
        XMVECTOR rayDirection = XMVector3Rotate(forward, rotation);

        float distance = 0.0f;
        return rayIntersectQuad(rayPosition, rayDirection, v0, v1, v2, v3, &hitPose, distance);
    }

    // https://gamedev.stackexchange.com/questions/136652/uv-world-mapping-in-shader-with-unity/136720#136720
    XrVector2f getUVCoordinates(const XrVector3f& point, const XrPosef& quadCenter, const XrExtent2Df& quadSize) {
        using namespace xr::math;

        const XrVector3f normal =
            Pose::Multiply(Pose::MakePose(quadCenter.orientation, XrVector3f{}), Pose::Translation({0, 0, 1})).position;

        XrVector3f uDirection, vDirection;
        vDirection = {0, 0, 1};
        if (std::abs(normal.y) < 1.0f) {
            vDirection = Normalize(XrVector3f{0, 1, 0} - normal.y * normal);
        }

        uDirection = Normalize(Cross(normal, vDirection));

        return {
            (-Dot(uDirection, point) + (quadSize.width / 2.f)) / quadSize.width,
            (-Dot(vDirection, point) + (quadSize.height / 2.f)) / quadSize.height,
        };
    }

} // namespace openxr_api_layer::utils::general
