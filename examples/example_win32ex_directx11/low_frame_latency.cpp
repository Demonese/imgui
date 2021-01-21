// Dear ImGui: standalone example application for DirectX 11
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <vector>
#include <unordered_map>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "imgui_freetype.h"
#include <Windows.h>
#include <wrl.h>
#include <dxgi1_6.h>
#include <d3d11_4.h>

// Low Frame Latency Swap Chain Techique:
//  - 1. Traditional:
//     - Screen:      exclusive fullscreen (SetFullscreenState, ResizeTarget, ResizeBuffers)
//     - Swap Effect: Discard (DXGI_SWAP_EFFECT_DISCARD)
//     - VSync:       Disable (sync interval = 0)
//  - 2. Modern:
//     - Screen:      fullscreen frameless window (SetWindowLongPtr, SetWindowPos, ResizeBuffers)
//     - Swap Effect: Flip Discard (DXGI_SWAP_EFFECT_FLIP_DISCARD)
//     - VBlank:      Sync by waitable object (create swap chain with flag DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)
//     - Tearing:     Enable (create swap chain with flag DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)
//     - VSync:       Disable (sync interval = 0, present with flag DXGI_PRESENT_ALLOW_TEARING)

constexpr UINT   MSG_SWITCH_DISPLAY_MODE = WM_USER + 64;
constexpr WPARAM MSG_SWITCH_DISPLAY_MODE_WINDOWED   = 1;
constexpr WPARAM MSG_SWITCH_DISPLAY_MODE_FULLSCREEN = 2;

static bool                 g_bExit                 = false;
static bool                 g_bWantUpdateWindowSize = false;
static UINT                 g_uWindowWidth          = 1;
static UINT                 g_uWindowHeight         = 1;
static std::recursive_mutex g_xWindowSizeLock;

#include <imm.h>
#pragma comment(lib, "imm32.lib")
class InputMethodHelper
{
private:
    typedef HIMC (WINAPI *PFN_ImmGetContext)(HWND);
    typedef BOOL (WINAPI *PFN_ImmReleaseContext)(HWND, HIMC);
    typedef BOOL (WINAPI *PFN_ImmSetOpenStatus)(HIMC, BOOL);
    typedef BOOL (WINAPI *PFN_ImmGetOpenStatus)(HIMC);
    typedef BOOL (WINAPI *PFN_ImmSetConversionStatus)(HIMC, DWORD, DWORD);
    typedef BOOL (WINAPI *PFN_ImmNotifyIME)(HIMC, DWORD, DWORD, DWORD);
    static constexpr int IME_CMODE = IME_CMODE_FIXED | IME_CMODE_NOCONVERSION;
private:
    HMODULE                    Imm32                  = NULL;
    PFN_ImmGetContext          ImmGetContext          = NULL;
    PFN_ImmReleaseContext      ImmReleaseContext      = NULL;
    PFN_ImmSetOpenStatus       ImmSetOpenStatus       = NULL;
    PFN_ImmGetOpenStatus       ImmGetOpenStatus       = NULL;
    PFN_ImmSetConversionStatus ImmSetConversionStatus = NULL;
    PFN_ImmNotifyIME           ImmNotifyIME           = NULL;
    std::unordered_map<HWND, bool> enable_map;
public:
    static LRESULT MessageCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        //if (msg == WM_IME_NOTIFY && wParam == IMN_SETCONVERSIONMODE)
        {
            auto& cls = InputMethodHelper::get();
            auto it = cls.enable_map.find(hWnd);
            if (it != cls.enable_map.end())
            {
                if (!it->second)
                {
                    HIMC imc = cls.ImmGetContext(hWnd);
                    if (imc)
                    {
                        cls.ImmSetConversionStatus(imc, IME_CMODE, IME_SMODE_NONE);
                        cls.ImmSetOpenStatus(imc, FALSE);
                        cls.ImmReleaseContext(hWnd, imc);
                    }
                }
            }
        }
        return 0;
    };
