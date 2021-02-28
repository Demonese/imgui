// Dear ImGui: standalone example application for DirectX 11
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

#include <thread>
#include <mutex>
#include <functional>

#include "imgui.h"
#include "imgui_impl_win32ex.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>

// Win32 Window datas and functions

static HWND         g_hWnd = NULL;
static WNDCLASSEX   g_tWndCls;

static LRESULT WINAPI WindowProcess(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool CreateWindowWin32();
void DestroyWindowWin32();
void MessageLoopWin32();

// Direct3D datas and functions

static ID3D11Device*            g_pd3dDevice            = NULL;
static ID3D11DeviceContext*     g_pd3dDeviceContext     = NULL;
static IDXGISwapChain*          g_pSwapChain            = NULL;
static ID3D11RenderTargetView*  g_mainRenderTargetView  = NULL;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
bool CheckDeviceD3DState();

// Working thread data

constexpr UINT   MSG_SWITCH_DISPLAY_MODE = WM_USER + 64;
constexpr WPARAM MSG_SWITCH_DISPLAY_MODE_WINDOWED   = 1;
constexpr WPARAM MSG_SWITCH_DISPLAY_MODE_FULLSCREEN = 2;

static bool                 g_bExit                 = false;
static bool                 g_bWantUpdateWindowSize = false;
static UINT                 g_uWindowWidth          = 1;
static UINT                 g_uWindowHeight         = 1;
static std::recursive_mutex g_xWindowSizeLock;

void WorkingThread(HWND hwnd)
{
    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        g_bExit = true;
        return;
    }
    
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    
    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();
    
    // Setup Platform/Renderer backends
    ImGui_ImplWin32Ex_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    
    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != NULL);
    
    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    
    // Some function
    auto HandleDeviceD3DLose = [&]() -> void
    {
        HRESULT reason = g_pd3dDevice->GetDeviceRemovedReason();
        #if defined(_DEBUG)
            wchar_t outString[100];
            size_t size = 100;
            swprintf_s(outString, size, L"Device removed! DXGI_ERROR code: 0x%X\n", reason);
            OutputDebugStringW(outString);
        #endif
        
        ImGui_ImplDX11_Shutdown();
        CleanupDeviceD3D();
        
        if (!CreateDeviceD3D(hwnd))
        {
            CleanupDeviceD3D();
            g_bExit = true;
            return;
        }
        ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    };
    auto HandleRender = [&](const std::function<void()>& render) -> void
    {
        if (g_xWindowSizeLock.try_lock())
        {
            const auto want_resize = g_bWantUpdateWindowSize;
            g_bWantUpdateWindowSize = false;
            const auto width = (g_uWindowWidth > 0) ? g_uWindowWidth : 1;
            const auto height = (g_uWindowHeight > 0) ? g_uWindowHeight : 1;
            g_xWindowSizeLock.unlock();
            if (want_resize)
            {
                CleanupRenderTarget();
                HRESULT hr = g_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
                if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
                {
                    HandleDeviceD3DLose();
                }
                else
                {
                    CreateRenderTarget();
                }
            }
        }
        if (CheckDeviceD3DState())
        {
            ID3D11RenderTargetView* rtvs[1] = { g_mainRenderTargetView };
            g_pd3dDeviceContext->OMSetRenderTargets(1, rtvs, NULL);
            g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, (float*)&clear_color);
            
            render();
            
            HRESULT hr = g_pSwapChain->Present(1, 0);
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            {
                HandleDeviceD3DLose();
            }
        }
        else
        {
            HandleDeviceD3DLose();
        }
    };
    auto HandleFullscreen = [&]() -> void
    {
        // only trigger on alt+enter down
        static bool _alt_enter = false;
        static bool _fullscreen = false;
        const bool alt_enter_key = (GetKeyState(VK_MENU) & 0x8000) && (GetKeyState(VK_RETURN) & 0x8000);
        if (!_alt_enter && alt_enter_key)
        {
            _alt_enter = true;
            _fullscreen = !_fullscreen;
            PostMessageW(hwnd, MSG_SWITCH_DISPLAY_MODE,
                _fullscreen ? MSG_SWITCH_DISPLAY_MODE_FULLSCREEN : MSG_SWITCH_DISPLAY_MODE_WINDOWED, 0);
        }
        else if(!alt_enter_key)
        {
            _alt_enter = false;
        }
    };
    
    // Update and Render loop
    while(!g_bExit)
    {
        HandleFullscreen();
        
        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32Ex_NewFrame();
        ImGui::NewFrame();
        
        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);
        
        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
        {
            static float f = 0.0f;
            static int counter = 0;
            
            ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.
            
            ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
            ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
            ImGui::Checkbox("Another Window", &show_another_window);
            if (ImGui::Button("Close Application")) g_bExit = true; // Escape working loop and tell GUI thread exit message loop
            
            ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color
            
            if (ImGui::Button("Button")) counter++;                 // Buttons return true when clicked (most widgets return true when edited/activated)
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);
            
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();
        }
        
        // 3. Show another simple window.
        if (show_another_window)
        {
            ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                show_another_window = false;
            ImGui::End();
        }
        
        // End the Dear ImGui frame
        ImGui::EndFrame();
        ImGui::Render();
        
        // Rendering
        HandleRender([&]() -> void {
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        });
    }
    
    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32Ex_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
}

// 'Main' and GUI thread

int main(int, char**)
{
    CreateWindowWin32();
    
    std::thread workth(&WorkingThread, g_hWnd);
    MessageLoopWin32();
    g_bExit = true;
    workth.join();
    
    DestroyWindowWin32();
    return 0;
}

