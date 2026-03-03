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
    // 게임 상태 (Game State) 열거형
    enum class GameState
    {
        WEAPON_SELECT,  // 처음 시작 시 무기 고르는 상태
        PLAY,           // 정상 플레이 중
        PAUSE,          // ESC 일시 정지
        GAME_OVER,      // HP 0 (사망)
        CLEAR,          // 생존 성공
    };

    // 게임 매니저용 변수들
    // 처음 켜지면 무조건 무기 선택 창 부터
    GameState currentState = GameState::WEAPON_SELECT;

    // 무기 시스템 변수들
    int selectedWeapon = -1;        // 0 : 근접, 1 : 유도 총, 2 : 오라 (주변)
    float attackTimer = 0.0f;
    float attackCooldown = 1.0f;    // 기본 1초마다 공격

    // 오라 전용 변수
    bool isAuraActive = false;
    float auraRadius = 0.5f;

    // 무기 선택 UI용 객체
    GameObject weaponCards[3];
    GameObject weaponIcons[3];

    // 이펙트 & 오라 객체 풀링
    static const int MAX_EFFECTS = 30;
    Effect meleeEffects[MAX_EFFECTS];
    Effect hitEffects[MAX_EFFECTS];
    GameObject auraEffect;  // 오라는 플레이어 몸에 1개만 붙어있으므로 단일 객체

    float gameTimer = 0.0f;                        // 현재 흘러간 시간
    bool isEscPressed = false;                     // ESC 키 꾹 누름 (중복) 방지용 플래그

    GameObject gameOverUI;
    GameObject clearUI;

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

    // Enemy 객체
    static const int ENEMY_COUNT = 60;
    Enemy enemies[ENEMY_COUNT];
    float spawnTimer = 0.0f;
    bool isBossSpawned[4] = { false, false, false, false }; // 5, 10, 15, 20분 보스 등장 여부

    // 중복 로딩 방지용 마스터 스킨들
    GameObject enemySkins[6];
    GameObject bossSkins[4];

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

    // 타이머 폰트 배열 (MM:SS 4자리)
    GameObject timerTexts[4];
    
    // 콜론의 검은색 배경 역할을 할 점 2개 선언
    GameObject timerColonBg[2];
    // 콜론 (:) 역할을 할 점 2개 선언
    GameObject timerColon[2];

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

        // 마스터 텍스처 딱 1번씩만 메모리에 올리기
        enemySkins[0].Initialize(d3dDevice.Get()); enemySkins[0].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/Enemy1.png", 20);
        enemySkins[1].Initialize(d3dDevice.Get()); enemySkins[1].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/Enemy2.png", 20);
        enemySkins[2].Initialize(d3dDevice.Get()); enemySkins[2].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/Enemy3.png", 20);
        enemySkins[3].Initialize(d3dDevice.Get()); enemySkins[3].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/Enemy4.png", 30);
        enemySkins[4].Initialize(d3dDevice.Get()); enemySkins[4].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/Enemy5.png", 30);
        enemySkins[5].Initialize(d3dDevice.Get()); enemySkins[5].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/Enemy6.png", 20);

        bossSkins[0].Initialize(d3dDevice.Get()); bossSkins[0].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/Boss1.png", 20);
        bossSkins[1].Initialize(d3dDevice.Get()); bossSkins[1].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/Boss2.png", 20);
        bossSkins[2].Initialize(d3dDevice.Get()); bossSkins[2].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/Boss3.png", 30);
        bossSkins[3].Initialize(d3dDevice.Get()); bossSkins[3].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/Boss4.png", 20);

        // 몬스터 초기화 및 스폰 위치 설정, 100마리의 몬스터는 로드된 마스터 스킨을 공유만 받음
        for (int i = 0; i < ENEMY_COUNT; i++)
        {
            // 각 몬스터가 자기 위치를 기억할 빈 공간(상수 버퍼)은 각자 만들어야 함
            enemies[i].Initialize(d3dDevice.Get());

            // 일반 몬스터 구역 (0 ~ 55번) - 10마리씩 할당
            if (i < 10) { enemies[i].ShareTextureFrom(enemySkins[0]); enemies[i].enemyType = 0; }
            else if (i < 20) { enemies[i].ShareTextureFrom(enemySkins[1]); enemies[i].enemyType = 0; }
            else if (i < 30) { enemies[i].ShareTextureFrom(enemySkins[2]); enemies[i].enemyType = 1; }
            else if (i < 40) { enemies[i].ShareTextureFrom(enemySkins[3]); enemies[i].enemyType = 1; }
            else if (i < 50) { enemies[i].ShareTextureFrom(enemySkins[4]); enemies[i].enemyType = 2; }
            else if (i < 56) { enemies[i].ShareTextureFrom(enemySkins[5]); enemies[i].enemyType = 2; }

            // 보스 구역 (56 ~ 59번)
            else if (i == 56) { enemies[i].ShareTextureFrom(bossSkins[0]); enemies[i].enemyType = 3; }
            else if (i == 57) { enemies[i].ShareTextureFrom(bossSkins[1]); enemies[i].enemyType = 4; }
            else if (i == 58) { enemies[i].ShareTextureFrom(bossSkins[2]); enemies[i].enemyType = 5; }
            else if (i == 59) { enemies[i].ShareTextureFrom(bossSkins[3]); enemies[i].enemyType = 6; }

            enemies[i].isDead = true; // 태어날 땐 무조건 죽은 상태로 창고 대기
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

        // Game Over 및 Clear UI 초기화
        gameOverUI.Initialize(d3dDevice.Get());
        gameOverUI.LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/GameOver.png", 50);
        gameOverUI.SetScale(0.8f, 1.2f);
        gameOverUI.SetObjectType(0);

        clearUI.Initialize(d3dDevice.Get());
        clearUI.LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/Clear.png", 80);
        clearUI.SetScale(0.8f, 1.2f);
        clearUI.SetObjectType(0);

        // 타이머 텍스트 초기화
        for (int i = 0; i < 4; i++)
        {
            timerTexts[i].Initialize(d3dDevice.Get());
            // 0~9가 10칸으로 나열된 Timer_font.png 사용
            timerTexts[i].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/Timer_font.png", 10);
            timerTexts[i].SetScale(0.04f, 0.06f); // 데미지 폰트보다 살짝 작거나 비슷하게
            timerTexts[i].SetTintColor(1.0f, 1.0f, 1.0f); // 하얀색
            timerTexts[i].SetObjectType(0);
            timerTexts[i].SetFrameDuration(9999.0f); // 애니메이션 멈춤
        }

        // 콜론(:) 초기화
        for (int i = 0; i < 2; i++)
        {
            // 검은색 배경 점 (테두리 역할)
            timerColonBg[i].Initialize(d3dDevice.Get());
            timerColonBg[i].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/map_bg.png", 1);
            timerColonBg[i].SetScale(0.015f, 0.02f); // 흰색 점보다 약간 크게
            timerColonBg[i].SetTintColor(0.0f, 0.0f, 0.0f); // 완벽한 검은색
            timerColonBg[i].SetObjectType(1); // 동그라미 셰이더 재활용

            // 흰색 점
            timerColon[i].Initialize(d3dDevice.Get());
            timerColon[i].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/map_bg.png", 1);
            timerColon[i].SetScale(0.01f, 0.015f); // 원래 크기
            timerColon[i].SetTintColor(1.0f, 1.0f, 1.0f); // 하얀색
            timerColon[i].SetObjectType(1);
        }

        // 무기 선택 카드 UI 초기화
        weaponCards[0].Initialize(d3dDevice.Get());
        weaponCards[0].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/weapon_card_1.png", 1);
        weaponCards[0].SetScale(0.6f, 0.95f);
        weaponCards[0].SetObjectType(0);

        weaponCards[1].Initialize(d3dDevice.Get());
        weaponCards[1].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/weapon_card_2.png", 1);
        weaponCards[1].SetScale(0.6f, 0.95f);
        weaponCards[1].SetObjectType(0);

        weaponCards[2].Initialize(d3dDevice.Get());
        weaponCards[2].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/weapon_card_3.png", 1);
        weaponCards[2].SetScale(0.6f, 0.95f);
        weaponCards[2].SetObjectType(0);

        // 카드 위에 올라갈 무기 아이콘 초기화
        for (int i = 0; i < 3; i++)
        {
            weaponIcons[i].Initialize(d3dDevice.Get());
            weaponIcons[i].SetScale(0.3f, 0.3f); // 카드보다 작게 크기 조절
            weaponIcons[i].SetObjectType(0);
        }

        // 각각 지정된 이름의 텍스처 로드
        weaponIcons[0].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/MELEE.png", 1);
        weaponIcons[1].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/BULLET.png", 1);
        weaponIcons[2].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/AURA.png", 1);

        // 이펙트 초기화
        for (int i = 0; i < MAX_EFFECTS; i++)
        {
            meleeEffects[i].Initialize(d3dDevice.Get());
            meleeEffects[i].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/weapon_melee.png", 30);
            meleeEffects[i].SetScale(0.3f, 0.3f);
            meleeEffects[i].SetObjectType(0);
            meleeEffects[i].SetFrameDuration(0.016f); // 이펙트는 빠르게 재생
            meleeEffects[i].isDead = true;

            hitEffects[i].Initialize(d3dDevice.Get());
            hitEffects[i].LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/weapon_bullet_hit.png", 30);
            hitEffects[i].SetScale(0.2f, 0.2f);
            hitEffects[i].SetObjectType(0);
            hitEffects[i].SetFrameDuration(0.016f);
            hitEffects[i].isDead = true;
        }

        // 오라 이펙트
        auraEffect.Initialize(d3dDevice.Get());
        auraEffect.LoadTexture(d3dDevice.Get(), commandList.Get(), "Assets/Textures/weapon_aura.png", 30);
        auraEffect.SetScale(auraRadius * 2.0f, auraRadius * 2.0f); // 반지름의 2배 = 지름
        auraEffect.SetObjectType(0);
        auraEffect.SetFrameDuration(0.016f);

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

        // ESC 키 일시정지 (PAUSE) 토글 로직
        if (inputMgr.IsKeyPressed(VK_ESCAPE))
        {
            if (!isEscPressed)  // 키를 누르는 그 순간 딱 한 번만 작동
            {
                if (currentState == GameState::PLAY)
                {
                    currentState = GameState::PAUSE;
                }
                else if (currentState == GameState::PAUSE)
                {
                    currentState = GameState::PLAY;
                }
                isEscPressed = true;
            }
        }
        else
        {
            isEscPressed = false;   // 키를 떼면 다시 누를 수 있게 리셋
        }

        // 공용 카메라 위치 계산
        XMFLOAT2 camPos = { player.GetPosition().x, player.GetPosition().y };
        float camLimit = 4.0f;

        if (camPos.x > camLimit)  camPos.x = camLimit;
        if (camPos.x < -camLimit) camPos.x = -camLimit;
        if (camPos.y > camLimit)  camPos.y = camLimit;
        if (camPos.y < -camLimit) camPos.y = -camLimit;

        // 무기 선택 창 (WEAPON_SELECT)
        if (currentState == GameState::WEAPON_SELECT)
        {
            // 배경과 플레이어가 정지된 상태로 화면에 그려지도록 위치 업데이트 유지
            player.SetCameraPos(camPos.x, camPos.y);
            player.GameObject::Update(0.0f);

            background.SetCameraPos(camPos.x, camPos.y);
            background.Update(0.0f);

            // 카드 3장을 화면 중앙에 나란히 배치 (크기 및 간격 확장)
            float spacing = 0.7f;
            float iconOffsetY = 0.05f;

            weaponCards[0].SetScale(0.6f, 0.95f);
            weaponCards[1].SetScale(0.6f, 0.95f);
            weaponCards[2].SetScale(0.6f, 0.95f);

            weaponCards[0].SetPosition(camPos.x - spacing, camPos.y);
            weaponCards[1].SetPosition(camPos.x, camPos.y);
            weaponCards[2].SetPosition(camPos.x + spacing, camPos.y);

            // 아이콘 위치 세팅 (카드 위치와 동일하게 맞춤)
            weaponIcons[0].SetPosition(camPos.x - spacing, camPos.y + iconOffsetY);
            weaponIcons[1].SetPosition(camPos.x, camPos.y + iconOffsetY);
            weaponIcons[2].SetPosition(camPos.x + spacing, camPos.y + iconOffsetY);

            for (int i = 0; i < 3; i++)
            {
                weaponCards[i].SetCameraPos(camPos.x, camPos.y);
                weaponCards[i].Update(0.0f);

                // 아이콘도 카메라와 매트릭스 업데이트
                weaponIcons[i].SetCameraPos(camPos.x, camPos.y);
                weaponIcons[i].Update(0.0f);
            }

            // 마우스 클릭 감지 로직 (Windows API)
            if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)  // 마우스 왼쪽 버튼 클릭 시
            {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(FindWindowW(L"DX12PortfolioClass", nullptr), &pt);

                // 마우스 좌표(픽셀)를 화면 좌표계 (-1.0 ~ 1.0)로 변환
                float mouseX = (pt.x * 2.0f / 1280.0f) - 1.0f;

                // 카드가 3등분 된 영역 중 어디를 클릭했는지 판별
                if (mouseX < -0.33f) selectedWeapon = 0;      // 왼쪽 클릭 -> 근접
                else if (mouseX < 0.33f) selectedWeapon = 1;  // 중앙 클릭 -> 총
                else selectedWeapon = 2;                      // 오른쪽 클릭 -> 오라      

                // 무기 고르면 게임 시작
                currentState = GameState::PLAY;
                Sleep(200); // 연속 클릭 방지용 아주 짧은 딜레이
            }
        }
        // 오직 PLAY 상태일 때만 게임 세계의 시간이 흐름
        else if (currentState == GameState::PLAY)
        {
            // 타이머 증가 및 클리어 체크
            gameTimer += dt;

            // 사망 체크
            if (player.hp <= 0.0f)
            {
                currentState = GameState::GAME_OVER;
            }

            // 충돌 범위 반지름 세팅
            float playerRadius = 0.08f;
            float enemyRadius = 0.08f;

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

                // 내 반지름 + 적 반지름 보다 거리가 짧으면 충돌 (겹침) 발생
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
            camPos = { player.GetPosition().x, player.GetPosition().y };

            // 카메라가 파란색 허공을 비추지 않도록 제한하는 마지노선
            if (camPos.x > camLimit)  camPos.x = camLimit;   // 오른쪽 카메라 정지
            if (camPos.x < -camLimit) camPos.x = -camLimit;  // 왼쪽 카메라 정지
            if (camPos.y > camLimit)  camPos.y = camLimit;   // 위쪽 카메라 정지
            if (camPos.y < -camLimit) camPos.y = -camLimit;  // 아래쪽 카메라 정지

            // 모든 객체에 제한이 걸린 카메라 좌표 전달 (플레이어 본인 포함)
            player.SetCameraPos(camPos.x, camPos.y);

            // 새 카메라 위치로 행렬을 다시 계산하기 위한 강제 업데이트
            player.GameObject::Update(0.0f);

            // 무한 맵 (배경) 스크롤 로직
            // 배경은 세상의 중심(0,0)에 가만히 있고 카메라만 움직이게
            background.SetCameraPos(camPos.x, camPos.y);
            background.Update(dt);

            // 무기 공격 쿨타임 로직
            attackTimer += dt;

            if (attackTimer >= attackCooldown)
            {
                attackTimer = 0.0f; // 쿨타임 리셋

                // 근접 공격
                if (selectedWeapon == 0)
                {
                    // 플레이어가 보는 방향 (isFlipped)에 따라 이펙트 위치 결정
                    float dir = player.GetIsFlipped() ? -1.0f : 1.0f;
                    float attackX = pPos.x + (0.2f * dir);

                    // 이펙트 생성
                    for (int i = 0; i < MAX_EFFECTS; i++)
                    {
                        if (meleeEffects[i].isDead)
                        {
                            meleeEffects[i].isDead = false;
                            meleeEffects[i].SetPosition(attackX, pPos.y);
                            meleeEffects[i].SetFlipped(player.GetIsFlipped());
                            break;
                        }
                    }

                    // 데미지 판정 (앞쪽 네모 박스 안의 적 타격 - 범위 축소 적용)
                    for (int i = 0; i < ENEMY_COUNT; i++)
                    {
                        if (enemies[i].isDead) continue;

                        float ex = enemies[i].GetPosition().x;
                        float ey = enemies[i].GetPosition().y;

                        // 방향이 맞고 거리가 가까우면 hit
                        if (((dir > 0 && ex > pPos.x) || (dir < 0 && ex < pPos.x)) && abs(ex - pPos.x) < 0.5f && abs(ey - pPos.y) < 0.3f)
                        {
                            // 기본 15 데미지에 -1 ~ +1 랜덤 오차 적용 (즉 14, 15, 16 중 하나가 뜸)
                            int randomDmg = 15 + (rand() % 3 - 1);
                            enemies[i].hp -= (float)randomDmg;

                            // 데미지 텍스트 팝업 띄우기
                            for (int k = 0; k < MAX_DMG_TEXTS; k++)
                            {
                                if (dmgTexts[k].isDead)
                                {
                                    dmgTexts[k].isDead = false;
                                    dmgTexts[k].lifeTime = 0.0f;
                                    dmgTexts[k].SetPosition(ex, ey + 0.1f);

                                    // 고정 숫자 대신 방금 뽑은 랜덤 데미지를 출력
                                    dmgTexts[k].SetFrame(randomDmg % 10);
                                    break;
                                }
                            }

                            // 적이 죽었다면 사망 처리 및 경험치 젬 드롭
                            if (enemies[i].hp <= 0.0f)
                            {
                                enemies[i].isDead = true;

                                if (enemies[i].enemyType == 6)
                                {
                                    currentState = GameState::CLEAR;
                                }

                                for (int g = 0; g < MAX_GEMS; g++)
                                {
                                    if (gems[g].isDead)
                                    {
                                        gems[g].isDead = false;
                                        gems[g].SetPosition(enemies[i].GetPosition().x, enemies[i].GetPosition().y);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                else if (selectedWeapon == 1)   // 총 (유도탄)
                {
                    // 배열에서 isDead된 미사일을 하나 찾아서 발사
                    for (int i = 0; i < MAX_BULLETS; i++)
                    {
                        if (bullets[i].isDead)
                        {
                            bullets[i].isDead = false;
                            bullets[i].lifeTime = 0.0f; // 쏠 때마다 수명을 0으로 새롭게 세팅
                            bullets[i].SetPosition(pPos.x, pPos.y); // 플레이어 위치에서 스폰
                            break;
                        }
                    }
                }
                else if (selectedWeapon == 2)   // 오라 (주변 공격)
                {
                    // 쿨타임이 돌 때마다 오라가 잠깐 켜졌다가 꺼짐 (지속 데미지)
                    isAuraActive = true;
                }
            }

            // 오라가 켜져 있을 때 (플레이어 따라다니며 닿는 적 피격)
            if (selectedWeapon == 2 && isAuraActive)
            {
                auraEffect.SetPosition(pPos.x, pPos.y);
                auraEffect.SetCameraPos(camPos.x, camPos.y);
                auraEffect.Update(dt); // 빙글빙글 돌릴 수 있음

                // 텍스트 폭주를 막기 위한 틱 타이머
                static float auraTextTimer = 0.0f;
                auraTextTimer += dt;
                bool shouldPopText = false;

                if (auraTextTimer >= 0.2f)
                {
                    shouldPopText = true;
                    auraTextTimer = 0.0f;
                }

                for (int i = 0; i < ENEMY_COUNT; i++)
                {
                    if (enemies[i].isDead) continue;

                    float dx = enemies[i].GetPosition().x - pPos.x;
                    float dy = enemies[i].GetPosition().y - pPos.y;

                    if (sqrt(dx * dx + dy * dy) < auraRadius)
                    {
                        // 데미지는 매 프레임 부드럽게 들어감
                        enemies[i].hp -= 15.0f * dt;

                        // 틱 타이머가 허락할 때만 텍스트 팝업
                        if (shouldPopText)
                        {
                            for (int k = 0; k < MAX_DMG_TEXTS; k++)
                            {
                                if (dmgTexts[k].isDead)
                                {
                                    dmgTexts[k].isDead = false;
                                    dmgTexts[k].lifeTime = 0.0f;
                                    dmgTexts[k].SetPosition(enemies[i].GetPosition().x, enemies[i].GetPosition().y + 0.1f);

                                    // 🌟 [수정] 3 ~ 4 중 하나가 랜덤으로 뜨도록 설정 (기본 데미지가 15로 올랐으므로 숫자를 조금 더 크게 표시)
                                    int randomAuraDmg = 3 + (rand() % 2);
                                    dmgTexts[k].SetFrame(randomAuraDmg);
                                    break;
                                }
                            }
                        }

                        // 체력이 0 이하가 되면 죽음 처리 및 젬 드롭
                        if (enemies[i].hp <= 0.0f)
                        {
                            enemies[i].isDead = true;

                            if (enemies[i].enemyType == 6)
                            {
                                currentState = GameState::CLEAR;
                            }

                            for (int g = 0; g < MAX_GEMS; g++)
                            {
                                if (gems[g].isDead)
                                {
                                    gems[g].isDead = false;
                                    gems[g].SetPosition(enemies[i].GetPosition().x, enemies[i].GetPosition().y);
                                    break;
                                }
                            }
                        }
                    }
                }

                // 0.5초 켜져있다가 꺼짐 (나중에 업그레이드로 무한 지속 가능)
                if (attackTimer > 0.5f)
                {
                    isAuraActive = false;
                }
            }

            // 살아있는 이펙트들 업데이트
            for (int i = 0; i < MAX_EFFECTS; i++)
            {
                meleeEffects[i].SetCameraPos(camPos.x, camPos.y);
                meleeEffects[i].Update(dt);

                hitEffects[i].SetCameraPos(camPos.x, camPos.y);
                hitEffects[i].Update(dt);
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

                    if (dist < minDist)
                    {
                        minDist = dist;
                        targetIdx = j;
                    }
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
                        // 총알 기본 데미지에 -1 ~ +1 랜덤 오차 적용 (예: 데미지가 5라면 4, 5, 6 중 하나가 적용됨)
                        int randomDmg = (int)bullets[i].damage + (rand() % 3 - 1);
                        enemies[targetIdx].hp -= (float)randomDmg;                       
                        bullets[i].isDead = true;

                        // 총알 피격 이펙트 띄우기
                        for (int k = 0; k < MAX_EFFECTS; k++)
                        {
                            if (hitEffects[k].isDead)
                            {
                                hitEffects[k].isDead = false;
                                hitEffects[k].SetPosition(enemies[targetIdx].GetPosition().x, enemies[targetIdx].GetPosition().y);
                                break;
                            }
                        }

                        // 데미지 텍스트 팝업 띄우기
                        for (int k = 0; k < MAX_DMG_TEXTS; k++)
                        {
                            if (dmgTexts[k].isDead)
                            {
                                dmgTexts[k].isDead = false;
                                dmgTexts[k].lifeTime = 0.0f;
                                dmgTexts[k].SetPosition(enemies[targetIdx].GetPosition().x, enemies[targetIdx].GetPosition().y + 0.1f);

                                // 무조건 5가 아니라, 방금 계산된 랜덤 데미지 숫자를 띄움
                                dmgTexts[k].SetFrame(randomDmg % 10);
                                break;
                            }
                        }

                        // 타입 6 (최종 보스) 적이 죽었다면
                        if (enemies[targetIdx].enemyType == 6)
                        {
                            currentState = GameState::CLEAR;
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

            // 20분 (1200초) 웨이브 & 보스 매니저
            spawnTimer += dt;
            int currentMinute = (int)(gameTimer / 60.0f);
            float spawnInterval = max(0.3f, 2.5f - (currentMinute * 0.15f));

            auto SpawnEnemy = [&](int targetType, float x, float y)
                {
                    int startIndex = rand() % ENEMY_COUNT;

                    for (int i = 0; i < ENEMY_COUNT; i++)
                    {
                        int idx = (startIndex + i) % ENEMY_COUNT;

                        if (enemies[idx].isDead && enemies[idx].enemyType == targetType)
                        {
                            enemies[idx].InitStats();

                            // 시간에 따른 체력 뻥튀기 로직 (1초마다 체력 0.1씩 증가)
                            // 10분이 지나면 (600초 * 0.1 = 60) 기본 몹 체력이 30에서 90으로 뛰어오름
                            float hpBonus = gameTimer * 0.1f;
                            enemies[idx].maxHp += hpBonus;
                            enemies[idx].hp = enemies[idx].maxHp;

                            enemies[idx].SetPosition(x, y);
                            break;
                        }
                    }
                };

            // 보스 스폰 로직 (5, 10, 15분엔 중간 보스 / 19분에 최종 보스)
            if (currentMinute == 5 && !isBossSpawned[0])
            {
                SpawnEnemy(3, pPos.x + 2.5f, pPos.y);
                isBossSpawned[0] = true;
            }
            else if (currentMinute == 10 && !isBossSpawned[1])
            {
                SpawnEnemy(4, pPos.x - 2.5f, pPos.y);
                isBossSpawned[1] = true;
            }
            else if (currentMinute == 15 && !isBossSpawned[2])
            {
                SpawnEnemy(5, pPos.x, pPos.y + 2.5f);
                isBossSpawned[2] = true;
            }
            else if (currentMinute >= 19 && !isBossSpawned[3])
            {
                SpawnEnemy(6, pPos.x, pPos.y - 2.5f);
                isBossSpawned[3] = true;
            }

            // 20분 스케일 일반 웨이브 스폰 로직
            if (spawnTimer >= spawnInterval)
            {
                spawnTimer = 0.0f;

                if (currentMinute < 5)
                {
                    for (int k = 0; k < 6; k++)
                    {
                        float angle = k * (XM_2PI / 6.0f);
                        SpawnEnemy(0, pPos.x + cos(angle) * 1.5f, pPos.y + sin(angle) * 1.5f);
                    }
                }
                else if (currentMinute < 10)
                {
                    for (int k = 0; k < 6; k++)
                    {
                        float offsetX = (pPos.x - 2.0f) + (k * 0.8f);
                        SpawnEnemy(1, offsetX, pPos.y + 1.5f);
                        SpawnEnemy(1, offsetX, pPos.y - 1.5f);
                    }
                }
                else if (currentMinute < 15)
                {
                    SpawnEnemy(2, pPos.x + 1.5f, pPos.y);
                    SpawnEnemy(2, pPos.x - 1.5f, pPos.y);
                    SpawnEnemy(2, pPos.x, pPos.y + 1.5f);
                    SpawnEnemy(2, pPos.x, pPos.y - 1.5f);

                    for (int k = 0; k < 5; k++)
                    {
                        float angle = k * (XM_2PI / 5.0f) + 0.5f;
                        SpawnEnemy(1, pPos.x + cos(angle) * 1.5f, pPos.y + sin(angle) * 1.5f);
                    }
                }
                else
                {
                    for (int k = 0; k < 10; k++)
                    {
                        float angle = k * (XM_2PI / 10.0f);
                        SpawnEnemy(k % 3, pPos.x + cos(angle) * 1.5f, pPos.y + sin(angle) * 1.5f);
                    }
                }
            }

            // 모든 Enemy가 플레이어의 위치를 향해 돌격
            for (int i = 0; i < ENEMY_COUNT; i++)
            {
                // 죽은 적은 움직이지 않음
                if (enemies[i].isDead) continue;

                enemies[i].SetCameraPos(camPos.x, camPos.y); // 적에게도 카메라 위치 전달
                enemies[i].Update(dt, pPos);
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
                    float minDistance = 0.2f;

                    // 0.0001f 체크는 둘이 완벽하게 겹쳐서 거리가 0이 될 때 생기는 나눗셈 오류 방지
                    if (dist < minDistance && dist > 0.0001f)
                    {
                        // 얼마나 겹쳤는지 계산
                        float overlap = minDistance - dist;

                        // 밀어낼 방향 (단위 벡터) 구하기
                        float nx = dx / dist;
                        float ny = dy / dist;

                        // dt를 곱해서 프레임 속도에 맞춰 0.5배 속도로 아주 부드럽게 밀어냄
                        float pushStrength = 0.5f * dt;
                        float pushX = nx * (overlap * pushStrength);
                        float pushY = ny * (overlap * pushStrength);

                        enemies[i].SetPosition(pos1.x - pushX, pos1.y - pushY);
                        enemies[j].SetPosition(pos2.x + pushX, pos2.y + pushY);
                    }
                }
            }

            // 젬 (플레이어에게 다가가서 먹히기)
            for (int i = 0; i < MAX_GEMS; i++)
            {
                if (gems[i].isDead) continue;

                gems[i].SetCameraPos(camPos.x, camPos.y);
                gems[i].Update(dt, player);
            }

            // 데미지 텍스트
            for (int i = 0; i < MAX_DMG_TEXTS; i++)
            {
                if (dmgTexts[i].isDead) continue;

                dmgTexts[i].SetCameraPos(camPos.x, camPos.y);
                dmgTexts[i].Update(dt);
            }

            // HP바 크기와 위치 실시간 계산
            float barWidth = 0.12f;      // 체력바 전체 가로길이
            float barHeight = 0.02f;    // 체력바 세로 두께
            float hpY = pPos.y - 0.25f; // 플레이어 위치보다 살짝 아래

            hpBarBg.SetPosition(pPos.x, hpY); // 위치 세팅
            hpBarBg.SetCameraPos(camPos.x, camPos.y);
            hpBarBg.SetScale(barWidth, barHeight);
            hpBarBg.Update(0.0f); // 애니메이션 없으므로 0.0f 전달

            // 체력 게이지(초록 줄) 계산
            float hpRatio = player.hp / player.maxHp;

            if (hpRatio < 0.0f) hpRatio = 0.0f; // 마이너스 방지

            float currentWidth = barWidth * hpRatio; // 현재 체력만큼 깎인 길이
            float offset = (barWidth - currentWidth) * 0.5f;

            hpBarFill.SetPosition(pPos.x - offset, hpY); // 위치 세팅
            hpBarFill.SetCameraPos(camPos.x, camPos.y);
            hpBarFill.SetScale(currentWidth, barHeight);

            // 피가 30% 이하면 빨간색으로 변경
            if (hpRatio <= 0.3f)
            {
                hpBarFill.SetTintColor(1.0f, 0.0f, 0.0f);
            }
            else
            {
                hpBarFill.SetTintColor(0.0f, 1.0f, 0.0f);
            }

            hpBarFill.Update(0.0f);

            // EXP 바 (화면 맨 위에 고정)
            float expBarWidth = 2.0f;
            float expBarHeight = 0.05f;
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

            // 레벨 UI (우측 상단)
            float uiY = camPos.y + 0.85f; // EXP 바 살짝 아래
            float levelX = camPos.x + 0.8f; // 화면 우측으로 이동

            levelBg.SetPosition(levelX, uiY);
            levelBg.SetCameraPos(camPos.x, camPos.y);
            levelBg.Update(0.0f);

            levelText.SetFrame(player.level % 10);
            levelText.SetPosition(levelX, uiY);
            levelText.SetCameraPos(camPos.x, camPos.y);
            levelText.Update(0.0f);


            // 타이머 시스템 (화면 중앙 상단 배치)
            // 전체 시간을 분(MM)과 초(SS)로 쪼개기
            int minutes = (int)(gameTimer / 60.0f);
            int seconds = (int)gameTimer % 60;

            // 각 자릿수 추출 (예: 12분 34초 -> m1=1, m2=2, s1=3, s2=4)
            int m1 = (minutes / 10) % 10;
            int m2 = minutes % 10;
            int s1 = (seconds / 10) % 10;
            int s2 = seconds % 10;

            // 폰트에 프레임(숫자) 적용
            timerTexts[0].SetFrame(m1);
            timerTexts[1].SetFrame(m2);
            timerTexts[2].SetFrame(s1);
            timerTexts[3].SetFrame(s2);

            // 폰트 간격 설정 (가운데를 살짝 띄워서 ':' 역할을 대신함)
            float spacingTime = 0.04f;
            float gap = 0.03f; // 콜론(:)이 들어갈 빈 공간

            timerTexts[0].SetPosition(camPos.x - spacingTime - gap, uiY);
            timerTexts[1].SetPosition(camPos.x - gap, uiY);
            timerTexts[2].SetPosition(camPos.x + gap, uiY);
            timerTexts[3].SetPosition(camPos.x + spacingTime + gap, uiY);

            for (int i = 0; i < 4; i++)
            {
                timerTexts[i].SetCameraPos(camPos.x, camPos.y);
                timerTexts[i].Update(0.0f);
            }

            // 콜론 (:) 위치 잡기
            // X좌표는 화면 정중앙(camPos.x), Y좌표는 타이머 기준 위/아래로 살짝 벌림
            // Y좌표 세팅 (위쪽 점, 아래쪽 점)
            float colonTopY = uiY + 0.015f;
            float colonBottomY = uiY - 0.015f;

            // 검은색 배경 점 (뒤에 그릴 예정)
            timerColonBg[0].SetPosition(camPos.x, colonTopY);
            timerColonBg[1].SetPosition(camPos.x, colonBottomY);

            // 흰색 점 (앞에 그릴 예정)
            timerColon[0].SetPosition(camPos.x, colonTopY);
            timerColon[1].SetPosition(camPos.x, colonBottomY);

            // 업데이트 호출 (카메라 좌표 전달)
            for (int i = 0; i < 2; i++)
            {
                timerColonBg[i].SetCameraPos(camPos.x, camPos.y);
                timerColonBg[i].Update(0.0f);

                timerColon[i].SetCameraPos(camPos.x, camPos.y);
                timerColon[i].Update(0.0f);
            }
        }

        // 게임 오버 / 클리어 UI 위치 동기화 (항상 화면 정중앙)
        // 카메라 위치에 정확히 겹치게 띄우고 애니메이션을 재생
        if (currentState == GameState::GAME_OVER)
        {
            gameOverUI.SetPosition(camPos.x, camPos.y);
            gameOverUI.SetCameraPos(camPos.x, camPos.y);
            // dt를 넣어서 타이머가 굴러가게 애니메이션 재생
            gameOverUI.Update(dt);
        }
        else if (currentState == GameState::CLEAR)
        {
            clearUI.SetPosition(camPos.x, camPos.y);
            clearUI.SetCameraPos(camPos.x, camPos.y);
            // dt를 넣어서 승리 애니메이션이 재생
            clearUI.Update(dt);
        }
    }

    // 매 프레임 화면을 그리는 함수
    void Render()
    {
        // 메모리 초기화 : CPU가 새로운 명령을 적기 위해 Allocator와 List를 싹 지움
        commandAllocator->Reset();
        commandList->Reset(commandAllocator.Get(), nullptr);

        // Resource Barrier (상태 변화: 출력용 -> 그리기용)
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = renderTargets[frameIndex].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);

        // 화면 칠하기 (파란색)
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += frameIndex * rtvDescriptorSize;
        const float clearColor[] = { 0.1f, 0.1f, 0.3f, 1.0f };
        commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

        // Output Merger 및 파이프라인 세팅
        commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
        D3D12_VIEWPORT viewport = { 0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f };
        D3D12_RECT scissorRect = { 0, 0, 1280, 720 };
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissorRect);
        commandList->SetGraphicsRootSignature(rootSignature.Get());
        commandList->SetPipelineState(pipelineState.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

        // [Layer 1] 배경 맵 (가장 밑바닥)
        background.Render(commandList.Get());

        // [Layer 2] 경험치 젬 (바닥에 떨어져 있음)
        for (int i = 0; i < MAX_GEMS; i++)
        {
            if (!gems[i].isDead) gems[i].Render(commandList.Get());
        }

        // [Layer 3] 전기 오라 이펙트 (적과 플레이어 발밑에 깔리게 연출)
        if (currentState == GameState::PLAY && selectedWeapon == 2 && isAuraActive)
        {
            auraEffect.Render(commandList.Get());
        }

        // [Layer 4] 살아있는 적군들
        for (int i = 0; i < ENEMY_COUNT; i++)
        {
            if (!enemies[i].isDead) enemies[i].Render(commandList.Get());
        }

        // [Layer 5] 플레이어 (항상 적군보다 위에 보이게)
        player.Render(commandList.Get());

        // [Layer 6] 날아다니는 미사일 (캐릭터들 위로 날아감)
        for (int i = 0; i < MAX_BULLETS; i++)
        {
            if (!bullets[i].isDead) bullets[i].Render(commandList.Get());
        }

        // [Layer 7] 타격 이펙트
        for (int i = 0; i < MAX_EFFECTS; i++)
        {
            if (!meleeEffects[i].isDead) meleeEffects[i].Render(commandList.Get());
            if (!hitEffects[i].isDead) hitEffects[i].Render(commandList.Get());
        }

        // UI 레이어 (게임 화면과 무관하게 항상 최상단에 뜨는 요소들)

        // 플레이어 체력바 (발 밑)
        hpBarBg.Render(commandList.Get());
        hpBarFill.Render(commandList.Get());

        // 피격 데미지 텍스트 (체력바 위로 올라가게)
        for (int i = 0; i < MAX_DMG_TEXTS; i++)
        {
            if (!dmgTexts[i].isDead) dmgTexts[i].Render(commandList.Get());
        }

        // 상단 시스템 UI (경험치바, 레벨, 타이머)
        expBarBg.Render(commandList.Get());
        expBarFill.Render(commandList.Get());

        levelBg.Render(commandList.Get());
        levelText.Render(commandList.Get());

        for (int i = 0; i < 4; i++) timerTexts[i].Render(commandList.Get());
        for (int i = 0; i < 2; i++) timerColonBg[i].Render(commandList.Get());
        for (int i = 0; i < 2; i++) timerColon[i].Render(commandList.Get());

        // [Layer 8] 무기 선택 카드 (게임 UI를 가리면서 화면 정중앙에 뜸)
        if (currentState == GameState::WEAPON_SELECT)
        {
            // 배경 카드를 먼저 그리고
            for (int i = 0; i < 3; i++)
            {
                weaponCards[i].Render(commandList.Get());
            }

            // 그 위에 아이콘을 덮어 씌움
            for (int i = 0; i < 3; i++)
            {
                weaponIcons[i].Render(commandList.Get());
            }
        }

        // [Layer 9] 시스템 메시지 UI (가장 마지막에 덮음)
        if (currentState == GameState::GAME_OVER)
        {
            gameOverUI.Render(commandList.Get());
        }
        else if (currentState == GameState::CLEAR)
        {
            clearUI.Render(commandList.Get());
        }

        // Resource Barrier 복구 (그리기용 -> 출력용)
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        commandList->ResourceBarrier(1, &barrier);

        // 명령 기록 끝 & 실행
        commandList->Close();
        ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
        commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

        // 스왑 체인 교체 & 동기화
        swapChain->Present(1, 0);
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