public:
    bool enable(HWND window, bool be)
    {
        if (enable_map.find(window) != enable_map.end())
        {
            enable_map[window] = be;
        }
        else
        {
            enable_map.emplace(std::make_pair(window, be));
        }
        if (ImmGetContext && ImmReleaseContext && ImmSetOpenStatus)
        {
            HIMC imc = ImmGetContext(window);
            if (imc)
            {
                if (ImmSetConversionStatus && !be)
                {
                    ImmSetConversionStatus(imc, IME_CMODE, IME_SMODE_NONE);
                }
                BOOL b = ImmSetOpenStatus(imc, be ? TRUE : FALSE);
                ImmReleaseContext(window, imc);
                return b != FALSE;
            }
        }
        return false;
    };
    bool seteng(HWND window)
    {
        if (ImmGetContext && ImmReleaseContext && ImmSetConversionStatus)
        {
            HIMC imc = ImmGetContext(window);
            if (imc)
            {
                if (ImmSetOpenStatus)
                {
                    ImmSetOpenStatus(imc, FALSE);
                }
                BOOL b = ImmSetConversionStatus(imc, IME_CMODE, IME_SMODE_NONE);
                ImmReleaseContext(window, imc);
                return b != FALSE;
            }
        }
        return false;
    };
    bool status(HWND window)
    {
        auto it = enable_map.find(window);
        if (it != enable_map.end())
        {
            return it->second;
        }
        HIMC imc = ImmGetContext(window);
        if (imc)
        {
            BOOL b = ImmGetOpenStatus(imc);
            ImmReleaseContext(window, imc);
            return b != FALSE;
        }
        return false;
    };
public:
    InputMethodHelper()
    {
        Imm32 = ::LoadLibraryA("Imm32.dll");
        if (Imm32)
        {
            ImmGetContext           = (PFN_ImmGetContext         )::GetProcAddress(Imm32, "ImmGetContext"         );
            ImmReleaseContext       = (PFN_ImmReleaseContext     )::GetProcAddress(Imm32, "ImmReleaseContext"     );
            ImmSetOpenStatus        = (PFN_ImmSetOpenStatus      )::GetProcAddress(Imm32, "ImmSetOpenStatus"      );
            ImmGetOpenStatus        = (PFN_ImmGetOpenStatus      )::GetProcAddress(Imm32, "ImmGetOpenStatus"      );
            ImmSetConversionStatus  = (PFN_ImmSetConversionStatus)::GetProcAddress(Imm32, "ImmSetConversionStatus");
            ImmNotifyIME            = (PFN_ImmNotifyIME          )::GetProcAddress(Imm32, "ImmNotifyIME"          );
        }
    };
    ~InputMethodHelper()
    {
        Imm32                  = NULL;
        ImmGetContext          = NULL;
        ImmReleaseContext      = NULL;
        ImmSetOpenStatus       = NULL;
        ImmGetOpenStatus       = NULL;
        ImmSetConversionStatus = NULL;
        ImmNotifyIME           = NULL;
        if (Imm32)
            ::FreeLibrary(Imm32);
        Imm32 = NULL;
    };
public:
    static InputMethodHelper& get()
    {
        static InputMethodHelper instance;
        return instance;
    };
};

class Application
{
public:
    using WindowMessageCallback = LRESULT (*)(HWND, UINT, WPARAM, LPARAM);
private:
    std::atomic_bool _quit;
private:
    ATOM _windowClassAtom;
    DWORD _windowThreadID;
    std::vector<WindowMessageCallback> _windowMessageCallback;
    static LRESULT WINAPI WindowProcess(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto& app = Application::get();
        {
            bool flag = false;
            if (app._windowMessageCallback.size() > 0)
            {
                for (auto& callback : app._windowMessageCallback)
                {
                    if (callback(hWnd, msg, wParam, lParam))
                        flag = true;
                }
            }
            if (flag)
                return 1; // do not continue to process message
        }
        
        switch (msg)
        {
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU)
                return 0; // disable ALT menu (becase we will using ALT key)
            break;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        }
        
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }
public:
    WNDCLASSEXW windowClass;
    HWND windowHandle;
