#include <windows.h>
#include <d3d12.h>                       // DX12 코어 기능
#include <dxgi1_4.h>                    // 디스플레이 장치 및 스왑 체인 관리
#include <d3dcompiler.h>               // 셰이더 컴파일용 헤더
#include <wrl.h>                      // Comptr (스마트 포인터) 사용을 위함
#include "../Utils/d3dx12.h"         // 헬퍼 헤더
#include <DirectXMath.h>
#include "../Utils/Utils.h"
#include "../Objects/GameObject.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../Utils/stb_image.h"

using namespace Microsoft::WRL;
using namespace DirectX;

// Vertex 구조체
// 점 하나가 가지는 정보 : 위치 (x, y, z)와 색상 (r, g, b, a)
struct Vertex
{
    float position[3];
    float uv[2];
};

class D3D12Manager
{
public:
    // ComPtr은 DX12 객체들의 메모리 누수를 막아주는 자동 관리 포인터
    ComPtr<IDXGIFactory4>       dxgiFactory;
    ComPtr<ID3D12Device>        d3dDevice;
    ComPtr<ID3D12CommandQueue>  commandQueue;

    // 더블 버퍼링
    static const int frameCount = 2;

    ComPtr<ID3D12CommandAllocator>      commandAllocator;
    ComPtr<ID3D12GraphicsCommandList>   commandList;
    ComPtr<IDXGISwapChain3>             swapChain;
    ComPtr<ID3D12DescriptorHeap>        rtvHeap;
    ComPtr<ID3D12Resource>              renderTargets[frameCount];

    // 서술자 하나의 크기
    UINT rtvDescriptorSize = 0;
    // 현재 몇 번째 버퍼를 쓰고 있는지
    UINT frameIndex = 0;

    // Fence 관련 변수
    ComPtr<ID3D12Fence> fence;
    UINT64              fenceValue = 0;
    HANDLE              fenceEvent = nullptr;

    // Vertex Buffer 관련 변수
    ComPtr<ID3D12Resource>      vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW    vertexBufferView;

    // 파이프라인 관련 변수
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr <ID3D12PipelineState> pipelineState;

    // 상수 버퍼와 위치 / 입력 변수
    ComPtr<ID3D12Resource> constantBuffer;
    UINT8* cbvDataBegin = nullptr;         // CPU가 쓸 데이터 주소

    float playerX = 0.0f; // 플레이어의 X 위치
    float playerY = 0.0f; // 플레이어의 Y 위치
    float speed = 2.0f;   // 이동 속도

    TimeManager timeMgr;
    InputManager inputMgr;

    // 플레이어 객체
    Player player;

    // 10마리 Enemy
    static const int ENEMY_COUNT = 10;
    Enemy enemies[ENEMY_COUNT];

    // 배경 맵 객체 (순수 GameObject 사용)
    GameObject background;

    // DX12 초기화를 진행하는 함수
    void Initialize(HWND hWnd, int width, int height)
    {
        // 디버그 레이어 활성화
#if defined(_DEBUG)
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
        }
#endif

