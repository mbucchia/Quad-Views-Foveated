// MIT License
//
// Copyright(c) 2023 Matthieu Bucchianeri
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

// A Pixel Shader that paints the content of a layer given the specified coordinates.

cbuffer ConstantBuffer : register(b0) {
    float smoothingArea;
    bool isUnpremultipliedAlpha;
};

SamplerState sourceSampler : register(s0);
Texture2D sourceTexture : register(t0);

float4 premultiplyAlpha(float4 color) {
    return float4(color.rgb * color.a, color.a);
}

float4 unpremultiplyAlpha(float4 color) {
    if (color.a != 0) {
        return float4(color.rgb / color.a, color.a);
    } else {
        return 0;
    }
}

float4 main(in float4 position : SV_POSITION, in float3 projectedCoord : PROJ_COORD) : SV_TARGET {
    float2 layerProjectedCoordNdc = projectedCoord.xy / projectedCoord.z;

    // Discard the pixels outside the layer.
    if (any(abs(layerProjectedCoordNdc) > 1)) {
        discard;
    }

    // Convert to texcoord and pick the pixel from the layer.
    float2 layerTexCoord = layerProjectedCoordNdc * float2(0.5f, -0.5f) + 0.5f;
    float4 color = sourceTexture.Sample(sourceSampler, layerTexCoord);

    if (!isUnpremultipliedAlpha) {
        color = unpremultiplyAlpha(color);
    }

    // Do a smooth transition with alpha-blending around the edges.
    if (smoothingArea) {
        float2 s = smoothstep(float2(0, 0), float2(smoothingArea, smoothingArea), layerTexCoord) -
                   smoothstep(float2(1, 1) - float2(smoothingArea, smoothingArea), float2(1, 1), layerTexCoord);
        color.a = max(0.5, s.x * s.y);
    } else {
        color.a = 1;
    }

    if (!isUnpremultipliedAlpha) {
        color = premultiplyAlpha(color);
    }

    return color;
}
