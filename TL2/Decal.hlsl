// Decal.hlsl

cbuffer ModelBuffer : register(b0)
{
    row_major matrix World;
}

cbuffer ViewProjBuffer : register(b1)
{
    row_major matrix View;
    row_major matrix Projection;
}

cbuffer DecalBuffer : register(b2)
{
    row_major matrix InverseDecalWorld;
};

// Texture and Sampler
Texture2D DecalTexture : register(t0);
SamplerState Sampler : register(s0);

// Vertex shader input structure
struct VS_INPUT
{
    float4 Position : POSITION;
    float3 Normal   : NORMAL;
    float4 Color    : COLOR0;
    float2 UV       : TEXCOORD0;
};

// Vertex shader output structure (Pixel shader input)
struct PS_INPUT
{
    float4 Position : SV_Position;
    float2 DecalUV  : TEXCOORD0;
};

// Vertex Shader
PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output = (PS_INPUT) 0;

    // 입력 위치를 월드 공간으로 변환
    float4 worldPos = mul(input.Position, World);

    // 월드 공간의 정점 위치와 노멀
    float3 worldNormal = normalize(input.Normal);

    // ★ 1. 법선 방향으로 살짝 띄우기 (Z-Fighting 해결)
    float Epsilon = 0.001f; // 매우 작은 값, 필요시 조절
    worldPos.xyz += worldNormal * Epsilon;
    
    // 2. 띄워진 위치를 기준으로 화면 좌표 계산
    output.Position = mul(worldPos, mul(View, Projection));

    // 3. 데칼 UV 계산 (띄우기 전 원래 위치 기준)
    // (주석: 입력 위치가 이미 월드 공간이므로 InverseDecalWorld를 바로 곱함)
    float4 decalLocalPos = mul(input.Position, InverseDecalWorld);

    output.DecalUV.x = decalLocalPos.x + 0.5;
    output.DecalUV.y = -decalLocalPos.y + 0.5; // Flip Y for typical texture coordinates

    return output;
}
// Pixel Shader
float4 mainPS(PS_INPUT input) : SV_Target
{
    // Discard pixels outside the decal's projected area.
    if (input.DecalUV.x < 0.0f || input.DecalUV.x > 1.0f || input.DecalUV.y < 0.0f || input.DecalUV.y > 1.0f)
    {
        discard;
    }

    // Sample the decal texture
    float4 decalColor = DecalTexture.Sample(Sampler, input.DecalUV);

    // If the decal texture has no alpha, don't render it.
    if (decalColor.a < 0.01f)
    {
        discard;
    }

    return decalColor;
}