        // DXGI 팩토리 생성
        // 모니터 해상도, 주사율, 그래픽 카드 어댑터 등의 하드웨어 정보를 수집
        CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));

        // D3D12 디바이스 생성
        // 실제 GPU를 조종하는 총사령관 객체, 모든 DX12 자원(버퍼, 텍스처 등)을 만듦
        // 첫 번째 인자를 nullptr로 두면 기본 그래픽 카드를 자동으로 잡아줌
        D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3dDevice));

        // 커맨드 큐(Command Queue) 생성
        // GPU는 CPU와 별개로 일하기 때문에 CPU가 그려 하고 명령서(Command List)를 작성해서
        // 이 큐(대기열)에 밀어 넣으면 GPU가 순서대로 꺼내서 그림을 그리는 구조
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; // 화면에 직접 그리는 일반적인 명령 타입
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

        d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));

        // 커맨드 할당자 (Command Allocator) 생성
        // 명령서를 작성하기 위한 실제 메모리 공간을 할당
        d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));

        // 커맨드 리스트 (Command List) 생성
        // 할당받은 메모리 공간에 명령을 적는 펜 역할
        d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));

        // 스왑 체인 (Swap Chain) 생성
        // 화면 깜빡임을 막기 위해 버퍼를 여러 장 교체하는 시스템
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.BufferCount = frameCount;             // 더블 버퍼링
        swapChainDesc.Width = width;                        // 창 가로 크기
        swapChainDesc.Height = height;                      // 창 세로 크기
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 일반적인 32비트 색상 포맷
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 이 버퍼를 화면 출력용으로 쓰겠다
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;   // 보여준 화면은 버리고 새 화면으로 덮어씀
        swapChainDesc.SampleDesc.Count = 1;                         // 안티앨리어싱 미사용 (2D 게임)

        // 스왑 체인은 기존 버전(1)으로 생성한 뒤 최선 버전(3)으로 캐스팅(As)
        ComPtr<IDXGISwapChain1> tempSwapChain;
        dxgiFactory->CreateSwapChainForHwnd(
            commandQueue.Get(),     // 스왑 체인은 아까 만든 큐랑 타이밍을 맞춰야 함
            hWnd,                   // 띄운 윈도우 창
            &swapChainDesc,
            nullptr, nullptr,
            &tempSwapChain
        );
        tempSwapChain.As(&swapChain); // IDXGISwapChain3로 변환

        // 현재 화면에 보여줄 버퍼의 인덱스(0번 아님 1번)를 가져옴
        frameIndex = swapChain->GetCurrentBackBufferIndex();

        // 서술자 힙(Descriptor Heap - RTV 용) 생성
        // 버퍼들이 메모리 어디에 있는지 알려주는 배열 만들기
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = frameCount; // 버퍼가 2개 이기 때문에 배열도 2개 필요
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; // Render Target View 타입의 목차
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));

        // 하드웨어 마다 배열 한 칸의 크기가 다르기 때문에 크기를 미루 구해야 함
        rtvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // 랜더 타겟 뷰(Redner Target View RTV) 생성
        // 스왑 체인에 있는 실제 Resource를 가져와서 배열에 연결해주는 작업
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart(); // 배열의 첫 번째 위치
        for (UINT n = 0; n < frameCount; n++)
        {
            // 스왑 체인에서 n 번째 버퍼를 가져와 renderTarget[n]에 저장
            swapChain->GetBuffer(n, IID_PPV_ARGS(&renderTargets[n]));
            // 뷰 (RTV) 생성 (GPU가 이 버퍼에 그림을 그릴 수 있음)
            d3dDevice->CreateRenderTargetView(renderTargets[n].Get(), nullptr, rtvHandle);
            // 다음 배열로 이동
            rtvHandle.ptr += rtvDescriptorSize;
        }

        // Fence (동기화 객체) 생성
        d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        fenceValue = 1;

        // Event 운영체제로 부터 발급 받기
        fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (fenceEvent == nullptr)
        {
            // 에러 처리
        }

        // 파이프라인(PSO) 구축 단계
        // Root Signature 생성 (매개변수가 몇 개 들어가는지 알려줌)
        CD3DX12_DESCRIPTOR_RANGE ranges[1];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // 텍스처 1개 (t0)

        CD3DX12_ROOT_PARAMETER rootParameters[2];
        rootParameters[0].InitAsConstantBufferView(0); // 위치 정보 (b0)
        rootParameters[1].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL); // 텍스처 정보 (t0)

        D3D12_STATIC_SAMPLER_DESC sampler = {}; // 스포이트 설정
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT; // 도트 픽셀 유지
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP; // 도화지 밖으로 나가면 테두리 색으로 고정
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MipLODBias = 0;
        sampler.MaxAnisotropy = 0;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0; // s0
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // 셰이더에게 넘겨줄 매개변수(변환 행렬, 텍스처 등)의 형식을 정의
        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        // 파라미터 개수를 0에서 1로 배열 주소를 남겨둠
        rootSignatureDesc.Init(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
        d3dDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature));

        // shaders.hlsl 파일 컴파일
