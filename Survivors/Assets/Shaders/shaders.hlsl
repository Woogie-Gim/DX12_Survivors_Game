// C++에서 넘겨줄 상수 버퍼 데이터
// register(b0) : 0번 버퍼 공간 (Slot)을 쓰겠다
cbuffer TransformBuffer : register(b0)
{
   
    matrix WorldMatrix;      // 사각형의 이동 / 회전 / 크기 정보를 담은 4 x 4 행렬
    float4 uvOffsetScale;   // x : 가로 이동, y : 세로 이동, z : 가로 크기, w : 세로 크기
    float4 tintColor;      // C++에서 넘겨준 생상 필터
    float objectType;
    float3 padding;
};

// 텍스처 이미지와 스포이트 (Sampler) 설정
Texture2D myTexture : register(t0);
SamplerState mySampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION; // 화면 좌표를 System Value에 알려줌
    float2 uv : TEXCOORD; // 컬러 대신 텍스처 좌표 사용
};


// 정점 셰이더 (Vertex Shader)
// C++ 버퍼에서 점 하나를 꺼낼 때마다 이 함수 실행
PSInput VSMain(float4 position : POSITION, float2 uv : TEXCOORD)
{
    PSInput result;
    
    // 기존 위치에 WorldMatrix를 곱해서 새로운 위치로 이동 시킴
    result.position = mul(position, WorldMatrix);
    
    // C++에서 넘겨준 값으로 전체 이미지 중 딱 한 프레임 영역만 잘라냄
    result.uv = (uv * uvOffsetScale.zw) + uvOffsetScale.xy;
    
    return result;  // 결과물을 픽셀 셰이더로 넘김
}

// 픽셀 셰이더 (Pixel Shader)
// 정점들 사이의 빈 공간을 칠할 때 화면의 모든 픽셀에 대해 이 함수가 실행
float4 PSMain(PSInput input) : SV_Target
{
    // float 값을 읽어서 정수형으로 반올림(round)해서 검사
    // 원형 모드 (미사일)
    if (round(objectType) == 1)
    {
        float dx = input.uv.x - 0.5f;
        float dy = input.uv.y - 0.5f;
        float dist = sqrt((dx * dx) + (dy * dy));
        
        clip(0.5f - dist);
        return tintColor;
    }
    // 단색 사각형 모드 (HP바)
    else if (round(objectType) == 2)
    {
        return tintColor;
    }
    
    // 텍스처 모드 (배경맵, 캐릭터)
    // 스포이트로 텍스처 색상 추출
    float4 color = myTexture.Sample(mySampler, input.uv);
    
    // png 이미지의 투명한 부분 (알파값이 0.1이하)은 픽셀을 아예 안그리고 버림 (Clip)
    clip(color.a - 0.1f);
    
    // 정점 셰이더가 넘겨준 색상을 그대로 SV_TARGET에 칠함
    // 원래 색상에 틴트 컬러를 곱해서 출력 (흰색을 곱하면 그대로, 빨간색을 곱하면 붉게 변함)
    return color * tintColor;
}