#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>
#include "../Utils/d3dx12.h"
#include "../Utils/Utils.h"			// Input Manager 사용
#include "../Utils/stb_image.h"		// 이미지 로드용

using namespace Microsoft::WRL;
using namespace DirectX;

// C++과 HLSL의 상수 버퍼 규격을 맞추기 위한 구조체
struct CBData
{
	XMMATRIX worldMatrix;
	XMFLOAT4 uvOffsetScale;	// x: Offset X, y: Offset Y, z: Scale X, w: Scale Y	
	XMFLOAT4 tintColor;		// R, G, B, A 색상 필터
	float objectType;		// objectType으로 Bullet, 체력바 등 구분
	float padding[3];
};

// Object들의 최상위 부모 클래스
class GameObject
{
	// 자식 클래스 (Player, Monster 등)가 접근할 수 있도록 protected 사용
protected:
	XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
	XMFLOAT3 scale = { 0.1f, 0.1f, 0.1f };

	// 객체 마다 자신만의 상수 버퍼 (메모리)를 가짐
	ComPtr<ID3D12Resource> constantBuffer;
	UINT8* cbvDataBegin = nullptr;

	// 텍스처 관련 변수
	ComPtr<ID3D12Resource> texture;
	ComPtr<ID3D12Resource> textureUploadHeap;
	ComPtr<ID3D12DescriptorHeap> srvHeap;		// 나만의 텍스처 목차

	// 애니메이션 관련 변수 추가
	int currentFrame = 0;
	int maxFrames = 1;
	float frameTime = 0.0f;
	float frameDuration = 0.033f;	// 0.033초마다 다음 동작으로 변경

	// 현재 뒤집혔는지 기억하는 boolean
	bool isFlipped = false;

	// 기본은 하얀색 (원본 색상 유지)
	XMFLOAT4 tintColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	int objectType = 0; // 기본 값은 텍스처 모드 (0)

	// 카메라와 UV 스크롤 변수
	XMFLOAT2 cameraPos = { 0.0f, 0.0f };	// 카메라 위치
	XMFLOAT2 uvScroll = { 0.0f, 0.0f }; // 런닝머신처럼 텍스처를 돌릴 수치
	XMFLOAT2 uvScale = { 1.0f, 1.0f };	// 텍스처 타일링(반복) 배율

public:
	// 밖에서 타입을 정할 수 있는 함수 추가
	void SetObjectType(int type) { objectType = type; }

	// 외부에서 카메라와 UV를 세팅하는 함수
	void SetCameraPos(float cx, float cy) { cameraPos = { cx, cy }; }
	void SetUVScroll(float u, float v) { uvScroll = { u, v }; }
	void SetUVScale(float u, float v) { uvScale = { u, v }; }

	// 애니메이션 강제 조작 스위치
	void SetFrame(int frame) { currentFrame = frame; }
	void SetFrameDuration(float duration) { frameDuration = duration; }
	
	// 객체 생성 시 GPU에 자신만의 메모리 (상수 버퍼)를 할당
	virtual void Initialize(ID3D12Device* device)
	{
		// 상수 버퍼 (Constant Buffer) 생성
		// DX12 규칙 : 상수 버퍼의 크기는 무조건 256의 배수여야 함 / CBData 크기 만큼 256 정렬해서 할당
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

	// 이미지 파일을 읽어서 GPU로 넘기는 DX12 마법의 코드
	void LoadTexture(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const char* filename, int frames)
	{
		maxFrames = frames;

		// stb_image로 PC에서 이미지 파일 읽기
		int texWidth, texHeight, texChannels;
		unsigned char* image = stbi_load(filename, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

		if (image == nullptr)
		{
			MessageBoxA(nullptr, filename, "Texture Load Failed! Check File Path/Name", MB_OK);
			return;
		}

		// 이미지가 DX12 한계치(16384)를 넘는지 검사
		if (texWidth > 16384 || texHeight > 16384)
		{
			MessageBoxA(nullptr, "2. 이미지가 너무 큽니다! (가로세로 16384 픽셀 제한 초과)", "DX12 하드웨어 한계 초과", MB_OK);
			stbi_image_free(image);
			return;
		}

		// GPU 메모리에 Texture 만들기
		CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, texWidth, texHeight);
		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

		// GPU가 도화지를 진짜 잘 만들었는지 결과(HRESULT)를 검사!
		HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture));
		if (FAILED(hr) || texture == nullptr)
		{
			MessageBoxA(nullptr, "3. GPU 메모리에 텍스처 생성 실패!", "GPU 에러", MB_OK);
			stbi_image_free(image);
			return;
		}

