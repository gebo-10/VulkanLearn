struct VertexInput
{
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 normal;
    [[vk::location(2)]] float3 color;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 worldNormal;
    [[vk::location(1)]] float3 color;
};

struct CubeParameters
{
    float4x4 viewProjection;
    float4x4 model;
};

[[vk::push_constant]] ConstantBuffer<CubeParameters> cubeParameters;

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    float4 worldPosition = mul(cubeParameters.model, float4(input.position, 1.0));
    output.position = mul(cubeParameters.viewProjection, worldPosition);
    output.worldNormal = normalize(mul((float3x3)cubeParameters.model, input.normal));
    output.color = input.color;
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
