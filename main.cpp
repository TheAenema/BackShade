/*																		*
*       BackshadeAMD6 - Reshade Powered Desktop Background Engine	    *
*						 Developed in 2022-2023			 				*
*																		*/

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <iostream>
#include <shellapi.h>

#include <d3d11.h>
#pragma comment (lib, "d3d11.lib")

#include "resource.h"

using namespace std;

// Helper Macros
#define stdlog(fmt,...)     printf("[BackShade] " fmt "\n",__VA_ARGS__);

// Macros
#define RENDER_INTERVAL     8
#define CPU_DELAY_MS        8
#define WM_TRAYICON         (WM_USER + 44)
#define HIDE_CONSOLE        TRUE

// Statics
static HWND hwnd                    = nullptr;
static HWND wallpaperHwnd           = nullptr;
static HWND iconTrayHwnd            = nullptr;
static HANDLE appThread             = nullptr;
static HANDLE rendererThread        = nullptr;
static NOTIFYICONDATA notifyData    = { 0 };
static BOOL appRunning              = false;
static BOOL backgroundApplied       = false;
static BOOL allowRendering          = false;

// Misc
HCURSOR hCursor;
FLOAT defaultColor[] = { 0.01, 0.01, 0.01, 0.0 };

// DirectX Objects
ID3D11Device* device                            = nullptr;
ID3D11DeviceContext* deviceContext              = nullptr;
IDXGISwapChain* swapChain                       = nullptr;
ID3D11RenderTargetView* renderTargetView        = nullptr;

// Forward Declarations
VOID ClearBackground();
BOOL ApplyBackground();
VOID RenderFrame();

// Helper Functions
HICON GetIcon(int IconID)
{
    // Load the image from resources
    HICON hIcon = (HICON)LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IconID), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    
    // Validate Icon
    if (hIcon == NULL) 
    { 
        stdlog("[!] -> Error : Icon Cannot be Loaded!");
        return LoadIcon(NULL, IDI_APPLICATION);
    }

    return hIcon;
}

// Main Events
LRESULT CALLBACK WndProc(HWND winHandle, UINT msg, WPARAM wParam, LPARAM lParam) 
{
    switch (msg) 
    {
    case WM_INITDIALOG:
        break;
    case WM_DESTROY:
        if (!backgroundApplied) ClearBackground();
        PostQuitMessage(0);
        if (!backgroundApplied) exit(0);
        break;
    case WM_SETCURSOR:
        hCursor = LoadCursor(NULL, IDC_ARROW);
        SetCursor(hCursor);
        break;
    case WM_SIZE:
        if (deviceContext && swapChain)
        {
            RECT rc;
            GetWindowRect(hwnd, &rc);
            D3D11_VIEWPORT viewport;
            viewport.MinDepth = 0;
            viewport.MaxDepth = 1;
            viewport.TopLeftX = 0;
            viewport.TopLeftY = 0;
            viewport.Width = static_cast<float>(rc.right - rc.left);
            viewport.Height = static_cast<float>(rc.bottom - rc.top);
            deviceContext->RSSetViewports(1u, &viewport);
            deviceContext->ClearRenderTargetView(renderTargetView, defaultColor);
            swapChain->Present(1, 0);
        }
        break;
    default:
        return DefWindowProc(winHandle, msg, wParam, lParam);
    }
    return 0;
}
LRESULT CALLBACK TrayProc(HWND winHandle, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_TRAYICON:
        switch (lParam) {
        case WM_RBUTTONUP:
            POINT cursor;
            GetCursorPos(&cursor);
            HMENU hMenu = CreatePopupMenu();
            if (!backgroundApplied)
            {
                AppendMenu(hMenu, MF_STRING, 1, L"Apply As Background");
                AppendMenu(hMenu, MF_STRING, 2, L"Quit");
            }
            else
            {
                AppendMenu(hMenu, MF_STRING, 2, L"Restore And Quit");
            }
            SetForegroundWindow(winHandle);
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, cursor.x, cursor.y, 0, winHandle, NULL);
            PostMessage(winHandle, WM_NULL, 0, 0);
            DestroyMenu(hMenu);
            break;
        }
        break;

    case WM_COMMAND:
        switch (wParam) {
        case 1:
            ApplyBackground();
            backgroundApplied = true;
            break;
        case 2:
            ClearBackground();
            appRunning = allowRendering = false;
            break;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(winHandle, uMsg, wParam, lParam);
        break;
    }
    return 0;
}
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    HWND p = FindWindowEx(hwnd, NULL, L"SHELLDLL_DefView", NULL);
    HWND* ret = (HWND*)lParam;

    if (p)
    {
        // Gets the WorkerW Window after the current one.
        *ret = FindWindowEx(NULL, hwnd, L"WorkerW", NULL);
    }
    return true;
}

