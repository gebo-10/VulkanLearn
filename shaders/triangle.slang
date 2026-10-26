struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color;
};

struct VertexInput
{
    [[vk::location(0)]] float2 position;
    [[vk::location(1)]] float3 color;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color;
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
