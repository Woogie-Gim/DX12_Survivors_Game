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

    // 미사일 배열 및 발사 타이머
    static const int MAX_BULLETS = 50;
    Bullet bullets[MAX_BULLETS];
    float shootTimer = 0.0f;
    float shootInterval = 0.5f; // 0.5초마다 1발씩 자동 발사

    // 플레이어 HP바 (배경 1개, 게이지 1개)
    GameObject hpBarBg;
    GameObject hpBarFill;

    // 젬, 데미지 텍스트, EXP 바
    static const int MAX_GEMS = 200;
    Gem gems[MAX_GEMS];

    static const int MAX_DMG_TEXTS = 50;
    DamageText dmgTexts[MAX_DMG_TEXTS];

    GameObject expBarBg;
    GameObject expBarFill;

    // 레벨 UI 배경과 레벨 텍스트 선언
    GameObject levelBg;
    GameObject levelText;

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
        // WRAP (무한 반복) -> 무한 맵
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
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

        background.SetScale(10.0f, 10.0f);   // 도화지를 화면보다 훨씬 크게 키움
        background.SetUVScale(1.0f, 1.0f);
        background.SetPosition(0.0f, 0.0f);  // 맵 항상 세상의 정중앙에 고정
        background.SetObjectType(0);

        // HP 바 초기화 (배경 이미지를 불러오되 셰이더에서 사각형으로 덮어씀)
        hpBarBg.Initialize(d3dDevice.Get());
        hpBarBg.LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/map_bg.png", 1);
        hpBarBg.SetTintColor(0.2f, 0.2f, 0.2f); // 짙은 회색 배경
        hpBarBg.SetObjectType(2);               // 사각형 사용

        hpBarFill.Initialize(d3dDevice.Get());
        hpBarFill.LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/map_bg.png", 1);
        hpBarFill.SetTintColor(0.0f, 1.0f, 0.0f); // 초록색 체력
        hpBarFill.SetObjectType(2);                 // 사각형 사용


        // 미사일 초기화 (플레이어 이미지를 노란색으로 칠해서 구슬처럼 쏨)
        for (int i = 0; i < MAX_BULLETS; i++)
        {
            bullets[i].Initialize(d3dDevice.Get());
            bullets[i].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/player_sheet.png", 1);
            bullets[i].SetScale(0.05f, 0.05f);
            bullets[i].SetTintColor(1.0f, 1.0f, 0.0f); // 노란색
            bullets[i].SetObjectType(1); // 완벽한 동그라미 사용
            bullets[i].isDead = true; // 처음엔 다 숨겨둠
        }
        
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

        // 경험치 바 (EXP Bar) 초기화
        expBarBg.Initialize(d3dDevice.Get());
        expBarBg.LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/map_bg.png", 1);
        expBarBg.SetTintColor(0.0f, 0.0f, 0.2f); // 짙은 파란색 (배경)
        expBarBg.SetObjectType(2); // 사각형 사용

        expBarFill.Initialize(d3dDevice.Get());
        expBarFill.LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/map_bg.png", 1);
        expBarFill.SetTintColor(0.0f, 0.5f, 1.0f); // 밝은 파란색 (채워지는 바)
        expBarFill.SetObjectType(2); // 사각형 사용

        // 레벨 배경 UI 초기화
        levelBg.Initialize(d3dDevice.Get());
        // 이미지 이름은 실제 저장하신 파일명과 완벽히 똑같이 맞춰주세요!
        levelBg.LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/level_bg.png", 1);
        levelBg.SetScale(0.1f, 0.15f);
        levelBg.SetObjectType(0);

        // 레벨 숫자 텍스트 (데미지 폰트 재활용)
        levelText.Initialize(d3dDevice.Get());
        levelText.LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/damage_font.png", 10);
        levelText.SetScale(0.03f, 0.045f);
        levelText.SetTintColor(1.0f, 1.0f, 1.0f);
        levelText.SetObjectType(0);
        levelText.SetFrameDuration(9999.0f); // 애니메이션 멈춤

        // 경험치 젬 초기화
        for (int i = 0; i < MAX_GEMS; i++)
        {
            gems[i].Initialize(d3dDevice.Get());

            // "gem.png" 같은 진짜 보석 이미지 파일 경로로 변경
            gems[i].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/gem.png", 1);
            gems[i].SetScale(0.04f, 0.06f);

            // 텍스처 원본 색상을 그대로 보여주기 위해 틴트 컬러를 흰색(1,1,1)으로 초기화
            gems[i].SetTintColor(1.0f, 1.0f, 1.0f);

            // 진짜 텍스처를 그리는 모드(0)로 변경
            gems[i].SetObjectType(0);

            gems[i].isDead = true;
        }

        // 데미지 텍스트 초기화
        for (int i = 0; i < MAX_DMG_TEXTS; i++)
        {
            dmgTexts[i].Initialize(d3dDevice.Get());
            // 숫자 0~9 가 일렬로 나열된 스프라이트 시트
            // 숫자가 10개이므로 프레임 수를 '10'으로 설정하여 이미지를 10등분
            dmgTexts[i].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/damage_font.png", 10);
            dmgTexts[i].SetScale(0.04f, 0.06f);
            dmgTexts[i].SetTintColor(1.0f, 1.0f, 1.0f);
            dmgTexts[i].SetObjectType(0);
            // 애니메이션 영원히 정지
            dmgTexts[i].SetFrameDuration(9999.0f);

            dmgTexts[i].isDead = true;
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
            if (enemies[i].isDead) continue; // 죽은 적과는 부딪히지 않음

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

            // 피격 시 dt(시간)을 곱해서 초당 데미지(DPS)를 줌
            player.hp -= 5.0f * dt;

            // 체력이 음수로 안 내려가게 방지
            if (player.hp < 0.0f) player.hp = 0.0f;
        }
        else
        {
            // 피격 받지 않을 시 원래 색상으로 변경
            player.SetTintColor(1.0f, 1.0f, 1.0f);
        }

        // 플레이어 객체 스스로 업데이트하도록 호출 (키보드 이동 반영)
        player.Update(dt, inputMgr);

        // 플레이어 투명 벽 (경계선) 로직
        XMFLOAT3 pPos = player.GetPosition();

        // 카메라 마지노선(camLimit)보다 조금 크게 설정하여, 카메라가 멈춘 상태에서 플레이어가 가장자리로 이동하도록 연출
        float mapLimit = 4.5f;

        if (pPos.x > mapLimit)  pPos.x = mapLimit;   // 오른쪽 벽
        if (pPos.x < -mapLimit) pPos.x = -mapLimit;  // 왼쪽 벽
        if (pPos.y > mapLimit)  pPos.y = mapLimit;   // 위쪽 벽
        if (pPos.y < -mapLimit) pPos.y = -mapLimit;  // 아래쪽 벽

        // 벽에 막힌 최종 위치를 플레이어에게 다시 덮어씌움
        player.SetPosition(pPos.x, pPos.y);

        // 카메라 좌표 설정 (카메라는 항상 최신 플레이어 위치를 따라가되 가두기 적용)
        XMFLOAT2 camPos = { player.GetPosition().x, player.GetPosition().y };

        // 🚨 카메라가 파란색 허공을 비추지 않도록 제한하는 마지노선!
        float camLimit = 4.0f;

        if (camPos.x > camLimit)  camPos.x = camLimit;   // 오른쪽 카메라 정지
        if (camPos.x < -camLimit) camPos.x = -camLimit;  // 왼쪽 카메라 정지
        if (camPos.y > camLimit)  camPos.y = camLimit;   // 위쪽 카메라 정지
        if (camPos.y < -camLimit) camPos.y = -camLimit;  // 아래쪽 카메라 정지

        // 모든 객체에 "제한이 걸린" 카메라 좌표 전달 (플레이어 본인 포함)
        player.SetCameraPos(camPos.x, camPos.y);

        // 새 카메라 위치로 행렬을 다시 계산하기 위한 강제 업데이트 (dt는 0.0f 전달)
        player.GameObject::Update(0.0f);

        // 무한 맵 (배경) 스크롤 로직 ...
        // 배경은 세상의 중심(0,0)에 가만히 있고 카메라만 움직이게
        background.SetCameraPos(camPos.x, camPos.y);
        background.Update(dt);

        // 자동 유도 미사일 발사 로직
        shootTimer += dt;
        if (shootTimer >= shootInterval)
        {
            shootTimer = 0.0f; // 타이머 초기화

            // 배열에서 isDead된 미사일을 하나 찾아서 발사
            for (int i = 0; i < MAX_BULLETS; i++)
            {
                if (bullets[i].isDead)
                {
                    bullets[i].isDead = false;
                    bullets[i].SetPosition(player.GetPosition().x, player.GetPosition().y); // 플레이어 위치에서 스폰
                    break;
                }
            }
        }

        // 살아 있는 미사일들 업데이트, 젬과 데미지 생성
        for (int i = 0; i < MAX_BULLETS; i++)
        {
            if (bullets[i].isDead) continue;
            bullets[i].SetCameraPos(camPos.x, camPos.y); // 미사일에게도 카메라 위치 전달

            // 미사일 로직을 밖으로 빼서 메인루프에서 적의 죽음을 캐치
            float minDist = 9999.0f;
            int targetIdx = -1;

            // 타겟 찾기
            for (int j = 0; j < ENEMY_COUNT; j++)
            {
                if (enemies[j].isDead) continue;
                float dx = enemies[j].GetPosition().x - bullets[i].GetPosition().x;
                float dy = enemies[j].GetPosition().y - bullets[i].GetPosition().y;
                float dist = sqrt((dx * dx) + (dy * dy));
                if (dist < minDist) { minDist = dist; targetIdx = j; }
            }

            // 날아가기 및 명중 처리
            if (targetIdx != -1)
            {
                float dx = enemies[targetIdx].GetPosition().x - bullets[i].GetPosition().x;
                float dy = enemies[targetIdx].GetPosition().y - bullets[i].GetPosition().y;
                float dist = sqrt((dx * dx) + (dy * dy));

                if (dist > 0.0f)
                {
                    bullets[i].SetPosition(bullets[i].GetPosition().x + (dx / dist) * bullets[i].speed * dt,
                        bullets[i].GetPosition().y + (dy / dist) * bullets[i].speed * dt);
                }

                // 적중
                float hitRadius = 0.08f;
                if (dist < hitRadius)
                {
                    enemies[targetIdx].hp -= bullets[i].damage;
                    bullets[i].isDead = true;

                    // 데미지 텍스트 팝업 띄우기
                    for (int k = 0; k < MAX_DMG_TEXTS; k++)
                    {
                        if (dmgTexts[k].isDead)
                        {
                            dmgTexts[k].isDead = false;
                            dmgTexts[k].lifeTime = 0.0f;
                            dmgTexts[k].SetPosition(enemies[targetIdx].GetPosition().x, enemies[targetIdx].GetPosition().y + 0.1f);

                            // 15 데미지면 일단 '5' (프레임 번호 5)를 띄우게 만듦
                            // 한 자릿수만 띄울 수 있으므로 임시로 1의 자리를 구해서 띄움
                            int dmgValue = (int)bullets[i].damage; // 15
                            dmgTexts[k].SetFrame(dmgValue % 10);   // 15 % 10 = 5번 프레임(숫자 5)

                            break;
                        }
                    }

                    // 적이 죽었다면 경험치 젬 드롭
                    if (enemies[targetIdx].hp <= 0.0f)
                    {
                        enemies[targetIdx].isDead = true;

                        for (int g = 0; g < MAX_GEMS; g++)
                        {
                            if (gems[g].isDead)
                            {
                                gems[g].isDead = false;
                                gems[g].SetPosition(enemies[targetIdx].GetPosition().x, enemies[targetIdx].GetPosition().y);
                                break;
                            }
                        }
                    }
                }
            }
            else
            {
                bullets[i].SetPosition(bullets[i].GetPosition().x, bullets[i].GetPosition().y + bullets[i].speed * dt);
            }

            bullets[i].Update(dt);
        }

        // Enemy 이동 및 Enemy vs Enemy 충돌 검사
        playerPos = player.GetPosition(); // 갱신된 플레이어 위치 가져오기

        // 모든 Enemy가 플레이어의 위치를 향해 돌격
        for (int i = 0; i < ENEMY_COUNT; i++)
        {
            // 죽은 적은 움직이지 않음
            if (enemies[i].isDead) continue;

            enemies[i].SetCameraPos(camPos.x, camPos.y); // 적에게도 카메라 위치 전달
            enemies[i].Update(dt, playerPos);
        }

        // 적들 끼리 겹치지 않게 서로 밀어내기 (군집 형성의 핵심)
        for (int i = 0; i < ENEMY_COUNT; i++)
        {
            // 죽은 적은 밀어내기 제외
            if (enemies[i].isDead) continue;

            for (int j = i + 1; j < ENEMY_COUNT; j++)
            {
                if (enemies[j].isDead) continue; // j도 죽었는지 체크

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

        // 젬 초기화 (플레이어에게 다가가서 먹히기)
        for (int i = 0; i < MAX_GEMS; i++)
        {
            if (gems[i].isDead) continue;
            gems[i].SetCameraPos(camPos.x, camPos.y);
            gems[i].Update(dt, player);
        }

        // 데미지 텍스트 초기화
        for (int i = 0; i < MAX_DMG_TEXTS; i++)
        {
            if (dmgTexts[i].isDead) continue;
            dmgTexts[i].SetCameraPos(camPos.x, camPos.y);
            dmgTexts[i].Update(dt);
        }

        // HP바 크기와 위치 실시간 계산
        float barWidth = 0.12f;      // 체력바 전체 가로길이
        float barHeight = 0.02f;    // 체력바 세로 두께
        float hpY = player.GetPosition().y - 0.25f; // 플레이어 위치보다 살짝 아래

        hpBarBg.SetPosition(player.GetPosition().x, hpY); // 위치 세팅
        hpBarBg.SetCameraPos(camPos.x, camPos.y);
        hpBarBg.SetScale(barWidth, barHeight);
        hpBarBg.Update(0.0f); // 애니메이션 없으므로 0.0f 전달

        // 체력 게이지(초록 줄) 계산
        float hpRatio = player.hp / player.maxHp;
        if (hpRatio < 0.0f) hpRatio = 0.0f; // 마이너스 방지

        float currentWidth = barWidth * hpRatio; // 현재 체력만큼 깎인 길이
        float offset = (barWidth - currentWidth) * 0.5f;

        hpBarFill.SetPosition(player.GetPosition().x - offset, hpY); // 위치 세팅
        hpBarFill.SetCameraPos(camPos.x, camPos.y);
        hpBarFill.SetScale(currentWidth, barHeight);

        // 피가 30% 이하면 빨간색으로 변경
        if (hpRatio <= 0.3f) hpBarFill.SetTintColor(1.0f, 0.0f, 0.0f);
        else hpBarFill.SetTintColor(0.0f, 1.0f, 0.0f);

        hpBarFill.Update(0.0f);

        // EXP 바 (화면 맨 위에 고정)
        float expBarWidth = 2.0f;  // 화면 꽉 차는 가로 길이 (-1.0 ~ 1.0)
        float expBarHeight = 0.05f; // 두께

        // 카메라 기준 화면 맨 위 (y + 0.95f 언저리)
        float expY = camPos.y + 0.95f;

        expBarBg.SetPosition(camPos.x, expY);
        expBarBg.SetCameraPos(camPos.x, camPos.y);
        expBarBg.SetScale(expBarWidth, expBarHeight);
        expBarBg.Update(0.0f);

        float expRatio = player.exp / player.maxExp;
        if (expRatio > 1.0f) expRatio = 1.0f;

        float currentExpWidth = expBarWidth * expRatio;
        float expOffset = (expBarWidth - currentExpWidth) * 0.5f;

        expBarFill.SetPosition(camPos.x - expOffset, expY);
        expBarFill.SetCameraPos(camPos.x, camPos.y);
        expBarFill.SetScale(currentExpWidth, expBarHeight);
        expBarFill.Update(0.0f);

        // 레벨 UI (EXP 바 바로 아래 중앙에 배치)
        float lvlY = camPos.y + 0.85f; // EXP 바(0.95f) 보다 살짝 아래

        levelBg.SetPosition(camPos.x, lvlY);
        levelBg.SetCameraPos(camPos.x, camPos.y);
        levelBg.Update(0.0f);

        // 플레이어의 현재 레벨(1의 자리)을 폰트로 띄움
        levelText.SetFrame(player.level % 10);
        // 텍스트를 배경 위에 완벽히 겹치게 배치
        levelText.SetPosition(camPos.x, lvlY);
        levelText.SetCameraPos(camPos.x, camPos.y);
        levelText.Update(0.0f);
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

        // 살아있는 적만 그리기 (죽으면 자연스럽게 화면에서 사라짐)
        for (int i = 0; i < ENEMY_COUNT; i++)
        {
            if (!enemies[i].isDead)
            {
                enemies[i].Render(commandList.Get());
            }
        }

        // 경험치 젬 그리기 (플레이어보다 바닥에)
        for (int i = 0; i < MAX_GEMS; i++)
            if (!gems[i].isDead) gems[i].Render(commandList.Get());

        // 플레이어 객체 스스로 렌더링 하도록 명령서를 넘겨줌
        player.Render(commandList.Get());

        // 발 밑에 체력바 덧그리기 (배경, 초록색 게이지 순서)
        hpBarBg.Render(commandList.Get());
        hpBarFill.Render(commandList.Get());

        // 날아다니는 미사일들 맨 위에 그리기
        for (int i = 0; i < MAX_BULLETS; i++)
        {
            if (!bullets[i].isDead)
            {
                bullets[i].Render(commandList.Get());
            }
        }

        // 데미지 텍스트 그리기 (가장 위에)
        for (int i = 0; i < MAX_DMG_TEXTS; i++)
            if (!dmgTexts[i].isDead) dmgTexts[i].Render(commandList.Get());

        // EXP 바 그리기 (화면 UI 최상단)
        expBarBg.Render(commandList.Get());
        expBarFill.Render(commandList.Get());

        // 레벨 UI 그리기 (가장 마지막에 그려서 맨 위에 덮기)
        levelBg.Render(commandList.Get());
        levelText.Render(commandList.Get());

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