#if defined(_DEBUG)
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;
        // shaders.hlsl 파일에서 VSMain 함수를 '정점 셰이더(vs_5_0)' 버전으로 컴파일
        D3DCompileFromFile(L"Assets/Shaders/shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr);
        // shaders.hlsl 파일에서 PSMain 함수를 '픽셀 셰이더(ps_5_0)' 버전으로 컴파일
        D3DCompileFromFile(L"Assets/Shaders/shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr);

        // Input Layout 정의
        // Vertex 구조체 (C++ 데이터)가 셰이더의 파라미터 (POSITION, COLOR)와 어떻게 매칭되는지 설명해주는 표
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            // 좌표 데이터 x,y,z 가 12바이트를 차지함
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        // 파이프라인 상태 객체 (PSO) 생성
        // 위에서 만든 셰이더, 레이아웃, 루트 시그니처 등을 하나로 뭉쳐서 GPU에게 규칙을 하달
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.pRootSignature = rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;    // 뒷면도 투명하게 만들지 말고 무조건 그려라 (Culling 끄기)
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE; // 2D 게임이니 깊이 테스트는 일단 꺼둠
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;
        d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)); 

        // Vertex Buffer 생성 함수
        CreateVertexBuffer();

        // 맵 초기화 및 텍스처 로드
        background.Initialize(d3dDevice.Get());
        // 맵 이미지 파일 경로를 넣어주고 프레임은 무조건 1
        background.LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/map_bg.png", 1);

        // 화면을 꽉 채우도록 크기를 크게 늘림 (1280 x 720 화면 비율에 맞추기)
        background.SetScale(2.0f, 2.0f);
        background.SetPosition(0.0f, 0.0f); // 화면 정중앙 배치

        // 플레이어 객체에서 자신의 메모리를 알아서 세팅하도록 명령
        // 플레이어 객체 세팅 & 텍스처 로드 (commandList 전달!)
        player.Initialize(d3dDevice.Get());
        // png 파일 이름과 애니메이션 프레임 수 전달
        player.LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/player_sheet.png", 30);
        player.SetScale(0.45f, 0.45f);

        // 몬스터 10마리 초기화 및 스폰 위치 설정
        for (int i = 0; i < ENEMY_COUNT; i++)
        {
            enemies[i].Initialize(d3dDevice.Get());
            enemies[i].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/enemy_sheet.png", 18);
            enemies[i].SetScale(0.1f, 0.15f);

            // 화면 밖이나 구석에서 스폰되도록 대충 위치를 분산
            float spawnX = (float)(i % 5) * 0.5f - 1.0f; // -1.0 ~ 1.0 사이 분산
            float spawnY = (float)(i / 5) * 0.5f + 0.5f;
            enemies[i].SetPosition(spawnX, spawnY);
        }

        // 모든 텍스처 복사 명령 기록이 끝났으니 Close() 하고 한 방에 실행
        commandList->Close();
        ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
        commandQueue->ExecuteCommandLists(1, ppCommandLists);

        // 이미지 복사가 끝날 때까지 CPU 잠깐 대기
        WaitForGPU();

        // 시간 관리자 시작
        timeMgr.Initialize();
    }

    // 매 프레임 위치를 계산하고 GPU로 데이터를 쏴주는 함수
    void Update()
    {
        timeMgr.Update();
        float dt = timeMgr.GetDeltaTime();

        // background 먼저 호출
        background.Update(dt);

        // 충돌 범위 반지름 세팅
        float playerRadius = 0.15f;
        float enemyRadius = 0.04f;

        // Player vs Enemy 충돌 검사 (속도 저하 로직)

        // 매 프레임 플레이어의 속도를 원래 속도로 원상복구 시킴
        player.currentSpeed = player.basespeed;
        XMFLOAT3 playerPos = player.GetPosition();
        bool isPlayerHit = false;

        for (int i = 0; i < ENEMY_COUNT; i++)
        {
            XMFLOAT3 enemyPos = enemies[i].GetPosition();
            // 피타고라스의 정리
            float dx = playerPos.x - enemyPos.x;
            float dy = playerPos.y - enemyPos.y;
            float dist = sqrt((dx * dx) + (dy * dy));

            // 내 반지름 + 적 반지름 보다 거리가 짧으면? 충돌 (겹침) 발생
            if (dist < playerRadius + enemyRadius)
            {
                isPlayerHit = true;
                break;  // 하나라도 부딪히면 느려지므로 더 검사할 필요 없음
            }
        }

        // 부딪혔다면 속도를 40%로 확 줄임
        if (isPlayerHit)
        {
            player.currentSpeed = player.basespeed * 0.6f;
            // 피격 시 빨간색으로 변경
            player.SetTintColor(1.0f, 0.0f, 0.0f);
        }
        else
        {
            // 피격 받지 않을 시 원래 색상으로 변경
            player.SetTintColor(1.0f, 1.0f, 1.0f);
        }

        // 플레이어 객체 스스로 업데이트하도록 호출
        player.Update(dt, inputMgr);

        // Enemy 이동 및 Enemy vs Enemy 충돌 검사

        // 모든 Enemy가 플레이어의 위치를 향해 돌격
        for (int i = 0; i < ENEMY_COUNT; i++)
        {
            enemies[i].Update(dt, playerPos);
        }
        
        // 적들 끼리 겹치지 않게 서로 밀어내기 (군집 형성의 핵심)
        // i 번째 적과 j 번째 적을 모두 1:1로 짝지어서 비교하는 이중 for문
        for (int i = 0; i < ENEMY_COUNT; i++)
        {
            for (int j = i + 1; j < ENEMY_COUNT; j++)
            {
                XMFLOAT3 pos1 = enemies[i].GetPosition();
                XMFLOAT3 pos2 = enemies[j].GetPosition();

                float dx = pos2.x - pos1.x;
                float dy = pos2.y - pos1.y;
                float dist = sqrt((dx * dx) + (dy * dy));

                // 두 적이 유지해야하는 최소 거리 (반지름 2배)
                float minDistance = enemyRadius * 2.0f;

                // 0.0001f 체크는 둘이 완벽하게 겹쳐서 거리가 0이 될 때 생기는 나눗셈 오류 방지
                if (dist < minDistance && dist > 0.0001f)
                {
                    // 얼마나 겹쳤는지 계산
                    float overlap = minDistance - dist;

                    // 밀어낼 방향 (단위 벡터) 구하기
                    float nx = dx / dist;
                    float ny = dy / dist;

                    // 각각 겹친 깊이의 절반(0.5) 만큼 반대 방향으로 밀어냄
                    float pushX = nx * (overlap * 0.5f);
                    float pushY = ny * (overlap * 0.5f);

                    enemies[i].SetPosition(pos1.x - pushX, pos1.y - pushY);
                    enemies[j].SetPosition(pos2.x + pushX, pos2.y + pushY);
                }
            }
        }
    }

    // 매 프레임 화면을 그리는 함수
    void Render()
    {
        // 메모리 초기화 : CPU가 새로운 명령을 적기 위해 Allocator와 List를 싹 지움
        commandAllocator->Reset();
        commandList->Reset(commandAllocator.Get(), nullptr);

        // Resource Barrier (상태 변화)
        // 현재 도화지는 유저에게 보여주기 위한 출력용 (PRESENT) 상태
        // 출력 중인 도화지에는 그림을 그릴 수 없으니 그리기용 (RENDER_TARGET)으로 상태를 변경
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = renderTargets[frameIndex].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);

        // 화면 칠하기 (파란색)
        // 우리가 쓸 도화지의 목차 주소를 가져옴
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += frameIndex * rtvDescriptorSize;

        // 색상 지정 (R, G, B, A)
        const float clearColor[] = { 0.1f, 0.1f, 0.3f, 1.0f };
        commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

        // Output Merger 세팅: 앞으로 그릴 모든 Draw는 이 rtvHandle에 올림
        commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        // Draw Call 렌더링
        // ViewPort와 Scissor Rect 설정
        // 도화지(800x600) 중에서 어느 영역에 그림을 그릴지 GPU에게 알려주는 영역 설정
        D3D12_VIEWPORT viewport = { 0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f };
        D3D12_RECT scissorRect = { 0, 0, 1280, 720 };
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissorRect);

        // PSO와 Root Signature 적용
        commandList->SetGraphicsRootSignature(rootSignature.Get());
        // 아까 복사해둔 상수 버퍼 데이터(위치 정보)를 이 파이프라인에 묶음
        commandList->SetPipelineState(pipelineState.Get());

        // 점들을 어떻게 이을지 결정 (삼각형 단위로 잇기 위해 TRIANGLELIST)
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // 정점 버퍼 꺼내오기
        commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

        // 배경 맵을 가장 먼저 그림 (캐릭터들이 파묻히지 않게 방지)
        background.Render(commandList.Get());

        // 플레이어 객체 스스로 렌더링 하도록 명령서를 넘겨줌
        player.Render(commandList.Get());

        // Enemy 그리기
        for (int i = 0; i < ENEMY_COUNT; i++)
        {
            enemies[i].Render(commandList.Get());
        }

        // Resource Barrier 복구
        // 다 그렸으니 다시 유저에게 보여주기 위해 출력용 (PRESENT) 상태로 되돌림
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

        commandList->ResourceBarrier(1, &barrier);

        // 명령 기록 끝
        commandList->Close();

        // 작성한 List를 GPU의 대기열(Queue)에 제출
        ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
        commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

        // 스왑 체인 교체 (Flip) - 도화지를 휙 바꿔치기 해서 유저에게 보여줌
        swapChain->Present(1, 0);

        // 동기화 (GPU가 다 그릴 때까지 CPU를 기다리게 함)
        WaitForGPU();
    }

    // CPU가 GPU의 작업 완료를 기다리는 함수
    void WaitForGPU()
    {
        // 큐의 마지막에 fenceValue를 Fence에 적도록 하는 명령을 삽입
        const UINT64 currentFenceValue = fenceValue;
        commandQueue->Signal(fence.Get(), currentFenceValue);
        fenceValue++;

        // 만약 GPU가 아직 그 번호를 Fence에 안 적었다면? (아직 작업 중이라면?)
        if (fence->GetCompletedValue() < currentFenceValue)
        {
            // Event를 설정하고 GPU가 번호를 적을 때까지 CPU를 Wait 시킴
            fence->SetEventOnCompletion(currentFenceValue, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }

        // GPU가 다 그렸으니 이제 다음 프레임에 쓸 도화지 번호 (0 또는 1)를 가져옴
        frameIndex = swapChain->GetCurrentBackBufferIndex();
    }

    void CreateVertexBuffer()
    {
        // 사각형을 이루는 점 6개 (삼각형 2개)의 데이터 배열
        // 화면 정중앙을 (0,0)으로 두고 크기가 1인 사각형
        Vertex quadVertices[] = {
            // 첫 번째 삼각형
            { { -0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f } }, // 좌상단
            { {  0.5f,  0.5f, 0.0f }, { 1.0f, 0.0f } }, // 우상단
            { { -0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f } }, // 좌하단

            // 두 번째 삼각형
            { {  0.5f,  0.5f, 0.0f }, { 1.0f, 0.0f } }, // 우상단
            { {  0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f } }, // 우하단
            { { -0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f } }, // 좌하단
        };

        const UINT vertexBufferSize = sizeof(quadVertices);

        // GPU 메모리에 Upload Heap 속성 설정
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        // 버퍼 (메모리 덩어리)의 크기 및 포맷 설정
        D3D12_RESOURCE_DESC resourceDesc = {};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = vertexBufferSize;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        // 실제로 GPU 메모리 공간에 버퍼 (Resource) 생성
        d3dDevice->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&vertexBuffer));

        // CPU에 있는 배열 데이터를 GPU 메모리로 복사 (Map -> Copy -> Unmap)
        UINT8* pVertexDataBegin;
        D3D12_RANGE readRange = { 0,0 };    // CPU가 이 버퍼를 읽지는 않을 거라는 뜻

        // GPU 메모리의 주소를 CPU가 접근할 수 있게 연결 (Map)
        vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
        // 데이터 복사하기
        memcpy(pVertexDataBegin, quadVertices, sizeof(quadVertices));
        // 복사 끝났으니 연결 해제 (Unmap)
        vertexBuffer->Unmap(0, nullptr);

        // 나중에 랜더링할 때 점 데이터 위치를 알려줄 View 설정
        vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
        vertexBufferView.StrideInBytes = sizeof(Vertex); // 점 1개의 크기
        vertexBufferView.SizeInBytes = vertexBufferSize; // 전체 점들의 크기
    }
};