private:
    bool _support_d3d_feature_level_11_1;
    bool _support_swap_effect_flip;
    bool _support_frame_latency_waitable_object;
    bool _support_allow_tearing;
    UINT _swapchain_flags;
    HANDLE _frame_latency_waitable_object;
    #pragma region /* Microsoft shit */
    Microsoft::WRL::ComPtr<IDXGIFactory1> _dxgiFactory1;
    Microsoft::WRL::ComPtr<IDXGIFactory2> _dxgiFactory2;
    Microsoft::WRL::ComPtr<IDXGIFactory3> _dxgiFactory3;
    Microsoft::WRL::ComPtr<IDXGIFactory4> _dxgiFactory4;
    Microsoft::WRL::ComPtr<IDXGIFactory5> _dxgiFactory5;
    Microsoft::WRL::ComPtr<IDXGIFactory6> _dxgiFactory6;
    Microsoft::WRL::ComPtr<IDXGIFactory7> _dxgiFactory7;
    Microsoft::WRL::ComPtr<IDXGISwapChain> _dxgiSwapChain;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> _dxgiSwapChain1;
    Microsoft::WRL::ComPtr<IDXGISwapChain2> _dxgiSwapChain2;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> _dxgiSwapChain3;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> _dxgiSwapChain4;
    Microsoft::WRL::ComPtr<ID3D11Device> _d3d11Device;
    Microsoft::WRL::ComPtr<ID3D11Device1> _d3d11Device1;
    Microsoft::WRL::ComPtr<ID3D11Device2> _d3d11Device2;
    Microsoft::WRL::ComPtr<ID3D11Device3> _d3d11Device3;
    Microsoft::WRL::ComPtr<ID3D11Device4> _d3d11Device4;
    Microsoft::WRL::ComPtr<ID3D11Device5> _d3d11Device5;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> _d3d11DeviceContext;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> _d3d11DeviceContext1;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext2> _d3d11DeviceContext2;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext3> _d3d11DeviceContext3;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> _d3d11DeviceContext4;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> _d3d11RenderTarget;
    #pragma endregion
    inline void _resetDXObject()
    {
        dxgiFactory = NULL;
        dxgiSwapChain = NULL;
        d3d11Device = NULL;
        d3d11DeviceContext = NULL;
        d3d11RenderTarget = NULL;
        if (_frame_latency_waitable_object)
            ::CloseHandle(_frame_latency_waitable_object);
        _frame_latency_waitable_object = NULL;
        
        _d3d11RenderTarget.Reset();
        
        _dxgiSwapChain.Reset();
        _dxgiSwapChain1.Reset();
        _dxgiSwapChain2.Reset();
        _dxgiSwapChain3.Reset();
        _dxgiSwapChain4.Reset();
        
        _d3d11DeviceContext.Reset();
        _d3d11DeviceContext1.Reset();
        _d3d11DeviceContext2.Reset();
        _d3d11DeviceContext3.Reset();
        _d3d11DeviceContext4.Reset();
        
        _d3d11Device.Reset();
        _d3d11Device1.Reset();
        _d3d11Device2.Reset();
        _d3d11Device3.Reset();
        _d3d11Device4.Reset();
        _d3d11Device5.Reset();
        
        _dxgiFactory1.Reset();
        _dxgiFactory2.Reset();
        _dxgiFactory3.Reset();
        _dxgiFactory4.Reset();
        _dxgiFactory5.Reset();
        _dxgiFactory6.Reset();
        _dxgiFactory7.Reset();
    };
public:
    IDXGIFactory1* dxgiFactory;
    IDXGISwapChain* dxgiSwapChain;
    ID3D11Device* d3d11Device;
    ID3D11DeviceContext* d3d11DeviceContext;
    ID3D11RenderTargetView* d3d11RenderTarget;
