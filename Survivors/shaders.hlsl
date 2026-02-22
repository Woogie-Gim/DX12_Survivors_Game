struct PSInput
{
    float4 position : SV_POSITION; // 화면 좌표를 System Value에 알려줌
    float4 color : COLOR;        // 색상 데이터
};

// 정점 셰이더 (Vertex Shader)
// C++ 버퍼에서 점 하나를 꺼낼 때마다 이 함수 실행
PSInput VSMain(float4 position : POSITION, float4 color : COLOR)
{
    PSInput result;
    result.position = position;
    result.color = color;
    
    return result;  // 결과물을 픽셀 셰이더로 넘김
}

// 픽셀 셰이더 (Pixel Shader)
// 정점들 사이의 빈 공간을 칠할 때 화면의 모든 픽셀에 대해 이 함수가 실행
float4 PSMain(PSInput input) : SV_Target
{
    // 정점 셰이더가 넘겨준 색상을 그대로 SV_TARGET에 칠함
    return input.color;
}