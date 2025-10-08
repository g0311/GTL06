// Simple Copy Shader for G-Buffer to Back Buffer
// Uses PositionColor vertex layout (Position only, UV generated from position)

struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 texCoord : TEXCOORD;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

// G-Buffer input
Texture2D GBufferAlbedo : register(t0);
SamplerState LinearSampler : register(s0);

// Fullscreen quad vertex shader
PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;
    
    // 입력 position이 이미 NDC 좌표(-1~1)이므로 그대로 사용
    output.position = float4(input.position, 1.0);
    
    // Position에서 UV 생성: NDC(-1~1) -> UV(0~1)
    output.texCoord = input.position.xy * 0.5 + 0.5;
    output.texCoord.y = 1.0 - output.texCoord.y; // Y축 뒤집기 (Direct3D UV 좌표계)
    
    return output;
}

// Simple copy pixel shader
float4 mainPS(PS_INPUT input) : SV_TARGET
{
    float4 albedo = GBufferAlbedo.Sample(LinearSampler, input.texCoord);
    return float4(albedo.rgb, 1.0);
}