public: // basic
    void clear()
    {
        _quit.store(false);
        _windowClassAtom = 0;
        _windowThreadID = 0;
        _windowMessageCallback.clear();
        
        ZeroMemory(&windowClass, sizeof(windowClass));
        windowClass.cbSize = sizeof(WNDCLASSEXW);
        windowHandle = NULL;
        
        _resetDXObject();
        _support_d3d_feature_level_11_1 = false;
        _support_swap_effect_flip = false;
        _support_frame_latency_waitable_object = false;
        _support_allow_tearing = false;
        _swapchain_flags = 0;
    };
public: // Window
    bool createWindow(int width, int height, const wchar_t* title)
    {
        windowClass.style = CS_CLASSDC;
        windowClass.lpfnWndProc = &WindowProcess;
        windowClass.hInstance = ::GetModuleHandleW(NULL);
        windowClass.lpszClassName = L"Dear-ImGui-Example";
        _windowClassAtom = ::RegisterClassExW(&windowClass);
        if (_windowClassAtom == 0)
            return false;
        
        // notice: this function MUST always suceed, otherwise your Windows system have problems.
        RECT wrect = { 0, 0, width, height };
        if (0 == ::AdjustWindowRectEx(&wrect, WS_OVERLAPPEDWINDOW, FALSE, 0))
            return false;
        
        windowHandle = ::CreateWindowExW(
            0, windowClass.lpszClassName, title, WS_OVERLAPPEDWINDOW,
            wrect.left, wrect.top, wrect.right - wrect.left, wrect.bottom - wrect.top,
            NULL, NULL, windowClass.hInstance, NULL);
        if (NULL == windowHandle)
            return false;
        _windowThreadID = ::GetCurrentThreadId();
        
        ::SetLastError(0);
        if (0 == ::SetWindowLongPtrW(windowHandle, GWLP_USERDATA, (LONG_PTR)this) && 0 != GetLastError())
            return false;
        
        // Show the window
        setWindowCentered();
        ::UpdateWindow(windowHandle);
        
        return true;
    };
    void destroyWindow()
    {
        if (NULL != windowHandle)
            ::DestroyWindow(windowHandle);
        windowHandle = NULL;
        _windowThreadID = 0;
        if (0 != _windowClassAtom)
            ::UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
        _windowClassAtom = 0;
    };
    bool setWindowCentered()
    {
        if (NULL == windowHandle)
            return false;
        
        // notice: this function MUST always return a monitor, otherwise your Windows system have problems.
        HMONITOR monitor = ::MonitorFromWindow(windowHandle, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info = { sizeof(MONITORINFO), {}, {}, 0 };
        if (0 == ::GetMonitorInfoW(monitor, &info))
            return false;
        
        const auto mwidth = info.rcMonitor.right - info.rcMonitor.left;
        const auto mheight = info.rcMonitor.bottom - info.rcMonitor.top;
        
        RECT wrect = {};
        if (0 == ::GetWindowRect(windowHandle, &wrect))
            return false;
        
        const auto wwidth = wrect.right - wrect.left;
        const auto wheight = wrect.bottom - wrect.top;
        
        const auto left = info.rcMonitor.left + (mwidth / 2) - (wwidth / 2);
        const auto top = info.rcMonitor.top + (mheight / 2) - (wheight / 2);
        
        if (0 == ::SetWindowPos(windowHandle, HWND_TOP, left, top, wwidth, wheight, SWP_SHOWWINDOW))
            return false;
        
        return true;
    }
    bool updateWindowMessage(bool peek)
    {
        bool quit = false;
        MSG msg = {};
        BOOL ret = 0;
        if (peek)
        {
            ret = PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);
            while (ret)
            {
                if (msg.message == WM_QUIT)
                {
                    quit = true;
                    _quit.store(true);
                    break;
                }
                else
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                ret = PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);
            }
        }
        else
        {
            ret = GetMessageW(&msg, NULL, 0, 0);
            // if error, ret will be -1
            while (ret >= 0)
            {
                if (_quit.load() || ret == 0)
                {
                    quit = true;
                    _quit.store(true);
                    break;
                }
                else
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                ret = GetMessageW(&msg, NULL, 0, 0);
            }
        }
        return !quit;
    }
    void setWindowShouldClose(bool quit)
    {
        _quit.store(quit);
    };
    bool isWindowShouldClose()
    {
        return _quit.load();
    }
    void addWindowMessageCallback(WindowMessageCallback callback)
    {
        if (callback == NULL)
            return;
        removeWindowMessageCallback(callback);
        _windowMessageCallback.push_back(callback);
    };
    void removeWindowMessageCallback(WindowMessageCallback callback)
    {
        if (callback == NULL)
            return;
        for (auto it = _windowMessageCallback.begin(); it != _windowMessageCallback.end();)
        {
            if (*it == callback)
                it = _windowMessageCallback.erase(it);
            else
                it++;
        }
    }
