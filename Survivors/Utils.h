#pragma once
#include <windows.h>

// 시간 관리를 담당하는 클래스 Tick 역할
class TimeManager
{
private:
	LARGE_INTEGER prevTime, currentTime, frequency;
	float deltaTime = 0.0f;

public:
	void Initialize()
	{
		QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&prevTime);
	}

	void Update()
	{
		QueryPerformanceCounter(&currentTime);
		// 이전 프레임부터 지금 프레임까지 걸린 시간(초)을 계산
		deltaTime = static_cast<float>(currentTime.QuadPart - prevTime.QuadPart) / frequency.QuadPart;
		prevTime = currentTime;
	}

	float GetDeltaTime() const { return deltaTime; }
};

// 키보드 입력을 담당하는 클래스
class InputManager
{
public:
	// 특정 키가 지금 눌려있는지 확인 (W, A, S, D)
	bool IsKeyPressed(int vKey)
	{
		return (GetAsyncKeyState(vKey) & 0x8000) != 0;
	}
};