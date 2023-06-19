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
    bool debugFocusView;
};

SamplerState sourceSampler : register(s0);
Texture2D sourceStereoTexture : register(t0);
Texture2D sourceFocusTexture : register(t1);

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

float4 main(in float4 position : SV_POSITION, in float2 texcoord : PROJ_COORD0, in float3 projectedFocusCoord : PROJ_COORD1) : SV_TARGET {
    // Convert to texcoord and pick the pixel from each layer.
    float4 color0 = sourceStereoTexture.Sample(sourceSampler, texcoord);

    float2 layer1ProjectedCoordNdc = projectedFocusCoord.xy / projectedFocusCoord.z;
    float2 layer1TexCoord = layer1ProjectedCoordNdc * float2(0.5f, -0.5f) + 0.5f;
    // For pixels outside of the focus view, the sampler will give us a fully transparent pixel.
    float4 color1 = sourceFocusTexture.Sample(sourceSampler, layer1TexCoord);

    if (!isUnpremultipliedAlpha) {
        color0 = unpremultiplyAlpha(color0);
        color1 = unpremultiplyAlpha(color1);
    }

    // Do a smooth transition with alpha-blending around the edges.
    float isInside = all(abs(layer1ProjectedCoordNdc) < 1);
    if (smoothingArea) {
        float2 s = smoothstep(float2(0, 0), float2(smoothingArea, smoothingArea), layer1TexCoord) -
                   smoothstep(float2(1, 1) - float2(smoothingArea, smoothingArea), float2(1, 1), layer1TexCoord);
        color1.a = isInside * max(0.5, s.x * s.y);
    } else {
        color1.a = isInside;
    }

    color0 = premultiplyAlpha(color0);
    color1 = premultiplyAlpha(color1);

    // Blend the two pixels.
    float4 color;
    if (!debugFocusView) {
        color = color1 + color0 * (1 - color1.a);
    } else {
        color = color1;
    }

    return float4(color.rgb, 1);
}