		// 복사용 Upload Heap 만들기
		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);
		CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
		device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureUploadHeap));

		// Upload Heap에 데이터 싣고 GPU 도화지로 복사 명령 내리기
		D3D12_SUBRESOURCE_DATA texData = {};
		texData.pData = image;
		texData.RowPitch = texWidth * 4;
		texData.SlicePitch = texData.RowPitch * texHeight;
		UpdateSubresources(cmdList, texture.Get(), textureUploadHeap.Get(), 0, 0, 1, &texData);

		// 복사가 끝난 도화지를 읽기 전용 (SRV) 모드로 변환
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmdList->ResourceBarrier(1, &barrier);

		stbi_image_free(image);	// 메모리 청소

		// 텍스처를 쓸 수 있는 SRV 힙 생성
		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.NumDescriptors = 1;
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap));

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		device->CreateShaderResourceView(texture.Get(), &srvDesc, srvHeap->GetCPUDescriptorHandleForHeapStart());
	}

	// 밖에서 Flip 조작할 수 있는 함수
	void SetFlipped(bool flipped) { isFlipped = flipped; }

	// 밖에서 색상을 강제로 바꿀 수 있는 함수
	void SetTintColor(float r, float g, float b, float a = 1.0f)
	{
		tintColor = { r, g, b, a };
	}

	// 매 프레임 자신의 위치를 행렬로 변환해 GPU로 전송
	virtual void Update(float dt)
	{
		// 타이머를 돌려서 프레임 넘기기 (애니메이션 걷기 구현)
		frameTime += dt;
		if (frameTime >= frameDuration)
		{
			currentFrame = (currentFrame + 1) % maxFrames;
			frameTime = 0.0f;
		}

		// isFlipped가 true면 가로 크기를 음수(-)로 만듦
		float realScaleX = isFlipped ? -scale.x : scale.x;

		// 두 행렬 (크기 행렬과 이동 행렬) 을 곱해서 최종 월드 행렬 완성 (무조건 크기 > 회전 > 이동 순서로 곱해야 함)
		// 진짜 내 위치에서 카메라 위치를 뺀 곳에 렌더링
		XMMATRIX worldMatrix = XMMatrixScaling(realScaleX, scale.y, scale.z) * XMMatrixTranslation(position.x - cameraPos.x, position.y - cameraPos.y, position.z);

		// HLSL(셰이더)은 수학 계산을 열 (Column) 기준으로 하기 때문에 행렬을 뒤집어서 (Transpose) 넘겨야 함
		CBData cbData;
		cbData.worldMatrix = XMMatrixTranspose(worldMatrix);

		// 전체 이미지에서 지금 프레임 영역만 자르는 UV 계산
		float frameWidth = 1.0f / maxFrames;							// 한 프레임의 가로 비율
		cbData.uvOffsetScale.z = frameWidth * uvScale.x;
		cbData.uvOffsetScale.w = 1.0f * uvScale.y;

		// 기본 애니메이션 이동 + 강제 텍스처 스크롤 (uvScroll) 합치기
		cbData.uvOffsetScale.x = (currentFrame * frameWidth) + uvScroll.x;
		cbData.uvOffsetScale.y = uvScroll.y;

		// 내 색상 정보를 GPU로 같이 넘김
		cbData.tintColor = tintColor;
		// objectType에 타입 정보 담기
		cbData.objectType = (float)objectType;

		// MAP 해둔 GPU 메모리에 완성된 행렬 데이터를 복사 (이 순간 셰이더로 데이터가 넘어감)
		memcpy(cbvDataBegin, &cbData, sizeof(CBData));
	}

	// 스스로를 화면에 그리는 명령
	virtual void Render(ID3D12GraphicsCommandList* commandList)
	{
		// 자신의 상수 버퍼 (위치 / UV 정보)를 파이프라인에 연결
		commandList->SetGraphicsRootConstantBufferView(0, constantBuffer->GetGPUVirtualAddress());

		// 텍스처 정보 (SRV 목차) 연결
		// srvHeap이 세팅된 (텍스처 로드한) 객체만 텍스처 목차를 연결
		// 이미지를 성공적으로 불러와서 srvHeap이 존재할 때만 텍스처를 연결
		if (srvHeap != nullptr)
		{
			ID3D12DescriptorHeap* descriptorHeaps[] = { srvHeap.Get() };
			commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
			commandList->SetGraphicsRootDescriptorTable(1, srvHeap->GetGPUDescriptorHandleForHeapStart());
		}

		// 물체 그리기
		commandList->DrawInstanced(6, 1, 0, 0);
	}

	void SetPosition(float x, float y) { position.x = x; position.y = y; }

	// 외부에서 내 위치를 알 수 있게 해주는 함수
	XMFLOAT3 GetPosition() const { return position; }

	// 크기를 바꿀 수 있는 Setter 함수 추가
	void SetScale(float x, float y) { scale.x = x; scale.y = y; }
};

