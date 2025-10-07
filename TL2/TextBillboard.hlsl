// 월드 매트릭스 상수 버퍼
cbuffer ModelBuffer : register(b0)
{
    row_major float4x4 WorldMatrix;
}

// 뷰/프로젝션 매트릭스 상수 버퍼
cbuffer ViewProjBuffer : register(b1)
{
    row_major float4x4 ViewMatrix;
    row_major float4x4 ProjectionMatrix;
}

struct VS_INPUT
{
    float3 centerPos : WORLDPOSITION;
    float2 size : SIZE;
    float4 uvRect : UVRECT;
    uint vertexId : SV_VertexID; // GPU가 자동으로 부여하는 고유 정점 ID
};

struct PS_INPUT
{
    float4 pos_screenspace : SV_POSITION;
    float2 tex : TEXCOORD0;
};

Texture2D fontAtlas : register(t0);
SamplerState linearSampler : register(s0);


PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;

    // 로컬 공간에서의 위치 (텍스트 배치)
    float3 localPos = float3(input.centerPos.x, input.centerPos.y, 0.0f);
    
    // 로컬 -> 월드 변환
    float4 worldPos = mul(float4(localPos, 1.0f), WorldMatrix);
    
    // 월드 -> 뷰 -> 프로젝션
    float4x4 VP = mul(ViewMatrix, ProjectionMatrix);
    output.pos_screenspace = mul(worldPos, VP);

    // UV는 C++에서 각 정점별로 계산해 전달띠
    output.tex = input.uvRect.xy;

    return output;
}

float4 mainPS(PS_INPUT input) : SV_Target
{
    float4 color = fontAtlas.Sample(linearSampler, input.tex);

    clip(color.a - 0.5f); // alpha - 0.5f < 0 이면 해당픽셀 렌더링 중단

    return color;
}
