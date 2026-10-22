struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    const float2 positions[3] = {
        float2(0.0, -0.65),
        float2(0.65, 0.55),
        float2(-0.65, 0.55)
    };
    const float3 colors[3] = {
        float3(1.0, 0.2, 0.2),
        float3(0.2, 1.0, 0.3),
        float3(0.2, 0.4, 1.0)
    };

    VertexOutput output;
    output.position = float4(positions[vertexIndex], 0.0, 1.0);
    output.color = colors[vertexIndex];
    return output;
}

struct FragmentInput
{
    [[vk::location(0)]] float3 color;
};

[shader("fragment")]
float4 fragmentMain(FragmentInput input) : SV_Target0
{
    return float4(input.color, 1.0);
}
