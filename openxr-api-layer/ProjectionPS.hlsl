// A Pixel Shader that paints the content of a layer given the specified coordinates.

SamplerState sourceSampler : register(s0);
Texture2D sourceTexture : register(t0);

float4 main(in float4 position : SV_POSITION, in float3 projectedCoord : PROJ_COORD) : SV_TARGET {
    float2 layerProjectedCoordNdc = projectedCoord.xy / projectedCoord.z;

    // Discard the pixels outside the layer.
    if (any(abs(layerProjectedCoordNdc) > 1)) {
        discard;
    }

    // Convert to texcoord and pick the pixel from the layer.
    float2 layerTexCoord = layerProjectedCoordNdc * float2(0.5f, -0.5f) + 0.5f;
    return sourceTexture.Sample(sourceSampler, layerTexCoord);
}
