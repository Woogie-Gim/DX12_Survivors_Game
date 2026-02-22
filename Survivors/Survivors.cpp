#include <windows.h>
#include <d3d12.h>      // DX12 코어 기능
#include <dxgi1_4.h>    // 디스플레이 장치 및 스왑 체인 관리
#include <wrl.h>        // Comptr (스마트 포인터) 사용을 위함

using namespace Microsoft::WRL;

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

        // 커맨드 리스트는 생성 직후 기록 가능 상태로 열려 있음
        // 우선 닫음
        commandList->Close();

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
            // 에러 처리 : 이벤트를 못 만들면 안됨
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

        // Resource Barrier 복구
        // 다 그렸으니 다시 유저에게 보여주기 위해 출력용 (PRESENT) 상태로 되돌림
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

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
        800, 600,                       // 창의 가로, 세로 크기
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
            // DX12 초기화를 실행
            D3D12Manager d3dManager;
            d3dManager.Initialize(hWnd, 800, 600);

            // 윈도우 창을 화면에 표시
            ShowWindow(hWnd, nCmdShow);
            UpdateWindow(hWnd);

            d3dManager.Render();
        }
    }

    // 프로그램 정상 종료
    return (int)msg.wParam;
}