public: // Graphic
    bool createGraphic()
    {
        if (windowHandle == NULL)
            return false;
        
        HRESULT hr = 0;
        
        hr = CreateDXGIFactory1(IID_PPV_ARGS(_dxgiFactory1.GetAddressOf()));
        if (hr != S_OK)
            return false;
        dxgiFactory = _dxgiFactory1.Get();
        _dxgiFactory1.As(&_dxgiFactory2);
        _dxgiFactory1.As(&_dxgiFactory3);
        _dxgiFactory1.As(&_dxgiFactory4);
        _dxgiFactory1.As(&_dxgiFactory5);
        _dxgiFactory1.As(&_dxgiFactory6);
        _dxgiFactory1.As(&_dxgiFactory7);
        if (_dxgiFactory5)
        {
            BOOL support = FALSE;
            hr = _dxgiFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &support, sizeof(BOOL));
            if (S_OK == hr && support != FALSE)
                _support_allow_tearing = true;
        }
        
        Microsoft::WRL::ComPtr<IDXGIAdapter1> _adapter;
        hr = _dxgiFactory1->EnumAdapters1(0, _adapter.GetAddressOf());
        if (hr != S_OK)
            return false; // no adapter ???
        
        UINT d3d11flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; // it must support BGRA
        #ifndef NDEBUG
            d3d11flags |= D3D11_CREATE_DEVICE_DEBUG;
        #endif
        D3D_FEATURE_LEVEL featureLevel;
        const D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };
        auto D3D11CreateDeviceA = [&]() -> bool {
            hr = D3D11CreateDevice(
                _adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, NULL,
                d3d11flags, featureLevels, 4, D3D11_SDK_VERSION,
                _d3d11Device.GetAddressOf(), &featureLevel, _d3d11DeviceContext.GetAddressOf());
            return S_OK == hr;
        };
        auto D3D11CreateDeviceB = [&]() -> bool {
            hr = D3D11CreateDevice(
                _adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, NULL,
                d3d11flags, featureLevels + 1, 3, D3D11_SDK_VERSION,
                _d3d11Device.GetAddressOf(), &featureLevel, _d3d11DeviceContext.GetAddressOf());
            return S_OK == hr;
        };
        if (!D3D11CreateDeviceA())
            if (!D3D11CreateDeviceB())
                return false;
        d3d11Device = _d3d11Device.Get();
        _d3d11Device.As(&_d3d11Device1);
        _d3d11Device.As(&_d3d11Device2);
        _d3d11Device.As(&_d3d11Device3);
        _d3d11Device.As(&_d3d11Device4);
        _d3d11Device.As(&_d3d11Device5);
        d3d11DeviceContext = _d3d11DeviceContext.Get();
        _d3d11DeviceContext.As(&_d3d11DeviceContext1);
        _d3d11DeviceContext.As(&_d3d11DeviceContext2);
        _d3d11DeviceContext.As(&_d3d11DeviceContext3);
        _d3d11DeviceContext.As(&_d3d11DeviceContext4);
        _support_d3d_feature_level_11_1 = (featureLevel == D3D_FEATURE_LEVEL_11_1);
        
        if (_dxgiFactory2 && _d3d11Device1 && _d3d11DeviceContext1 && featureLevel == D3D_FEATURE_LEVEL_11_1)
        {
            DXGI_SWAP_CHAIN_DESC1 desc = {};
            desc.Width = 1;
            desc.Height = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // BGRA
            desc.Stereo = FALSE;
            desc.SampleDesc = { 1, 0 };
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2;
            desc.Scaling = DXGI_SCALING_NONE; // DO NOT confused with DXGI_MODE_SCALING
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE; // we didn't need it
            desc.Flags = 0
                | DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
                | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
                | (_support_allow_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
            
            hr = _dxgiFactory2->CreateSwapChainForHwnd(_d3d11Device.Get(), windowHandle, &desc, NULL, NULL, _dxgiSwapChain1.GetAddressOf());
            if (S_OK != hr)
            {
                desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                hr = _dxgiFactory2->CreateSwapChainForHwnd(_d3d11Device.Get(), windowHandle, &desc, NULL, NULL, _dxgiSwapChain1.GetAddressOf());
            }
            if (S_OK != hr)
            {
                desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                desc.Flags ^= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
                hr = _dxgiFactory2->CreateSwapChainForHwnd(_d3d11Device.Get(), windowHandle, &desc, NULL, NULL, _dxgiSwapChain1.GetAddressOf());
            }
            if (S_OK != hr)
            {
                desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                hr = _dxgiFactory2->CreateSwapChainForHwnd(_d3d11Device.Get(), windowHandle, &desc, NULL, NULL, _dxgiSwapChain1.GetAddressOf());
            }
            if (S_OK == hr)
            {
                _support_swap_effect_flip = true;
                _support_frame_latency_waitable_object = (0 != (desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT));
                
                _swapchain_flags = desc.Flags;
                _dxgiSwapChain1.As(&_dxgiSwapChain);
                _dxgiSwapChain1.As(&_dxgiSwapChain2);
                _dxgiSwapChain1.As(&_dxgiSwapChain3);
                _dxgiSwapChain1.As(&_dxgiSwapChain4);
                
                if (_dxgiSwapChain2 && _support_frame_latency_waitable_object)
                {
                    _dxgiSwapChain2->SetMaximumFrameLatency(1);
                    _frame_latency_waitable_object = _dxgiSwapChain2->GetFrameLatencyWaitableObject();
                }
                else
                {
                    Microsoft::WRL::ComPtr<IDXGIDevice1> _dxgiDev;
                    if (S_OK == _d3d11Device.As(&_dxgiDev))
                        _dxgiDev->SetMaximumFrameLatency(1);
                }
            }
            // else...
        }
        if (!_dxgiSwapChain) // fallback
        {
            UINT swapchainflag = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
            
            DXGI_SWAP_CHAIN_DESC desc = {};
            desc.BufferDesc.Width = 1;
            desc.BufferDesc.Height = 1;
            desc.BufferDesc.RefreshRate = { 0, 1 };
            desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // BGRA
            desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
            desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
            desc.SampleDesc = { 1, 0 };
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2;
            desc.OutputWindow = windowHandle;
            desc.Windowed = TRUE;
            desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            desc.Flags = swapchainflag;
            
            hr = _dxgiFactory1->CreateSwapChain(_d3d11Device.Get(), &desc, _dxgiSwapChain.GetAddressOf());
            if (S_OK != hr)
                return false;
            
            _swapchain_flags = desc.Flags;
            Microsoft::WRL::ComPtr<IDXGIDevice1> _dxgiDev;
            if (S_OK == _d3d11Device.As(&_dxgiDev))
                _dxgiDev->SetMaximumFrameLatency(1);
        }
        dxgiSwapChain = _dxgiSwapChain.Get();
        
        // we didn't need default alt+enter fullscreen switch (exclusive fullscreen mode will cause crash)
        // instead, using fullscreen window mode
        _dxgiFactory1->MakeWindowAssociation(windowHandle, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
        
        return true;
    };
    void destroyGraphic()
    {
        if (_d3d11DeviceContext)
        {
            _d3d11DeviceContext->ClearState();
        }
        _resetDXObject();
    };
    bool resizeSwapChain(int width, int height)
    {
        destroyRenderTarget();
        
        if (!_dxgiSwapChain)
            return false;
        
        HRESULT hr = S_OK;
        
        const UINT w = (width >= 1) ? width : 1;
        const UINT h = (height >= 1) ? height : 1;
        hr = _dxgiSwapChain->ResizeBuffers(2, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, _swapchain_flags);
        if (hr != S_OK)
            return false;
        
        if (!createRenderTarget())
            return false;
        
        return true;
    };
    void waitSwapChain()
    {
        if (_frame_latency_waitable_object)
        {
            WaitForSingleObjectEx(_frame_latency_waitable_object, 1000, TRUE);
        }
    }
    bool createRenderTarget()
    {
        if (!_dxgiSwapChain)
            return false;
        if (!_d3d11Device)
            return false;
        if (_d3d11RenderTarget)
            destroyRenderTarget();
        
        HRESULT hr = 0;
        
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer; 
        hr = _dxgiSwapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
        if (hr != S_OK)
            return false;
        
        hr = _d3d11Device->CreateRenderTargetView(backBuffer.Get(), NULL, _d3d11RenderTarget.GetAddressOf());
        if (hr != S_OK)
            return false;
        
        d3d11RenderTarget = _d3d11RenderTarget.Get();
        
        return true;
    };
    void destroyRenderTarget()
    {
        if (_d3d11DeviceContext)
        {
            ID3D11RenderTargetView* rtvs[1] = { NULL };
            _d3d11DeviceContext->OMSetRenderTargets(1, rtvs, NULL);
        }
        _d3d11RenderTarget.Reset();
        d3d11RenderTarget = NULL;
    };
    bool bindRenderTarget()
    {
        if (!_d3d11DeviceContext)
            return false;
        if (!_d3d11RenderTarget)
            return false;
        
        ID3D11RenderTargetView* rtvs[1] = { _d3d11RenderTarget.Get() };
        _d3d11DeviceContext->OMSetRenderTargets(1, rtvs, NULL);
        const FLOAT color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        _d3d11DeviceContext->ClearRenderTargetView(_d3d11RenderTarget.Get(), color);
        
        return true;
    };
    bool presentBackBuffer(bool vsync)
    {
        if (!_dxgiSwapChain)
            return false;
        
        HRESULT hr = S_OK;
        
        UINT flag = 0;
        if (!vsync && _support_allow_tearing)
            flag |= DXGI_PRESENT_ALLOW_TEARING;
        hr = _dxgiSwapChain->Present(vsync ? 1 : 0, flag);
        if (hr != S_OK)
            return false;
        
        return true;
    }
public:
    Application()
    {
        clear();
    };
    ~Application()
    {
        destroyGraphic();
        destroyWindow();
    };
public:
    static Application& get()
    {
        static Application instance;
        return instance;
    };
};

