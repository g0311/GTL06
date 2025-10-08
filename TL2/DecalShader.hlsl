// Decal Shader for Deferred Rendering
// Reads G-Buffer and applies decal projection onto surfaces

cbuffer ModelBuffer : register(b0)
{
    row_major float4x4 WorldMatrix;
}

cbuffer ViewProjBuffer : register(b1)
{
    row_major float4x4 ViewMatrix;
    row_major float4x4 ProjectionMatrix;
}

cbuffer DecalDataBuffer : register(b6)
{
    row_major float4x4 DecalViewMatrix;        // Decal's view matrix (for projection)
    row_major float4x4 DecalProjectionMatrix; // Decal's projection matrix
    row_major float4x4 InvViewProjMatrix;     // Inverse view-projection matrix
    float4 DecalColor;                         // Decal tint color
    float DecalIntensity;                      // Decal blend intensity
    float DecalFade;                           // Distance-based fade
    float2 DecalPadding;
}

// G-Buffer inputs (from previous pass)
Texture2D GBufferAlbedo : register(t0);    // Albedo texture from G-Buffer
Texture2D GBufferNormal : register(t1);    // Normal texture from G-Buffer  
Texture2D GBufferDepth : register(t2);     // Depth texture from G-Buffer

// Decal textures
Texture2D DecalDiffuse : register(t3);     // Decal diffuse texture
Texture2D DecalNormal : register(t4);      // Decal normal map (optional)

SamplerState LinearSampler : register(s0);
SamplerState DecalSampler : register(s1);

struct VS_INPUT
{
    float3 position : POSITION;
    float2 texCoord : TEXCOORD0;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 screenUV : TEXCOORD0;
    float4 screenPos : TEXCOORD1;
};

// Fullscreen quad vertex shader for decal pass
PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;
    
    // Transform to clip space (fullscreen quad)
    output.position = float4(input.position, 1.0);
    
    // Screen UV coordinates (convert from [-1,1] to [0,1])
    output.screenUV = input.texCoord;
    
    // Store screen position for depth reconstruction
    output.screenPos = output.position;
    
    return output;
}

// Reconstruct world position from screen coordinates and depth
float3 ReconstructWorldPosition(float2 screenUV, float depth)
{
    // Convert screen UV to NDC coordinates
    float2 ndc = screenUV * 2.0 - 1.0;
    ndc.y = -ndc.y; // Flip Y coordinate for D3D
    
    // Create clip space position (use depth as Z)
    float4 clipPos = float4(ndc.x, ndc.y, depth, 1.0);
    
    // Transform to world space using pre-calculated inverse view-projection matrix
    float4 worldPos = mul(clipPos, InvViewProjMatrix);
    
    // Perspective divide
    return worldPos.xyz / worldPos.w;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    // Sample G-Buffer
    float4 albedo = GBufferAlbedo.Sample(LinearSampler, input.screenUV);
    float4 normalData = GBufferNormal.Sample(LinearSampler, input.screenUV);
    float4 depthData = GBufferDepth.Sample(LinearSampler, input.screenUV);
    
    // Early exit if no geometry (far clip plane)
    if (depthData.r > 999.0)
        discard;
    
    // Decode world normal from G-Buffer (from [0,1] back to [-1,1])
    float3 worldNormal = normalize(normalData.xyz * 2.0 - 1.0);
    
    // Reconstruct world position from depth
    float3 worldPos = ReconstructWorldPosition(input.screenUV, depthData.r);
    
    // Transform world position to decal space
    float4 decalSpacePos = mul(float4(worldPos, 1.0), DecalViewMatrix);
    decalSpacePos = mul(decalSpacePos, DecalProjectionMatrix);
    decalSpacePos.xyz /= decalSpacePos.w;
    
    // Check if fragment is within decal bounds
    if (abs(decalSpacePos.x) > 1.0 || abs(decalSpacePos.y) > 1.0 || abs(decalSpacePos.z) > 1.0)
        discard;
    
    // Convert decal space coordinates to UV coordinates
    float2 decalUV = decalSpacePos.xy * 0.5 + 0.5;
    decalUV.y = 1.0 - decalUV.y; // Flip Y for texture sampling
    
    // Sample decal texture
    float4 decalTexture = DecalDiffuse.Sample(DecalSampler, decalUV);
    
    // Calculate decal normal if normal map is provided
    float3 decalNormal = worldNormal; // Default to surface normal
    if (DecalNormal)
    {
        float3 decalNormalSample = DecalNormal.Sample(DecalSampler, decalUV).xyz * 2.0 - 1.0;
        // Transform decal normal to world space (simplified)
        decalNormal = normalize(decalNormalSample);
    }
    
    // Calculate fade based on distance from decal center
    float distanceFade = 1.0 - saturate(length(decalSpacePos.xy));
    distanceFade = pow(distanceFade, DecalFade);
    
    // Calculate angle fade (decals fade on steep angles)
    float3 decalForward = float3(0, 0, 1); // Decal facing direction in decal space
    float angleFade = saturate(dot(worldNormal, decalForward));
    angleFade = pow(angleFade, 2.0); // Sharper falloff
    
    // Combine all fade factors
    float finalAlpha = decalTexture.a * DecalIntensity * distanceFade * angleFade;
    
    // Blend decal with existing albedo
    float3 finalColor = lerp(albedo.rgb, decalTexture.rgb * DecalColor.rgb, finalAlpha);
    
    return float4(finalColor, 1.0);
}