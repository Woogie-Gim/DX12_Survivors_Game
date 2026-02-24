#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>
#include "d3dx12.h"
#include "Utils.h"	// Input Manager 사용

using namespace Microsoft::WRL;
using namespace DirectX;

// Object들의 최상위 부모 클래스
class GameObject
{
// 자식 클래스 (Player, Monster 등)가 접근할 수 있도록 protected 사용
protected:
	XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };

	// 객체 마다 자신만의 상수 버퍼 (메모리)를 가짐
	ComPtr<ID3D12Resource> constantBuffer;
	UINT8* cbvDataBegin = nullptr;

public:
	// 객체 생성 시 GPU에 자신만의 메모리 (상수 버퍼)를 할당
	virtual void Initialize(ID3D12Device* device)
	{
		// 상수 버퍼 (Constant Buffer) 생성
		// DX12 규칙 : 상수 버퍼의 크기는 무조건 256의 배수여야 함
		UINT cbSize = (sizeof(XMMATRIX) + 255) & ~255;
		CD3DX12_HEAP_PROPERTIES cbHeapProps(D3D12_HEAP_TYPE_UPLOAD);	// CPU가 매 프레임 위치를 써야 하므로 Upload Heap
		CD3DX12_RESOURCE_DESC cbDesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);

		device->CreateCommittedResource(
			&cbHeapProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(&constantBuffer));

		// CPU가 데이터를 쓸 수 있도록 Map 해둠 (게임 끝날 때 까지 닫지 않음)
		constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&cbvDataBegin));
	}

	// 매 프레임 자신의 위치를 행렬로 변환해 GPU로 전송
	virtual void Update(float dt)
	{
		// XMATRIX로 이동 행렬 만들기
		XMMATRIX translation = XMMatrixTranslation(position.x, position.y, position.z);

		// HLSL(셰이더)은 수학 계산을 열 (Column) 기준으로 하기 때문에 행렬을 뒤집어서 (Transpose) 넘겨야 함
		XMMATRIX cbvMatrix = XMMatrixTranspose(translation);

		// MAP 해둔 GPU 메모리에 완성된 행렬 데이터를 복사 (이 순간 셰이더로 데이터가 넘어감)
		memcpy(cbvDataBegin, &cbvMatrix, sizeof(XMMATRIX));
	}

	// 스스로를 화면에 그리는 명령
	virtual void Render(ID3D12GraphicsCommandList* commandList)
	{
		// 자신의 상수 버퍼를 파이프라인에 연결
		commandList->SetGraphicsRootConstantBufferView(0, constantBuffer->GetGPUVirtualAddress());
		// 물체 그리기
		commandList->DrawInstanced(6, 1, 0, 0);
	}

	void SetPosition(float x, float y) { position.x = x; position.y = y; }

	// 외부에서 내 위치를 알 수 있게 해주는 함수
	XMFLOAT3 GetPosition() const { return position; }
};

// GameObject를 상속받은 실제 플레이어 클래스
class Player : public GameObject
{
public:
	float speed = 2.0f;

	// 플레이어만의 고유한 업데이트 로직 (키보드 입력)
	void Update(float dt, InputManager& inputMgr)
	{
		if (inputMgr.IsKeyPressed('W')) position.y += speed * dt;
		if (inputMgr.IsKeyPressed('S')) position.y -= speed * dt;
		if (inputMgr.IsKeyPressed('A')) position.x -= speed * dt;
		if (inputMgr.IsKeyPressed('D')) position.x += speed * dt;

		// 부모의 Update를 호출해서 변경된 위치를 GPU로 전송!
		GameObject::Update(dt);
	}
};

// 플레이어를 쫓아가는 적 클래스
class Enemy : public GameObject
{
public:
	// 플레이어(2.0f) 보다 살짝 느리게 설정
	float speed = 1.0f;

	// Enemy 업데이트 : 매 프레임 플레이어의 위치 (targetPos)를 받아서 그쪽으로 이동함
	void Update(float dt, XMFLOAT3 targetPos)
	{
		// 방향 벡터 구하기 (목표 위치 - 내 위치)
		float dirX = targetPos.x - position.x;
		float dirY = targetPos.y - position.y;

		// 피타고라스의 정리로 목표 까지의 실제 거리 구하기 (대각선 길이)
		float distance = sqrt((dirX * dirX) + (dirY * dirY));

		// 정규화 (Normalize) 및 이동
		// 거리가 0보다 클 때만 움직임 (0 나누기 에러 방지)
		if (distance > 0.0f)
		{
			// 방향을 거리로 나누면 크기가 무조건 '1'인 순수한 방향 화살표 (단위 벡터)
			dirX /= distance;
			dirY /= distance;

			// 단위 벡터 * 속도 * 시간 = 정확한 추적 이동
			position.x += dirX * speed * dt;
			position.y += dirY * speed * dt;
		}

		// 이동한 위치를 GPU(상수 버퍼)로 전송
		GameObject::Update(dt);
	}
};