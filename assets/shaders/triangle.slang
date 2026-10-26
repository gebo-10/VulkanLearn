struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color;
    [[vk::location(1)]] float2 uv;
};

struct VertexInput
{
    [[vk::location(0)]] float2 position;
    [[vk::location(1)]] float3 color;
    [[vk::location(2)]] float2 uv;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

struct FragmentInput
{
    [[vk::location(0)]] float3 color;
    [[vk::location(1)]] float2 uv;
};

struct TriangleParameters
{
    float4 color;
};

[[vk::push_constant]] ConstantBuffer<TriangleParameters> triangleParameters;
[[vk::binding(0, 0)]] Texture2D<float4> triangleTexture;
[[vk::binding(1, 0)]] SamplerState triangleSampler;

[shader("fragment")]
float4 fragmentMain(FragmentInput input) : SV_Target0
{
    float4 textureColor = triangleTexture.Sample(triangleSampler, input.uv);
    return float4(textureColor.rgb * input.color *
                      triangleParameters.color.rgb,
                  textureColor.a * triangleParameters.color.a);
}
