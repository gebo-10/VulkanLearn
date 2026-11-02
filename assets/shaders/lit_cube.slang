struct VertexInput
{
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 normal;
    [[vk::location(2)]] float3 color;
    uint instanceID : SV_InstanceID;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 worldNormal;
    [[vk::location(1)]] float3 color;
};

struct FrameData
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewProjection;
};

struct ModelParameters
{
    float4x4 model;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frameData;
[[vk::binding(1, 0)]] StructuredBuffer<float4> instanceColors;
[[vk::push_constant]] ConstantBuffer<ModelParameters> modelParameters;

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    float4 worldPosition =
        mul(modelParameters.model, float4(input.position, 1.0));
    float2 instanceOffset = float2(
        (input.instanceID % 2) * 1.3 - 0.65,
        (input.instanceID / 2) * 1.3 - 0.65);
    worldPosition.xy += instanceOffset;
    output.position = mul(frameData.viewProjection, worldPosition);
    output.worldNormal =
        normalize(mul((float3x3)modelParameters.model, input.normal));
    output.color = input.color * instanceColors[input.instanceID].rgb;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    float3 lightDirection = normalize(float3(-0.4, 0.8, 0.6));
    float diffuse = max(dot(normalize(input.worldNormal), lightDirection), 0.0);
    float lighting = 0.22 + diffuse * 0.78;
    return float4(input.color * lighting, 1.0);
}