// 프로그램 시작점인 메인 함수
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	// 윈도우 클래스 설정 및 등록
	// 창의 기본적인 속성 (아이콘, 커서, 이름 등)을 정의하는 구조체
	WNDCLASSEXW wcex = { 0 };
	wcex.cbSize = sizeof(WNDCLASSEXW);
	wcex.style = CS_HREDRAW | CS_VREDRAW;

	// 윈도우 메시지 (클릭, 종료 등)를 처리하는 콜백 함수
	// 외부 함수를 만들지 않고 메인 함수 내부에서 람다(Lambda)로 처리
    wcex.lpfnWndProc = [](HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT WINAPI
    {
        // 창 닫기 버튼을 눌렀을 때의 처리
        if (message == WM_DESTROY)
        {
            PostQuitMessage(0);
            return 0;
        }
        // 그 외의 메시지는 윈도우 기본 처리에 맡김
        return DefWindowProcW(hWnd, message, wParam, lParam);
    };

    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = L"DX12PortfolioClass"; // 이 창의 고유한 클래스 이름

    // 설정한 정보를 운영체제에 등록
    RegisterClassExW(&wcex);

    // 윈도우 창 생성
    // 등록한 클래스 이름을 바탕으로 실제 화면에 띄울 창 객체를 만듦
    HWND hWnd = CreateWindowExW(
        0,
        L"DX12PortfolioClass",          // 등록했던 클래스 이름
        L"Survivors",                   // 창 상단에 뜰 제목
        WS_OVERLAPPEDWINDOW,            // 일반적인 창 스타일 (최소화, 최대화, 닫기 버튼 포함)
        CW_USEDEFAULT, CW_USEDEFAULT,   // 창의 X, Y 시작 위치
        1280, 720,                      // 창의 가로, 세로 크기
        nullptr, nullptr, hInstance, nullptr
    );

    // 창 생성에 실패하면 프로그램 종료
    if (!hWnd)
    {
        return 0;
    }

    // 윈도우 창을 화면에 표시
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // 게임 루프 시작 전에 초기화를 한 번만 실행!
    D3D12Manager d3dManager;
    d3dManager.Initialize(hWnd, 1280, 720);

    // 메시지 루프 (게임 루프)
    // 프로그램이 종료될 때까지 계속해서 도는 무한 루프
    MSG msg = { 0 };
    while (msg.message != WM_QUIT)
    {
        // PeekMessage는 메시지가 없어도 프로그램이 멈추지 않고 바로 다음 코드로 넘어가게 해줌
        // 메시지가 없을 때 (else 문) 게임의 랜더링을 계속 수행 가능
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            // Update로 위치 계산하고 Render로 그리기
            d3dManager.Update();
            d3dManager.Render();
        }
    }

    // 프로그램 정상 종료
    return (int)msg.wParam;
}