// Main Functions
BOOL CreateIconTray()
{
    // Create window class
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = TrayProc;
    wc.hInstance = GetModuleHandle(0);
    wc.lpszClassName = L"BackShadeIconTray";
    RegisterClass(&wc);

    // Create hidden window
    iconTrayHwnd = CreateWindow(wc.lpszClassName, NULL, 0, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);

    // Initialize tray icon data
    notifyData.cbSize = sizeof(NOTIFYICONDATA);
    notifyData.hWnd = iconTrayHwnd;
    notifyData.uID = 1;
    notifyData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    notifyData.uCallbackMessage = WM_TRAYICON;
    notifyData.hIcon = GetIcon(TRAY_ICON);
    
    wcscpy(notifyData.szTip, L"BackShade™ Running");

    // Add tray icon
    Shell_NotifyIcon(NIM_ADD, &notifyData);

    return TRUE;
}
BOOL RegisterAndCreateWindow()
{
    // Create Window
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(0);
    wc.lpszClassName = L"BackShadeSurface";
    wc.hbrBackground = CreateSolidBrush(RGB(20, 20, 20));
    wc.hIcon = GetIcon(APP_ICON);
    RegisterClass(&wc);

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int winX = (screenWidth - screenWidth) / 2;
    int winY = (screenHeight - screenHeight) / 2;

    hwnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        nullptr,
        WS_POPUP | WS_VISIBLE,
        winX, winY, screenWidth, screenHeight,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr
    );

    if (hwnd == nullptr) {
        return false;
    }

    // Initialize Direct X
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.OutputWindow = hwnd;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.BufferDesc.Width = 0;
    swapChainDesc.BufferDesc.Height = 0;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0, };
    D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
        featureLevelArray, 3, D3D11_SDK_VERSION, &swapChainDesc, &swapChain, &device, &featureLevel, &deviceContext);

    ID3D11Texture2D* pBackBuffer;
    swapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) device->CreateRenderTargetView(pBackBuffer, NULL, &renderTargetView);
    pBackBuffer->Release();

    RECT rc;
    GetWindowRect(hwnd, &rc);

    D3D11_VIEWPORT viewport;
    viewport.MinDepth = 0;
    viewport.MaxDepth = 1;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = static_cast<float>(rc.right - rc.left);
    viewport.Height = static_cast<float>(rc.bottom - rc.top);
    deviceContext->RSSetViewports(1u, &viewport);

    if (renderTargetView) deviceContext->ClearRenderTargetView(renderTargetView, defaultColor);
    swapChain->Present(1, 0);

    return true;
}
HWND GetWallpaperWindow()
{
    // Fetch the Progman window
    HWND progman = FindWindow(L"ProgMan", NULL);
    SendMessageTimeout(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, nullptr);
    HWND wallpaper_hwnd = nullptr;
    EnumWindows(EnumWindowsProc, (LPARAM)&wallpaper_hwnd);
    return wallpaper_hwnd;
}
BOOL ApplyBackground()
{
    // Get Desktop Background Handle
    wallpaperHwnd = GetWallpaperWindow();
    if (!wallpaperHwnd) { stdlog("[!] -> Error : Cannot Find Desktop Wallpaper Handle!"); return FALSE; }

    // Set Our Window as Background
    SetParent(hwnd, wallpaperHwnd);

    // Resize Our Window to Working Area
    RECT workAreaRect;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workAreaRect, 0);
    int workAreaWidth = workAreaRect.right - workAreaRect.left;
    int workAreaHeight = workAreaRect.bottom - workAreaRect.top;
    MoveWindow(hwnd, 0, 0, workAreaWidth, workAreaHeight, TRUE);

    // Show
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    return TRUE;
}
VOID ClearBackground()
{    
    // Remove Our Window as Background
    SetParent(hwnd, nullptr);
    UpdateWindow(hwnd);

    // Close Window [ Just in Case ]
    CloseWindow(hwnd);
    DestroyWindow(hwnd);

    // Clear Desktop Background
    SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, NULL, SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE);
    SendMessage(wallpaperHwnd, WM_PAINT, 0, 0);

    // Send Close Command to Tray Icon
    SendMessage(iconTrayHwnd, WM_CLOSE, 0, 0);

    // Remove Tray Icon [ Just in Case ]
    Shell_NotifyIcon(NIM_DELETE, &notifyData);
}
VOID RenderFrame()
{
    deviceContext->ClearRenderTargetView(renderTargetView, defaultColor);
    swapChain->Present(1, 0);
}

// Renderer Thread
int ApplicationThread()
{
    // Create Background Window
    if (!RegisterAndCreateWindow()) { stdlog("[!] -> Error : Cannot Create Wallpaper Window!"); return EXIT_FAILURE; }

    // Show
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Create Tray Icon
    if (!CreateIconTray()) { stdlog("[!] -> Error : Cannot Create Icon Tray!"); return EXIT_FAILURE; }

    // Start Rendering
    allowRendering = true;

    // Start App Loop
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        // Dispatch/Translate Messages
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        // Add a delay to reduce CPU usage
        Sleep(CPU_DELAY_MS);
    }

    // Remove tray icon
    Shell_NotifyIcon(NIM_DELETE, &notifyData);

    return EXIT_SUCCESS;
}
int RendererThread()
{
    // Wait Untill Rendering is Allowed
    while (allowRendering == FALSE) { Sleep(1); }

    // Render Loop
    while (allowRendering == TRUE)
    {
        if (deviceContext && swapChain) 
        {
            // Render Frame
            RenderFrame();
        }
   
        // Wait for Next Frame
        Sleep(RENDER_INTERVAL);
    }

    return EXIT_SUCCESS;
}

// App Entrypoint
int main()
{
    // Handle Console
    if (HIDE_CONSOLE)
    {
        ShowWindow(GetConsoleWindow(), SW_HIDE);
    }
    else
    {
        SetConsoleTitle(L"BackShade™ Terminal");
    }

    // Initialize App
    stdlog("[^] -> Initializing BackShade Engine...");

    // Activate DPI Awareness
    SetProcessDPIAware();

    // Start Renderer Thread
    appThread = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)ApplicationThread, NULL, NULL, NULL);
    rendererThread = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)RendererThread, NULL, NULL, NULL);
    
    // Wait For Exit Command
    appRunning = true;
    while (appRunning) Sleep(10);

    // App Ended
    stdlog("[^] -> Engine Executed With No Issue, Press A Key to Quit...");
    Sleep(1000);

    return EXIT_SUCCESS;
}