// GameObject를 상속받은 실제 플레이어 클래스
class Player : public GameObject
{
public:
	float basespeed = 0.5f;		// 원래 속도
	float currentSpeed = 0.5f;	// 실제 적용될 현재 속도

	// 체력 변수
	float maxHp = 100.0f;
	float hp = 100.0f;

	// 레벨업 시스템 변수
	int level = 1;
	float exp = 0.0f;
	float maxExp = 100.0f;	// 이 수치가 다 차면 레벨업

	// 플레이어만의 고유한 업데이트 로직 (키보드 입력)
	void Update(float dt, InputManager& inputMgr)
	{
		if (inputMgr.IsKeyPressed('W') || inputMgr.IsKeyPressed(VK_UP))
		{
			position.y += currentSpeed * dt;
		}

		if (inputMgr.IsKeyPressed('S') || inputMgr.IsKeyPressed(VK_DOWN))
		{
			position.y -= currentSpeed * dt;
		}
		if (inputMgr.IsKeyPressed('A') || inputMgr.IsKeyPressed(VK_LEFT)) 
		{
			position.x -= currentSpeed * dt;
			isFlipped = true;	// 왼쪽 볼 땐 뒤집기
		}
		if (inputMgr.IsKeyPressed('D') || inputMgr.IsKeyPressed(VK_RIGHT)) 
		{
			position.x += currentSpeed * dt;
			isFlipped = false;	// 오른쪽 볼 땐 원상 복구
		}

		// 부모의 Update를 호출해서 변경된 위치를 GPU로 전송!
		GameObject::Update(dt);
	}
};

// 플레이어를 쫓아가는 적 클래스
class Enemy : public GameObject
{
public:
	// 플레이어(2.0f) 보다 살짝 느리게 설정
	float speed = 0.25f;

	// 체력 및 생존 변수 추가
	float maxHp = 30.0f;
	float hp = 30.0f;
	bool isDead = false;	// HP가 0이 되면 true로 바뀜

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

		// 몬스터는 플레이어 위치와 내 위치를 비교해서 뒤집기
		if (targetPos.x < position.x)
		{
			isFlipped = true;  // 플레이어가 내 왼쪽에 있으면 왼쪽 보기
		}
		else 
		{
			isFlipped = false; // 플레이어가 내 오른쪽에 있으면 오른쪽 보기
		}

		// 이동한 위치를 GPU(상수 버퍼)로 전송
		GameObject::Update(dt);
	}
};

// Bullet 클래스
class Bullet : public GameObject
{
public:
	float speed = 1.5f;		// 미사일 속도
	float damage = 15.0f;	// 미사일 데미지
	bool isDead = true;		// 처음엔 비활성화 (발사 대기) 상태

	// 미사일 전용 Update 메인 루프에서 다 계산해주므로 이제 가볍게 행렬만 갱신
	void Update(float dt)
	{
		// 죽은 미사일은 계산하지 않음
		if (isDead) return;

		// 부모(GameObject)의 행렬 갱신만 호출
		GameObject::Update(dt);
	}
};

// 경험치 젬 (Gem) 클래스 
class Gem : public GameObject
{
public:
	bool isDead = true;
	float expValue = 20.0f; // 보석 하나 당 경험치
	
	void Update(float dt, Player& player)
	{
		if (isDead) return;

		// 플레이어와의 거리 계산
		float dx = player.GetPosition().x - position.x;
		float dy = player.GetPosition().y - position.y;
		float dist = sqrt((dx * dx) + (dy * dy));

		// 획득 범위 (플레이어 몸에 닿으면 경험치 획득)
		float pickupRadius = 0.15f;
		if (dist < pickupRadius)
		{
			player.exp += expValue;

			// 경험치 다 차면 레벨업
			if (player.exp >= player.maxExp)
			{
				player.exp -= player.maxExp;
				player.maxExp *= 1.5f; // 다음 레벨업 요구량 1.5배 증가
				player.level++;
			}
			isDead = true; // 획득한 보석은 파괴
		}

		GameObject::Update(dt);
	}
};

// 피격 데미지 텍스트 (DamageText) 클래스 추가
class DamageText : public GameObject
{
public:
	bool isDead = true;
	float lifeTime = 0.0f;
	float maxLife = 0.5f;	// 0.5초 동안만 화면에 떠 있음

	void Update(float dt)
	{
		if (isDead) return;

		lifeTime += dt;

		// 위로 살살 떠오르는 연출
		position.y += 0.5f * dt;

		// 수명이 다하면 삭제
		if (lifeTime >= maxLife)
		{
			isDead = true;
		}

		GameObject::Update(dt);
	}
};