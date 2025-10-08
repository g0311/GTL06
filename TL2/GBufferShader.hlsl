// G-Buffer Shader for Deferred Rendering
// Outputs geometry information to multiple render targets

cbuffer ModelBuffer : register(b0)
{
    row_major float4x4 WorldMatrix;
}

cbuffer ViewProjBuffer : register(b1)
{
    row_major float4x4 ViewMatrix;
    row_major float4x4 ProjectionMatrix;
}

cbuffer HighLightBuffer : register(b2)
{
    int Picked;
    float3 Color;
    int X;
    int Y;
    int Z;
    int GIzmo;
}

struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal : NORMAL0;
    float4 color : COLOR;
    float2 texCoord : TEXCOORD0;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION1;
    float3 worldNormal : NORMAL0;
    float4 color : COLOR;
    float2 texCoord : TEXCOORD0;
    float linearDepth : TEXCOORD1;
};

// G-Buffer output structure
struct PS_OUTPUT
{
    float4 Albedo : SV_Target0;    // RT0: Albedo (RGB) + Metallic (A)
    float4 Normal : SV_Target1;    // RT1: World Normal (RGB) + Roughness (A)
    float4 Depth : SV_Target2;     // RT2: Linear Depth (R) + Motion Vector (GB) + AO (A)
};

Texture2D g_DiffuseTexColor : register(t0);
SamplerState g_Sample : register(s0);

struct FMaterial
{
    float3 DiffuseColor;
    float OpticalDensity;
    
    float3 AmbientColor;
    float Transparency;
    
    float3 SpecularColor;
    float SpecularExponent;
    
    float3 EmissiveColor;
    uint IlluminationModel;
    
    float3 TransmissionFilter;
    float dummy;
};

cbuffer PixelConstData : register(b4)
{
    FMaterial Material;
    bool HasMaterial;
    bool HasTexture;
}

cbuffer ColorBuffer : register(b3)
{
    float4 LerpColor;
}

cbuffer PSScrollCB : register(b5)
{
    float2 UVScrollSpeed;
    float UVScrollTime;
    float _pad_scrollcb;
}

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;
    
    // Transform to world space
    float4 worldPos = mul(float4(input.position, 1.0f), WorldMatrix);
    output.worldPos = worldPos.xyz;
    
    // Transform to clip space
    float4x4 MVP = mul(mul(WorldMatrix, ViewMatrix), ProjectionMatrix);
    output.position = mul(float4(input.position, 1.0f), MVP);
    
    // Transform normal to world space
    output.worldNormal = normalize(mul(input.normal, (float3x3)WorldMatrix));
    
    // Calculate linear depth (distance from camera)
    float4 viewPos = mul(worldPos, ViewMatrix);
    output.linearDepth = length(viewPos.xyz);
    
    // Handle gizmo coloring
    float4 c = input.color;
    if (GIzmo == 1)
    {
        if (Y == 1)
        {
            c = float4(1.0, 1.0, 0.0, c.a); // Yellow
        }
        else
        {
            if (X == 1)
                c = float4(1.0, 0.0, 0.0, c.a); // Red
            else if (X == 2)
                c = float4(0.0, 1.0, 0.0, c.a); // Green
            else if (X == 3)
                c = float4(0.0, 0.0, 1.0, c.a); // Blue
        }
    }
    
    output.color = c;
    output.texCoord = input.texCoord;
    
    return output;
}

PS_OUTPUT mainPS(PS_INPUT input)
{
    PS_OUTPUT output;
    
    float4 albedo = input.color;
    
    // 기존 로직: LerpColor와 lerp 처리
    albedo.rgb = lerp(albedo.rgb, LerpColor.rgb, LerpColor.a) * (1.0f - HasMaterial);
    
    if (HasMaterial && HasTexture)
    {
        float2 uv = input.texCoord + UVScrollSpeed * UVScrollTime;
        albedo.rgb = g_DiffuseTexColor.Sample(g_Sample, uv).rgb;
    }
    // HasMaterial만 있는 경우 Material.DiffuseColor 추가 제거 (기존 셰이더에도 주석 처리됨)
    
    output.Albedo = float4(albedo.rgb, 0.0); // Alpha channel can store metallic value
    
    // RT1: World Normal (encoded from [-1,1] to [0,1])
    float3 normal = normalize(input.worldNormal);
    output.Normal = float4(normal * 0.5 + 0.5, 0.0); // Alpha channel can store roughness
    
    // RT2: Linear Depth
    output.Depth = float4(input.linearDepth, 0.0, 0.0, 1.0); // R=depth, GB=motion vector, A=AO
    
    return output;
}