// A Vertex Shader that draws a full-screen quad and projects the coordinates of a layer.

cbuffer ConstantBuffer : register(b0) {
    float4x4 Projection;
};

void main(in uint id : SV_VertexID, out float4 position : SV_POSITION, out float3 projectedCoord : PROJ_COORD) {
    float2 texcoord = float2((id == 1) ? 2.0 : 0.0, (id == 2) ? 2.0 : 0.0);
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    projectedCoord = mul(position, Projection).xyw;
}