void WorkingThread()
{
    auto& app = Application::get();
    auto& imm = InputMethodHelper::get();
    
    // Initialize Direct3D
    if (!app.createGraphic())
    {
        app.setWindowShouldClose(true);
        return;
    }
    app.resizeSwapChain(1280, 720);
    
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
    ImGui_ImplWin32_Init(app.windowHandle);
    ImGui_ImplDX11_Init(app.d3d11Device, app.d3d11DeviceContext);
    
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
    io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyh.ttc", 16.0f * ImGui_ImplWin32_GetDpiScaleForHwnd(app.windowHandle), NULL, io.Fonts->GetGlyphRangesChineseFull());
    ImGuiFreeType::BuildFontAtlas(io.Fonts);
    
    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    
    // Some function
    auto HandleRender = [&](const std::function<void()>& render) -> void
    {
        bool resize_flag = false;
        if (g_xWindowSizeLock.try_lock())
        {
            const auto want_resize = g_bWantUpdateWindowSize;
            g_bWantUpdateWindowSize = false;
            const auto width = (g_uWindowWidth > 0) ? g_uWindowWidth : 1;
            const auto height = (g_uWindowHeight > 0) ? g_uWindowHeight : 1;
            g_xWindowSizeLock.unlock();
            if (want_resize)
            {
                app.resizeSwapChain(width, height);
                resize_flag = true;
            }
        }
        if (!resize_flag)
        {
            app.bindRenderTarget();
            render();
            app.presentBackBuffer(true);
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
            PostMessageW(app.windowHandle, MSG_SWITCH_DISPLAY_MODE,
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
        app.waitSwapChain();
        
        HandleFullscreen();
        app.updateWindowMessage(true);
        g_bExit = app.isWindowShouldClose();
        
        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
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
            if (ImGui::Button("Close Application"))
                { g_bExit = true; app.setWindowShouldClose(true);}  // Escape working loop and tell GUI thread exit message loop
            
            ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color
            
            if (ImGui::Button("Button")) counter++;                 // Buttons return true when clicked (most widgets return true when edited/activated)
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);
            
            bool ime_status = imm.status(app.windowHandle);
            ImGui::Text("IME %s", ime_status ? "Enable" : "Disable");
            ImGui::SameLine();
            if (ImGui::Button("Disable IME")) imm.enable(app.windowHandle, false);
            ImGui::SameLine();
            if (ImGui::Button("Enable IME")) imm.enable(app.windowHandle, true);
            ImGui::SameLine();
            if (ImGui::Button("Set IME EN")) imm.seteng(app.windowHandle);
            
            static HIMC _oldhimc = NULL;
            
            if (ImGui::Button("Associate NULL")) _oldhimc = ::ImmAssociateContext(app.windowHandle, _oldhimc);
            if (ImGui::Button("Associate Back")) _oldhimc = ::ImmAssociateContext(app.windowHandle, _oldhimc);
            
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
    
    // Cleanup backend
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    
    // Cleanup imgui
    ImGui::DestroyContext();
    
    // Cleanup d3d
    app.destroyGraphic();
}
LRESULT WorkingThreadMessageCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (hWnd == Application::get().windowHandle)
    {
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
            break;
        case MSG_SWITCH_DISPLAY_MODE:
            {
                static RECT window_rect = {};
                switch(wParam)
                {
                case MSG_SWITCH_DISPLAY_MODE_WINDOWED:
                    {
                        SetWindowLongPtrW(hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
                        SetWindowPos(hWnd, HWND_NOTOPMOST,
                            window_rect.left,
                            window_rect.top,
                            window_rect.right - window_rect.left,
                            window_rect.bottom - window_rect.top,
                            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
                    }
                    break;
                case MSG_SWITCH_DISPLAY_MODE_FULLSCREEN:
                    {
                        GetWindowRect(hWnd, &window_rect);
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
            }
            break;
        }
    }
    return 0;
}
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int main(int, char**)
{
    ImGui_ImplWin32_EnableDpiAwareness();
    auto& app = Application::get();
    app.addWindowMessageCallback(&WorkingThreadMessageCallback);
    app.addWindowMessageCallback(&ImGui_ImplWin32_WndProcHandler);
    app.addWindowMessageCallback(&InputMethodHelper::MessageCallback);
    if (app.createWindow(1280, 720, L"Dear ImGui Win32EX Direct3D11 Example"))
    {
        WorkingThread();
    }
    app.destroyWindow();
    app.removeWindowMessageCallback(&InputMethodHelper::MessageCallback);
    app.removeWindowMessageCallback(&ImGui_ImplWin32_WndProcHandler);
    app.removeWindowMessageCallback(&WorkingThreadMessageCallback);
    return 0;
}