// Win32 Window functions

bool CreateWindowWin32()
{
    // Register window
    //ImGui_ImplWin32_EnableDpiAwareness();
    ZeroMemory(&g_tWndCls, sizeof(WNDCLASSEX));
    {
        g_tWndCls.cbSize = sizeof(WNDCLASSEX);
        g_tWndCls.style = CS_CLASSDC;
        g_tWndCls.lpfnWndProc = &WindowProcess;
        g_tWndCls.hInstance = GetModuleHandleW(NULL);
        g_tWndCls.lpszClassName = L"ImGui Example";
    }
    RegisterClassExW(&g_tWndCls);
    
    // Create application window
    g_hWnd = CreateWindowExW(
        0, g_tWndCls.lpszClassName, L"Dear ImGui DirectX11 Example", WS_OVERLAPPEDWINDOW,
        100, 100, 1280, 800,
        NULL, NULL, g_tWndCls.hInstance, NULL);
    
    // Show the window
    ShowWindow(g_hWnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hWnd);
    return true;
}
void DestroyWindowWin32()
{
    // Clean
    if (g_hWnd) { DestroyWindow(g_hWnd); g_hWnd = NULL; }
    UnregisterClassW(g_tWndCls.lpszClassName, g_tWndCls.hInstance);
}
void MessageLoopWin32()
{
    // Main loop
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (!g_bExit && msg.message != WM_QUIT)
    {
        // Poll and handle messages (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        if (PeekMessageW(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

// Direct3D functions

#define SAFE_RELEASE_COM(x) do { if(x != nullptr) { x->Release(); x = nullptr; } } while(false)
bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 0;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    
    UINT createDeviceFlags = 0;
    #ifdef _DEBUG
        createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    #endif
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return false;
    
    // Get factory from device
    IDXGIDevice*    pDXGIDevice     = nullptr;
    IDXGIAdapter*   pDXGIAdapter    = nullptr;
    IDXGIFactory*   pDXGIFactory    = nullptr;
    if (g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&pDXGIDevice)) == S_OK)
        if (pDXGIDevice->GetParent(IID_PPV_ARGS(&pDXGIAdapter)) == S_OK)
            if (pDXGIAdapter->GetParent(IID_PPV_ARGS(&pDXGIFactory)) == S_OK)
            {
                // we didn't need default alt+enter fullscreen switch (exclusive fullscreen mode will cause crash)
                // instead, using fullscreen window mode
                pDXGIFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
            }
    SAFE_RELEASE_COM(pDXGIDevice);
    SAFE_RELEASE_COM(pDXGIAdapter);
    SAFE_RELEASE_COM(pDXGIFactory);
    
    CreateRenderTarget();
    return true;
}
void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    SAFE_RELEASE_COM(g_pSwapChain);
    SAFE_RELEASE_COM(g_pd3dDeviceContext);
    SAFE_RELEASE_COM(g_pd3dDevice);
}
void CreateRenderTarget()
{
    HRESULT hr = S_OK;
    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (hr == S_OK)
    {
        hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
        if (hr != S_OK)
        {
            g_mainRenderTargetView = nullptr;
        }
        SAFE_RELEASE_COM(pBackBuffer);
    }
}
void CleanupRenderTarget()
{
    if (g_pd3dDeviceContext)
    {
        ID3D11RenderTargetView* rtvs[1] = { nullptr };
        g_pd3dDeviceContext->OMSetRenderTargets(1, rtvs, nullptr);
    }
    SAFE_RELEASE_COM(g_mainRenderTargetView);
}
bool CheckDeviceD3DState()
{
    if (nullptr == g_pd3dDevice || nullptr == g_pd3dDeviceContext || nullptr == g_pSwapChain)
        return false;
    if (S_OK != g_pd3dDevice->GetDeviceRemovedReason())
        return false;
    if (nullptr == g_mainRenderTargetView)
        return false;
    else
        return true;
}

// Win32 message handler

// Forward declare message handler from imgui_impl_win32ex.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32Ex_WndProcHandler(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WindowProcess(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32Ex_WndProcHandler(hWnd, msg, wParam, lParam))
        return 1; // do not continue to process message
    
    switch (msg)
    {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            g_xWindowSizeLock.lock();
            g_bWantUpdateWindowSize = true;
            g_uWindowWidth = (UINT)LOWORD(lParam);
            g_uWindowHeight = (UINT)HIWORD(lParam);
            g_xWindowSizeLock.unlock();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0; // Disable ALT application menu (becase imgui also using ALT key)
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case MSG_SWITCH_DISPLAY_MODE:
        switch(wParam)
        {
        case MSG_SWITCH_DISPLAY_MODE_WINDOWED:
            {
                SetWindowLongPtrW(hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
                SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOMOVE);
                ShowWindow(hWnd, SW_MAXIMIZE);
            }
            break;
        case MSG_SWITCH_DISPLAY_MODE_FULLSCREEN:
            {
                HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY);
                MONITORINFO info = {};
                info.cbSize = sizeof(info);
                if (FALSE != GetMonitorInfoW(monitor, &info))
                {
                    SetWindowLongPtrW(hWnd, GWL_STYLE, WS_POPUP);
                    SetWindowPos(hWnd, HWND_TOPMOST,
                        info.rcMonitor.left,
                        info.rcMonitor.top,
                        info.rcMonitor.right - info.rcMonitor.left,
                        info.rcMonitor.bottom - info.rcMonitor.top,
                        SWP_FRAMECHANGED | SWP_SHOWWINDOW);
                }
            }
            break;
        }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
