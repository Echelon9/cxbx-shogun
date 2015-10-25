// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;;
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   Cxbx->Win32->CxbxKrnl->EmuD3D8.cpp
// *
// *  This file is part of the Cxbx project.
// *
// *  Cxbx and Cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file COPYING.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
// *
// *  All rights reserved
// *
// ******************************************************************
#define _CXBXKRNL_INTERNAL
#define _XBOXKRNL_DEFEXTRN_

// Disable overlay updates for Unreal Championship...
// TODO: Find out why a crash occurs playing the second
// intro video.
//#define UnrealChampionshipHack

// prevent name collisions
namespace xboxkrnl
{
    #include <xboxkrnl/xboxkrnl.h>
};

#include "CxbxUtil.h"
#include "CxbxKrnl.h"
#include "Emu.h"
#include "EmuFS.h"
#include "EmuShared.h"
#include "DbgConsole.h"
#include "ResourceTracker.h"
#include "EmuAlloc.h"
#include "EmuXTL.h"
#include "ResCxbxDll.h"

#include <process.h>
#include <clocale>

// Global(s)
HWND                                g_hEmuWindow   = NULL; // rendering window
#ifndef D3D9
XTL::LPDIRECT3DDEVICE8              g_pD3DDevice   = NULL; // Direct3D Device
#else
XTL::LPDIRECT3DDEVICE9              g_pD3DDevice   = NULL; // Direct3D Device
#endif
XTL::LPDIRECTDRAWSURFACE7           g_pDDSPrimary  = NULL; // DirectDraw7 Primary Surface
XTL::LPDIRECTDRAWSURFACE7           g_pDDSOverlay7 = NULL; // DirectDraw7 Overlay Surface
XTL::LPDIRECTDRAWCLIPPER            g_pDDClipper   = NULL; // DirectDraw7 Clipper
DWORD                               g_CurrentVertexShader = 0;
DWORD								g_dwCurrentPixelShader = 0;
XTL::PIXEL_SHADER                  *g_CurrentPixelShader = NULL;
BOOL                                g_bFakePixelShaderLoaded = FALSE;
BOOL                                g_bIsFauxFullscreen = FALSE;
BOOL								g_bHackUpdateSoftwareOverlay = FALSE;

// Static Function(s)
static BOOL WINAPI                  EmuEnumDisplayDevices(GUID FAR *lpGUID, LPSTR lpDriverDescription, LPSTR lpDriverName, LPVOID lpContext, HMONITOR hm);
static DWORD WINAPI                 EmuRenderWindow(LPVOID);
static DWORD WINAPI                 EmuCreateDeviceProxy(LPVOID);
static LRESULT WINAPI               EmuMsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static DWORD WINAPI                 EmuUpdateTickCount(LPVOID);
static inline void                  EmuVerifyResourceIsRegistered(XTL::X_D3DResource *pResource);
static void                         EmuAdjustPower2(UINT *dwWidth, UINT *dwHeight);

// Static Variable(s)
static GUID                         g_ddguid;               // DirectDraw driver GUID
static HMONITOR                     g_hMonitor      = NULL; // Handle to DirectDraw monitor
#ifndef D3D9
static XTL::LPDIRECT3D8             g_pD3D          = NULL; // Direct3D instance
#else
static XTL::LPDIRECT3D9             g_pD3D          = NULL; // Direct3D instance
#endif
static BOOL                         g_bSupportsYUY2 = FALSE;// Does device support YUY2 overlays?
static XTL::LPDIRECTDRAW7           g_pDD7          = NULL; // DirectDraw7
static DWORD                        g_dwOverlayW    = 640;  // Cached Overlay Width
static DWORD                        g_dwOverlayH    = 480;  // Cached Overlay Height
static DWORD                        g_dwOverlayP    = 640;  // Cached Overlay Pitch
static Xbe::Header                 *g_XbeHeader     = NULL; // XbeHeader
static uint32                       g_XbeHeaderSize = 0;    // XbeHeaderSize
#ifndef D3D9
static XTL::D3DCAPS8                g_D3DCaps;              // Direct3D Caps
#else
static XTL::D3DCAPS9                g_D3DCaps;              // Direct3D Caps
#endif
static HBRUSH                       g_hBgBrush      = NULL; // Background Brush
static volatile bool                g_bRenderWindowActive = false;
static XBVideo                      g_XBVideo;
static XTL::D3DVBLANKCALLBACK       g_pVBCallback   = NULL; // Vertical-Blank callback routine
static XTL::D3DSWAPCALLBACK			g_pSwapCallback = NULL;	// Swap/Present callback routine
static XTL::D3DCALLBACK				g_pCallback		= NULL;	// D3DDevice::InsertCallback routine
static XTL::X_D3DCALLBACKTYPE		g_CallbackType;			// Callback type
static DWORD						g_CallbackParam;		// Callback param
static BOOL                         g_bHasZBuffer = FALSE;  // Does device have Z Buffer?
//static DWORD						g_dwPrimPerFrame = 0;	// Number of primitives within one frame

// wireframe toggle
static int                          g_iWireframe    = 0;

// build version
extern uint32						g_BuildVersion;

// resource caching for _Register
static XTL::X_D3DResource pCache[16] = {0};

// current active index buffer
static XTL::X_D3DIndexBuffer       *g_pIndexBuffer  = NULL; // current active index buffer
static DWORD                        g_dwBaseVertexIndex = 0;// current active index buffer base index

// current active vertex stream
static XTL::X_D3DVertexBuffer      *g_pVertexBuffer = NULL; // current active vertex buffer
#ifndef D3D9
static XTL::IDirect3DVertexBuffer8 *g_pDummyBuffer = NULL;  // Dummy buffer, used to set unused stream sources with
#else
static XTL::IDirect3DVertexBuffer9 *g_pDummyBuffer = NULL;  // Dummy buffer, used to set unused stream sources with
#endif
static DWORD						g_dwLastSetStream = 0;	// The last stream set by D3DDevice::SetStreamSource

// current vertical blank information
static XTL::D3DVBLANKDATA           g_VBData = {0};
static DWORD                        g_VBLastSwap = 0;

// current swap information
static XTL::D3DSWAPDATA				g_SwapData = {0};
static DWORD						g_SwapLast = 0;

// cached Direct3D state variable(s)
static XTL::X_D3DSurface           *g_pCachedRenderTarget = NULL;
static XTL::X_D3DSurface           *g_pCachedZStencilSurface = NULL;
static XTL::X_D3DSurface           *g_YuvSurface = NULL;
static BOOL                         g_fYuvEnabled = FALSE;
static DWORD                        g_dwVertexShaderUsage = 0;
static DWORD                        g_VertexShaderSlots[136];

// cached palette pointer
static PVOID pCurrentPalette;
// cached palette size
static DWORD dwCurrentPaletteSize = -1;

static XTL::X_VERTEXSHADERCONSTANTMODE g_VertexShaderConstantMode = X_VSCM_192;

// cached Direct3D tiles
XTL::X_D3DTILE XTL::EmuD3DTileCache[0x08] = {0};

// cached active texture
XTL::X_D3DResource *XTL::EmuD3DActiveTexture[4] = {0,0,0,0};

// information passed to the create device proxy thread
struct EmuD3D8CreateDeviceProxyData
{
    XTL::UINT                        Adapter;
    XTL::D3DDEVTYPE                  DeviceType;
    HWND                             hFocusWindow;
    XTL::DWORD                       BehaviorFlags;
    XTL::X_D3DPRESENT_PARAMETERS    *pPresentationParameters;
#ifndef D3D9
    XTL::IDirect3DDevice8          **ppReturnedDeviceInterface;
#else
    XTL::IDirect3DDevice9          **ppReturnedDeviceInterface;
#endif
    volatile bool                    bReady;

    union
    {
        volatile HRESULT  hRet;
        volatile bool     bCreate;   // false : release
    };
}
g_EmuCDPD = {0};

// Direct3D initialization (called before emulation begins)
VOID XTL::EmuD3DInit(Xbe::Header *XbeHeader, uint32 XbeHeaderSize)
{
    g_EmuShared->GetXBVideo(&g_XBVideo);

    if(g_XBVideo.GetFullscreen())
        CxbxKrnl_hEmuParent = NULL;

    // cache XbeHeader and size of XbeHeader
    g_XbeHeader     = XbeHeader;
    g_XbeHeaderSize = XbeHeaderSize;

    // create timing thread
    {
        DWORD dwThreadId;

        HANDLE hThread = CreateThread(NULL, NULL, EmuUpdateTickCount, NULL, NULL, &dwThreadId);

        // we must duplicate this handle in order to retain Suspend/Resume thread rights from a remote thread
        {
            HANDLE hDupHandle = NULL;

            DuplicateHandle(GetCurrentProcess(), hThread, GetCurrentProcess(), &hDupHandle, 0, FALSE, DUPLICATE_SAME_ACCESS);

            CxbxKrnlRegisterThread(hDupHandle);
        }
    }

    // create the create device proxy thread
    {
        DWORD dwThreadId;

        CreateThread(NULL, NULL, EmuCreateDeviceProxy, NULL, NULL, &dwThreadId);
    }

    // create window message processing thread
    {
        DWORD dwThreadId;

        g_bRenderWindowActive = false;

        CreateThread(NULL, NULL, EmuRenderWindow, NULL, NULL, &dwThreadId);

        while(!g_bRenderWindowActive)
            Sleep(10);

        Sleep(50);
    }

    // create Direct3D8 and retrieve caps
    {
        using namespace XTL;

        // xbox Direct3DCreate8 returns "1" always, so we need our own ptr
#ifndef D3D9
        g_pD3D = Direct3DCreate8(D3D_SDK_VERSION);
#else
        g_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
#endif

        if(g_pD3D == NULL)
            CxbxKrnlCleanup("Could not initialize Direct3D8!");

        D3DDEVTYPE DevType = (g_XBVideo.GetDirect3DDevice() == 0) ? D3DDEVTYPE_HAL : D3DDEVTYPE_REF;

        g_pD3D->GetDeviceCaps(g_XBVideo.GetDisplayAdapter(), DevType, &g_D3DCaps);
    }

    SetFocus(g_hEmuWindow);

    // create default device
    {
        XTL::X_D3DPRESENT_PARAMETERS PresParam;

        ZeroMemory(&PresParam, sizeof(PresParam));

        PresParam.BackBufferWidth  = 640;
        PresParam.BackBufferHeight = 480;
        PresParam.BackBufferFormat = 6; /* X_D3DFMT_A8R8G8B8 */
        PresParam.BackBufferCount  = 1;
        PresParam.EnableAutoDepthStencil = TRUE;
        PresParam.AutoDepthStencilFormat = 0x2A; /* X_D3DFMT_D24S8 */
        PresParam.SwapEffect = XTL::D3DSWAPEFFECT_DISCARD;

        EmuSwapFS();    // XBox FS
        XTL::EmuIDirect3D8_CreateDevice(0, XTL::D3DDEVTYPE_HAL, 0, D3DCREATE_HARDWARE_VERTEXPROCESSING, &PresParam, &g_pD3DDevice);
        EmuSwapFS();    // Win2k/XP FS
    }
}

// cleanup Direct3D
VOID XTL::EmuD3DCleanup()
{
    XTL::EmuDInputCleanup();

    return;
}

// enumeration procedure for locating display device GUIDs
static BOOL WINAPI EmuEnumDisplayDevices(GUID FAR *lpGUID, LPSTR lpDriverDescription, LPSTR lpDriverName, LPVOID lpContext, HMONITOR hm)
{
    static DWORD dwEnumCount = 0;

    if(dwEnumCount++ == g_XBVideo.GetDisplayAdapter()+1)
    {
        g_hMonitor = hm;
        dwEnumCount = 0;
        if(lpGUID != 0)
        {
            memcpy(&g_ddguid, lpGUID, sizeof(GUID));
        }
        else
        {
            memset(&g_ddguid, 0, sizeof(GUID));
        }

        return FALSE;
    }

    return TRUE;
}

// window message processing thread
static DWORD WINAPI EmuRenderWindow(LPVOID lpVoid)
{
    char AsciiTitle[50];

    // register window class
    {
        HMODULE hCxbxDll = GetModuleHandle("CxbxKrnl.dll");

        LOGBRUSH logBrush = {BS_SOLID, RGB(0,0,0)};

        g_hBgBrush = CreateBrushIndirect(&logBrush);

        WNDCLASSEX wc =
        {
            sizeof(WNDCLASSEX),
            CS_CLASSDC,
            EmuMsgProc,
            0, 0, GetModuleHandle(NULL),
            LoadIcon(hCxbxDll, MAKEINTRESOURCE(IDI_CXBX)),
            LoadCursor(NULL, IDC_ARROW),
            (HBRUSH)(g_hBgBrush), NULL,
            "CxbxRender",
            NULL
        };

        RegisterClassEx(&wc);
    }

    // retrieve Xbe title (if possible)
    {
        char tAsciiTitle[40] = "Unknown";

        uint32 CertAddr = g_XbeHeader->dwCertificateAddr - g_XbeHeader->dwBaseAddr;

        if(CertAddr + 0x0C + 40 < g_XbeHeaderSize)
        {
            Xbe::Certificate *XbeCert = (Xbe::Certificate*)((uint32)g_XbeHeader + CertAddr);

            setlocale( LC_ALL, "English" );

            wcstombs(tAsciiTitle, XbeCert->wszTitleName, 40);
        }

        sprintf(AsciiTitle, "Cxbx : Emulating %s", tAsciiTitle);
    }

    // create the window
    {
        DWORD dwStyle = (g_XBVideo.GetFullscreen() || (CxbxKrnl_hEmuParent == 0))? WS_OVERLAPPEDWINDOW : WS_CHILD;

        int nTitleHeight  = GetSystemMetrics(SM_CYCAPTION);
        int nBorderWidth  = GetSystemMetrics(SM_CXSIZEFRAME);
        int nBorderHeight = GetSystemMetrics(SM_CYSIZEFRAME);

        int x = 100, y = 100, nWidth = 640, nHeight = 480;

        nWidth  += nBorderWidth*2;
        nHeight += nBorderHeight*2 + nTitleHeight;

        sscanf(g_XBVideo.GetVideoResolution(), "%d x %d", &nWidth, &nHeight);

        if(g_XBVideo.GetFullscreen())
        {
            x = y = nWidth = nHeight = 0;
            dwStyle = WS_POPUP;
        }

        HWND hwndParent = GetDesktopWindow();

        if(!g_XBVideo.GetFullscreen())
        {
            hwndParent = CxbxKrnl_hEmuParent;
        }

        g_hEmuWindow = CreateWindow
        (
            "CxbxRender", AsciiTitle,
            dwStyle, x, y, nWidth, nHeight,
            hwndParent, NULL, GetModuleHandle(NULL), NULL
        );
    }

    ShowWindow(g_hEmuWindow, (g_XBVideo.GetFullscreen() || (CxbxKrnl_hEmuParent == 0) ) ? SW_SHOWDEFAULT : SW_SHOWMAXIMIZED);
    UpdateWindow(g_hEmuWindow);

    if(!g_XBVideo.GetFullscreen() && (CxbxKrnl_hEmuParent != NULL))
    {
        SetFocus(CxbxKrnl_hEmuParent);
    }

    // initialize direct input
    if(!XTL::EmuDInputInit())
        CxbxKrnlCleanup("Could not initialize DirectInput!");

    DbgPrintf("EmuD3D8 (0x%X): Message-Pump thread is running.\n", GetCurrentThreadId());

    SetFocus(g_hEmuWindow);

    DbgConsole *dbgConsole = new DbgConsole();

    // message processing loop
    {
        MSG msg;

        ZeroMemory(&msg, sizeof(msg));

        bool lPrintfOn = g_bPrintfOn;

        g_bRenderWindowActive = true;

        while(msg.message != WM_QUIT)
        {
            if(PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            else
            {
                Sleep(10);

                // if we've just switched back to display off, clear buffer & display prompt
                if(!g_bPrintfOn && lPrintfOn)
                {
                    dbgConsole->Reset();
                }

                lPrintfOn = g_bPrintfOn;

                dbgConsole->Process();
            }
        }

        g_bRenderWindowActive = false;

        delete dbgConsole;

        CxbxKrnlCleanup(NULL);
    }

    return 0;
}

// simple helper function
void ToggleFauxFullscreen(HWND hWnd)
{
    if(g_XBVideo.GetFullscreen())
        return;

    static LONG lRestore = 0, lRestoreEx = 0;
    static RECT lRect = {0};

    if(!g_bIsFauxFullscreen)
    {
        if(CxbxKrnl_hEmuParent != NULL)
        {
            SetParent(hWnd, NULL);
        }
        else
        {
            lRestore = GetWindowLong(hWnd, GWL_STYLE);
            lRestoreEx = GetWindowLong(hWnd, GWL_EXSTYLE);

            GetWindowRect(hWnd, &lRect);
        }

        SetWindowLong(hWnd, GWL_STYLE, WS_POPUP);
        ShowWindow(hWnd, SW_MAXIMIZE);
        SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
    }
    else
    {
        if(CxbxKrnl_hEmuParent != NULL)
        {
            SetParent(hWnd, CxbxKrnl_hEmuParent);
            SetWindowLong(hWnd, GWL_STYLE, WS_CHILD);
            ShowWindow(hWnd, SW_MAXIMIZE);
            SetFocus(CxbxKrnl_hEmuParent);
        }
        else
        {
            SetWindowLong(hWnd, GWL_STYLE, lRestore);
            SetWindowLong(hWnd, GWL_EXSTYLE, lRestoreEx);
            ShowWindow(hWnd, SW_RESTORE);
            SetWindowPos(hWnd, HWND_NOTOPMOST, lRect.left, lRect.top, lRect.right - lRect.left, lRect.bottom - lRect.top, 0);
            SetFocus(hWnd);
        }
    }

    g_bIsFauxFullscreen = !g_bIsFauxFullscreen;
}

// rendering window message procedure
static LRESULT WINAPI EmuMsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static bool bAutoPaused = false;

    switch(msg)
    {
        case WM_DESTROY:
        {
            DeleteObject(g_hBgBrush);
            PostQuitMessage(0);
            return 0;
        }
        break;

        case WM_SYSKEYDOWN:
        {
            if(wParam == VK_RETURN)
            {
                ToggleFauxFullscreen(hWnd);
            }
            else if(wParam == VK_F4)
            {
                PostMessage(hWnd, WM_CLOSE, 0, 0);
            }
        }
        break;

        case WM_KEYDOWN:
        {
            /*! disable fullscreen if we are set to faux mode, and faux fullscreen is active */
            if(wParam == VK_ESCAPE)
            {
                if(g_XBVideo.GetFullscreen())
                {
                    SendMessage(hWnd, WM_CLOSE, 0, 0);
                }
                else if(g_bIsFauxFullscreen)
                {
                    ToggleFauxFullscreen(hWnd);
                }
            }
            else if(wParam == VK_F8)
            {
                g_bPrintfOn = !g_bPrintfOn;
            }
            else if(wParam == VK_F9)
            {
                XTL::g_bBrkPush = TRUE;
            }
            else if(wParam == VK_F10)
            {
                ToggleFauxFullscreen(hWnd);
            }
            else if(wParam == VK_F11)
            {
                if(g_iWireframe++ == 2)
                    g_iWireframe = 0;
            }
            else if(wParam == VK_F12)
            {
                XTL::g_bStepPush = !XTL::g_bStepPush;
            }
        }
        break;

        case WM_SIZE:
        {
            switch(wParam)
            {
                case SIZE_RESTORED:
                case SIZE_MAXIMIZED:
                {
                    if(bAutoPaused)
                    {
                        bAutoPaused = false;
                        CxbxKrnlResume();
                    }
                }
                break;

                case SIZE_MINIMIZED:
                {
                    if(g_XBVideo.GetFullscreen())
                        CxbxKrnlCleanup(NULL);

                    if(!g_bEmuSuspended)
                    {
                        bAutoPaused = true;
                        CxbxKrnlSuspend();
                    }
                }
                break;
            }
        }
        break;

        case WM_CLOSE:
            DestroyWindow(hWnd);
            break;

        case WM_SETFOCUS:
        {
            if(CxbxKrnl_hEmuParent != NULL)
            {
                SetFocus(CxbxKrnl_hEmuParent);
            }
        }
        break;

        case WM_SETCURSOR:
        {
            if(g_XBVideo.GetFullscreen() || g_bIsFauxFullscreen)
            {
                SetCursor(NULL);
                return 0;
            }

            return DefWindowProc(hWnd, msg, wParam, lParam);
        }
        break;

        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    return 0;
}

// timing thread procedure
static DWORD WINAPI EmuUpdateTickCount(LPVOID)
{
    // since callbacks come from here
    EmuGenerateFS(CxbxKrnl_TLS, CxbxKrnl_TLSData);

    DbgPrintf("EmuD3D8 (0x%X): Timing thread is running.\n", GetCurrentThreadId());

    timeBeginPeriod(0);

    // current vertical blank count
    int curvb = 0;

    while(true)
    {
        xboxkrnl::KeTickCount = timeGetTime();
        Sleep(1);

        //
        // Poll input
        //

        {
            int v;

            for(v=0;v<XINPUT_SETSTATE_SLOTS;v++)
            {
                HANDLE hDevice = g_pXInputSetStateStatus[v].hDevice;

                if(hDevice == 0)
                    continue;

                DWORD dwLatency = g_pXInputSetStateStatus[v].dwLatency++;

                if(dwLatency < XINPUT_SETSTATE_LATENCY)
                    continue;

                g_pXInputSetStateStatus[v].dwLatency = 0;

                XTL::PXINPUT_FEEDBACK pFeedback = (XTL::PXINPUT_FEEDBACK)g_pXInputSetStateStatus[v].pFeedback;

                if(pFeedback == 0)
                    continue;

                //
                // Only update slot if it has not already been updated
                //

                if(pFeedback->Header.dwStatus != ERROR_SUCCESS)
                {
                    if(pFeedback->Header.hEvent != 0)
                    {
                        SetEvent(pFeedback->Header.hEvent);
                    }

                    pFeedback->Header.dwStatus = ERROR_SUCCESS;
                }
            }
        }

        // trigger vblank callback
        {
            g_VBData.VBlank++;

			// TODO: Fixme.  This may not be right...
			g_SwapData.SwapVBlank = 1;

            if(g_pVBCallback != NULL)
            {
                EmuSwapFS();    // Xbox FS
                g_pVBCallback(&g_VBData);
                EmuSwapFS();    // Win2k/XP FS
            }

            g_VBData.Swap = 0;

			// TODO: This can't be accurate...
			g_SwapData.TimeUntilSwapVBlank = 0;

			// TODO: Recalculate this for PAL version if necessary.
			// Also, we should check the D3DPRESENT_INTERVAL value for accurracy.
		//	g_SwapData.TimeBetweenSwapVBlanks = 1/60;
			g_SwapData.TimeBetweenSwapVBlanks = 0;
        }
    }

    timeEndPeriod(0);
}

// thread dedicated to create devices
static DWORD WINAPI EmuCreateDeviceProxy(LPVOID)
{
    DbgPrintf("EmuD3D8 (0x%X): CreateDevice proxy thread is running.\n", GetCurrentThreadId());

    while(true)
    {
        // if we have been signalled, create the device with cached parameters
        if(g_EmuCDPD.bReady)
        {
            DbgPrintf("EmuD3D8 (0x%X): CreateDevice proxy thread recieved request.\n", GetCurrentThreadId());

            if(g_EmuCDPD.bCreate)
            {
                // only one device should be created at once
                // TODO: ensure all surfaces are somehow cleaned up?
                if(g_pD3DDevice != 0)
                {
                    DbgPrintf("EmuD3D8 (0x%X): CreateDevice proxy thread releasing old Device.\n", GetCurrentThreadId());

                    g_pD3DDevice->EndScene();

                    // Address DirectX Debug Runtime reported error in _DEBUG builds
                    // Direct3D8: (ERROR) :Not all objects were freed: the following indicate the types of unfreed objects.
                    #ifndef _DEBUG
                        while(g_pD3DDevice->Release() != 0);
                    #endif

                    g_pD3DDevice = 0;
                }

                if(g_EmuCDPD.pPresentationParameters->BufferSurfaces[0] != NULL)
                    EmuWarning("BufferSurfaces[0] : 0x%.08X", g_EmuCDPD.pPresentationParameters->BufferSurfaces[0]);

                if(g_EmuCDPD.pPresentationParameters->DepthStencilSurface != NULL)
                    EmuWarning("DepthStencilSurface : 0x%.08X", g_EmuCDPD.pPresentationParameters->DepthStencilSurface);

                // make adjustments to parameters to make sense with windows Direct3D
                {
                    g_EmuCDPD.DeviceType =(g_XBVideo.GetDirect3DDevice() == 0) ? XTL::D3DDEVTYPE_HAL : XTL::D3DDEVTYPE_REF;
                    g_EmuCDPD.Adapter    = g_XBVideo.GetDisplayAdapter();

                    g_EmuCDPD.pPresentationParameters->Windowed = !g_XBVideo.GetFullscreen();

#ifdef D3D9
                    g_EmuCDPD.pPresentationParameters->SwapEffect = XTL::D3DSWAPEFFECT_DISCARD;
#else
                    if(g_XBVideo.GetVSync())
                        g_EmuCDPD.pPresentationParameters->SwapEffect = XTL::D3DSWAPEFFECT_COPY_VSYNC;
#endif
#if 0//def D3D9
                    // TODO: Verify the need for this setting
                    g_EmuCDPD.pPresentationParameters->FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE;
#endif

                    g_EmuCDPD.hFocusWindow = g_hEmuWindow;

#ifndef D3D9
                    g_EmuCDPD.pPresentationParameters->BackBufferFormat       = XTL::EmuXB2PC_D3DFormat(g_EmuCDPD.pPresentationParameters->BackBufferFormat);
                    g_EmuCDPD.pPresentationParameters->AutoDepthStencilFormat = XTL::EmuXB2PC_D3DFormat(g_EmuCDPD.pPresentationParameters->AutoDepthStencilFormat);
#endif

                    if(!g_XBVideo.GetVSync() && (g_D3DCaps.PresentationIntervals & D3DPRESENT_INTERVAL_IMMEDIATE) && g_XBVideo.GetFullscreen())
                        g_EmuCDPD.pPresentationParameters->FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
                    else
                    {
                        if(g_D3DCaps.PresentationIntervals & D3DPRESENT_INTERVAL_ONE && g_XBVideo.GetFullscreen())
                            g_EmuCDPD.pPresentationParameters->FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE;
                        else
                            g_EmuCDPD.pPresentationParameters->FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
                    }

					// HACK: Disable Tripple Buffering for now...
					// TODO: Enumerate maximum BackBufferCount if possible.
					if(g_EmuCDPD.pPresentationParameters->BackBufferCount > 1)
					{
						EmuWarning("Limiting BackBufferCount to 1...");
						g_EmuCDPD.pPresentationParameters->BackBufferCount = 1;
					}

                    // TODO: Support Xbox extensions if possible
                    if(g_EmuCDPD.pPresentationParameters->MultiSampleType != 0)
                    {
                        EmuWarning("MultiSampleType 0x%.08X is not supported!", g_EmuCDPD.pPresentationParameters->MultiSampleType);

                        g_EmuCDPD.pPresentationParameters->MultiSampleType = XTL::D3DMULTISAMPLE_NONE;

                        // TODO: Check card for multisampling abilities
            //            if(pPresentationParameters->MultiSampleType == 0x00001121)
            //                pPresentationParameters->MultiSampleType = D3DMULTISAMPLE_2_SAMPLES;
            //            else
            //                CxbxKrnlCleanup("Unknown MultiSampleType (0x%.08X)", pPresentationParameters->MultiSampleType);
                    }

                    g_EmuCDPD.pPresentationParameters->Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;

                    // retrieve resolution from configuration
                    if(g_EmuCDPD.pPresentationParameters->Windowed)
                    {
                        sscanf(g_XBVideo.GetVideoResolution(), "%d x %d", &g_EmuCDPD.pPresentationParameters->BackBufferWidth, &g_EmuCDPD.pPresentationParameters->BackBufferHeight);

                        XTL::D3DDISPLAYMODE D3DDisplayMode;

                        g_pD3D->GetAdapterDisplayMode(g_XBVideo.GetDisplayAdapter(), &D3DDisplayMode);

                        g_EmuCDPD.pPresentationParameters->BackBufferFormat = D3DDisplayMode.Format;
                        g_EmuCDPD.pPresentationParameters->FullScreen_RefreshRateInHz = 0;
                    }
                    else
                    {
                        char szBackBufferFormat[16];

                        sscanf(g_XBVideo.GetVideoResolution(), "%d x %d %*dbit %s (%d hz)",
                            &g_EmuCDPD.pPresentationParameters->BackBufferWidth,
                            &g_EmuCDPD.pPresentationParameters->BackBufferHeight,
                            szBackBufferFormat,
                            &g_EmuCDPD.pPresentationParameters->FullScreen_RefreshRateInHz);

                        if(strcmp(szBackBufferFormat, "x1r5g5b5") == 0)
                            g_EmuCDPD.pPresentationParameters->BackBufferFormat = XTL::D3DFMT_X1R5G5B5;
                        else if(strcmp(szBackBufferFormat, "r5g6r5") == 0)
                            g_EmuCDPD.pPresentationParameters->BackBufferFormat = XTL::D3DFMT_R5G6B5;
                        else if(strcmp(szBackBufferFormat, "x8r8g8b8") == 0)
                            g_EmuCDPD.pPresentationParameters->BackBufferFormat = XTL::D3DFMT_X8R8G8B8;
                        else if(strcmp(szBackBufferFormat, "a8r8g8b8") == 0)
                            g_EmuCDPD.pPresentationParameters->BackBufferFormat = XTL::D3DFMT_A8R8G8B8;
                    }
                }

                // detect vertex processing capabilities
                if((g_D3DCaps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) && g_EmuCDPD.DeviceType == XTL::D3DDEVTYPE_HAL)
                {
                    DbgPrintf("EmuD3D8 (0x%X): Using hardware vertex processing\n", GetCurrentThreadId());

                    g_EmuCDPD.BehaviorFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING;
                    g_dwVertexShaderUsage = 0;
                }
                else
                {
                    DbgPrintf("EmuD3D8 (0x%X): Using software vertex processing\n", GetCurrentThreadId());

                    g_EmuCDPD.BehaviorFlags = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
                    g_dwVertexShaderUsage = D3DUSAGE_SOFTWAREPROCESSING;
                }

#if 0//def D3D9
                // TODO Verify if required, debug thoroughly
                if( g_EmuCDPD.pPresentationParameters->AutoDepthStencilFormat == 0x28)
                    __asm int 3;
#endif

                // Address debug DirectX runtime warning in _DEBUG builds
                // Direct3D8: (WARN) :Device that was created without D3DCREATE_MULTITHREADED is being used by a thread other than the creation thread.
                #ifdef _DEBUG
                    g_EmuCDPD.BehaviorFlags |= D3DCREATE_MULTITHREADED;
                #endif

                // redirect to windows Direct3D

#ifdef D3D9
                XTL::D3DPRESENT_PARAMETERS d3dpp;
                ZeroMemory(&d3dpp, sizeof(d3dpp));
                d3dpp.AutoDepthStencilFormat = XTL::D3DFMT_D24S8;//XTL::EmuXB2PC_D3DFormat(g_EmuCDPD.pPresentationParameters->AutoDepthStencilFormat);
                d3dpp.BackBufferCount = 1; //g_EmuCDPD.pPresentationParameters->BackBufferCount;
                d3dpp.BackBufferFormat = XTL::D3DFMT_A8R8G8B8; //XTL::EmuXB2PC_D3DFormat(g_EmuCDPD.pPresentationParameters->BackBufferFormat);
                d3dpp.BackBufferHeight = 480; //g_EmuCDPD.pPresentationParameters->BackBufferHeight;
                d3dpp.BackBufferWidth = 640; //g_EmuCDPD.pPresentationParameters->BackBufferWidth;
                d3dpp.EnableAutoDepthStencil = TRUE; //g_EmuCDPD.pPresentationParameters->EnableAutoDepthStencil;
                d3dpp.Flags = 0;//g_EmuCDPD.pPresentationParameters->Flags;
                d3dpp.FullScreen_RefreshRateInHz = 0;//g_EmuCDPD.pPresentationParameters->FullScreen_RefreshRateInHz;
                d3dpp.PresentationInterval = 0;//g_EmuCDPD.pPresentationParameters->FullScreen_PresentationInterval;
                d3dpp.hDeviceWindow = g_EmuCDPD.pPresentationParameters->hDeviceWindow;
                d3dpp.MultiSampleType = XTL::D3DMULTISAMPLE_NONE;//g_EmuCDPD.pPresentationParameters->MultiSampleType;
                d3dpp.MultiSampleQuality = 0;
                d3dpp.SwapEffect = XTL::D3DSWAPEFFECT_DISCARD;//g_EmuCDPD.pPresentationParameters->SwapEffect;
                d3dpp.Windowed = TRUE;//g_EmuCDPD.pPresentationParameters->Windowed;
#endif

                g_EmuCDPD.hRet = g_pD3D->CreateDevice
                (
                    g_EmuCDPD.Adapter,
                    g_EmuCDPD.DeviceType,
                    g_EmuCDPD.hFocusWindow,
                    g_EmuCDPD.BehaviorFlags,
#ifndef D3D9
                    (XTL::D3DPRESENT_PARAMETERS*)g_EmuCDPD.pPresentationParameters,
#else
                    (XTL::D3DPRESENT_PARAMETERS*)&d3dpp,
#endif
                    g_EmuCDPD.ppReturnedDeviceInterface
                );

                // report error
                if(FAILED(g_EmuCDPD.hRet))
                {
                    if(g_EmuCDPD.hRet == D3DERR_INVALIDCALL)
                        CxbxKrnlCleanup("IDirect3D8::CreateDevice failed (Invalid Call)");
                    else if(g_EmuCDPD.hRet == D3DERR_NOTAVAILABLE)
                        CxbxKrnlCleanup("IDirect3D8::CreateDevice failed (Not Available)");
                    else if(g_EmuCDPD.hRet == D3DERR_OUTOFVIDEOMEMORY)
                        CxbxKrnlCleanup("IDirect3D8::CreateDevice failed (Out of Video Memory)");

                    CxbxKrnlCleanup("IDirect3D8::CreateDevice failed (Unknown)");
                }

                // cache device pointer
                g_pD3DDevice = *g_EmuCDPD.ppReturnedDeviceInterface;

                // default NULL guid
                ZeroMemory(&g_ddguid, sizeof(GUID));

                // enumerate device guid for this monitor, for directdraw
                HRESULT hRet = XTL::DirectDrawEnumerateExA(EmuEnumDisplayDevices, NULL, DDENUM_ATTACHEDSECONDARYDEVICES);

                // create DirectDraw7
                {
                    if(FAILED(hRet))
                        hRet = XTL::DirectDrawCreateEx(NULL, (void**)&g_pDD7, XTL::IID_IDirectDraw7, NULL);
                    else
                        hRet = XTL::DirectDrawCreateEx(&g_ddguid, (void**)&g_pDD7, XTL::IID_IDirectDraw7, NULL);

                    if(FAILED(hRet))
                        CxbxKrnlCleanup("Could not initialize DirectDraw7");

                    hRet = g_pDD7->SetCooperativeLevel(0, DDSCL_NORMAL);

                    if(FAILED(hRet))
                        CxbxKrnlCleanup("Could not set cooperative level");
                }

                // check for YUY2 overlay support TODO: accept other overlay types
                {
                    DWORD  dwCodes = 0;
                    DWORD *lpCodes = 0;

                    g_pDD7->GetFourCCCodes(&dwCodes, lpCodes);

                    lpCodes = (DWORD*)CxbxMalloc(dwCodes*sizeof(DWORD));

                    g_pDD7->GetFourCCCodes(&dwCodes, lpCodes);

                    g_bSupportsYUY2 = false;
                    for(DWORD v=0;v<dwCodes;v++)
                    {
                        if(lpCodes[v] == MAKEFOURCC('Y','U','Y','2'))
                        {
                            g_bSupportsYUY2 = true;
                            break;
                        }
                    }

                    CxbxFree(lpCodes);
						
                    if(!g_bSupportsYUY2)
                        EmuWarning("YUY2 overlays are not supported in hardware, could be slow!");
					
					// Does the user want to use Hardware accelerated YUV surfaces?
					if(g_bSupportsYUY2 && g_XBVideo.GetHardwareYUV())
						DbgPrintf("EmuD3D8 (0x%X): Hardware accelerated YUV surfaces Enabled...\n", GetCurrentThreadId());
					
					if(!g_XBVideo.GetHardwareYUV())
					{
						g_bSupportsYUY2 = false;
						DbgPrintf("EmuD3D8 (0x%X): Hardware accelerated YUV surfaces Disabled...\n", GetCurrentThreadId());
					}
                }

                // initialize primary surface
                if(g_bSupportsYUY2)
                {
                    XTL::DDSURFACEDESC2 ddsd2;

                    ZeroMemory(&ddsd2, sizeof(ddsd2));

                    ddsd2.dwSize = sizeof(ddsd2);
                    ddsd2.dwFlags = DDSD_CAPS;
                    ddsd2.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

                    HRESULT hRet = g_pDD7->CreateSurface(&ddsd2, &g_pDDSPrimary, 0);

                    if(FAILED(hRet))
                        CxbxKrnlCleanup("Could not create primary surface (0x%.08X)", hRet);
                }

                // update render target cache
                g_pCachedRenderTarget = new XTL::X_D3DSurface();
                g_pCachedRenderTarget->Common = 0;
                g_pCachedRenderTarget->Data = X_D3DRESOURCE_DATA_FLAG_SPECIAL | X_D3DRESOURCE_DATA_FLAG_D3DREND;
#ifndef D3D9
                g_pD3DDevice->GetRenderTarget(&g_pCachedRenderTarget->EmuSurface8);
#else
                g_pD3DDevice->GetRenderTarget(0, &g_pCachedRenderTarget->EmuSurface8);
#endif

                // update z-stencil surface cache
                g_pCachedZStencilSurface = new XTL::X_D3DSurface();
                g_pCachedZStencilSurface->Common = 0;
                g_pCachedZStencilSurface->Data = X_D3DRESOURCE_DATA_FLAG_SPECIAL | X_D3DRESOURCE_DATA_FLAG_D3DSTEN;
                if (FAILED(g_pD3DDevice->GetDepthStencilSurface(&g_pCachedZStencilSurface->EmuSurface8)))
                    g_bHasZBuffer = FALSE;
                else
                    g_bHasZBuffer = TRUE;

                (void)g_pD3DDevice->CreateVertexBuffer
                (
                    1, 0, 0, XTL::D3DPOOL_MANAGED,
#ifndef D3D9
                    &g_pDummyBuffer
#else
                    &g_pDummyBuffer, NULL
#endif
                );

                for(int Streams = 0; Streams < 8; Streams++)
                {
#ifndef D3D9
                    g_pD3DDevice->SetStreamSource(Streams, g_pDummyBuffer, 1);
#else
                    /* One parameter has been added to the GetStreamSource and SetStreamSource methods. 
                     * The offset is the number of bytes between the beginning of the stream and the 
                     * beginning of the vertex data. It is measured in bytes.
                     *
                     * This enables the pipeline to support stream offsets. To find out if the device 
                     * supports stream offsets, see D3DDEVCAPS2_STREAMOFFSET.
                     */
                    g_pD3DDevice->SetStreamSource(Streams, g_pDummyBuffer, 0, 1 * sizeof(DWORD));
#endif
                }

                // initially, show a black screen
                // Only clear depth buffer and stencil if present
                //
                // Avoids following DirectX Debug Runtime error report
                //    [424] Direct3D8: (ERROR) :Invalid flag D3DCLEAR_ZBUFFER: no zbuffer is associated with device. Clear failed. 
                //
                if (g_bHasZBuffer)
                    g_pD3DDevice->Clear(0, 0, D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL, 0xFF000000, 1.0f, 0);
                else
                    g_pD3DDevice->Clear(0, 0, D3DCLEAR_TARGET, 0xFF000000, 0.0f, 0);
				g_pD3DDevice->BeginScene();
				g_pD3DDevice->EndScene();
                g_pD3DDevice->Present(0, 0, 0, 0);

                // begin scene
                g_pD3DDevice->BeginScene();

                // signal completion
                g_EmuCDPD.bReady = false;
            }
            else
            {
                // release direct3d
                if(g_pD3DDevice != 0)
                {
                    DbgPrintf("EmuD3D8 (0x%X): CreateDevice proxy thread releasing old Device.\n", GetCurrentThreadId());

                    g_pD3DDevice->EndScene();

                    g_EmuCDPD.hRet = g_pD3DDevice->Release();

                    if(g_EmuCDPD.hRet == 0)
                        g_pD3DDevice = 0;
                }

                if(g_bSupportsYUY2)
                {
                    // cleanup directdraw surface
                    if(g_pDDSPrimary != 0)
                    {
                        g_pDDSPrimary->Release();
                        g_pDDSPrimary = 0;
                    }
                }

                // cleanup directdraw
                if(g_pDD7 != 0)
                {
                    g_pDD7->Release();
                    g_pDD7 = 0;
                }

                // signal completion
                g_EmuCDPD.bReady = false;
            }
        }

        Sleep(1);
    }

    return 0;
}

// check if a resource has been registered yet (if not, register it)
static void EmuVerifyResourceIsRegistered(XTL::X_D3DResource *pResource)
{
    // 0xEEEEEEEE and 0xFFFFFFFF are somehow set in Halo :(
    if(pResource == 0 || pResource->Lock != 0 && pResource->Lock != 0xEEEEEEEE && pResource->Lock != 0xFFFFFFFF)
        return;

    // Already "Registered" implicitly
    if((IsSpecialResource(pResource->Data) && ((pResource->Data & X_D3DRESOURCE_DATA_FLAG_D3DREND) || (pResource->Data & X_D3DRESOURCE_DATA_FLAG_D3DSTEN)))
     ||(pResource->Data == 0xB00BBABE))
        return;

    int v=0;

    for(v=0;v<16;v++)
    {
        if(pCache[v].Data == pResource->Data && pResource->Data != 0)
        {
            pResource->EmuResource8 = pCache[v].EmuResource8;
            return;
        }
    }

    EmuSwapFS();    // XBox FS;
    XTL::EmuIDirect3DResource8_Register(pResource, 0/*(PVOID)pResource->Data*/);
    EmuSwapFS();    // Win2k/XP FS

    if(pResource->Lock != X_D3DRESOURCE_LOCK_FLAG_NOSIZE)
    {
        for(v=0;v<16;v++)
        {
            if(pCache[v].Data == 0)
            {
                pCache[v].Data = pResource->Data;
                pCache[v].EmuResource8 = pResource->EmuResource8;
                break;
            }

            if(v == 16)
                CxbxKrnlCleanup("X_D3DResource cache is maxed out!");
        }
    }
}

// ensure a given width/height are powers of 2
static void EmuAdjustPower2(UINT *dwWidth, UINT *dwHeight)
{
    UINT NewWidth=0, NewHeight=0;

    int v;

    for(v=0;v<32;v++)
    {
        int mask = 1 << v;

        if(*dwWidth & mask)
            NewWidth = mask;

        if(*dwHeight & mask)
            NewHeight = mask;
    }

    if(*dwWidth != NewWidth)
    {
        NewWidth <<= 1;
        EmuWarning("Needed to resize width (%d->%d)", *dwWidth, NewWidth);
    }

    if(*dwHeight != NewHeight)
    {
        NewHeight <<= 1;
        EmuWarning("Needed to resize height (%d->%d)", *dwHeight, NewHeight);
    }

    *dwWidth = NewWidth;
    *dwHeight = NewHeight;
}

// Derived from EmuUnswizzleActiveTexture
static void EmuUnswizzleTextureStages()
{
	for( int i = 0; i < 4; i++ )
	{
		// for current usages, we're always on stage 0
		XTL::X_D3DPixelContainer *pPixelContainer = (XTL::X_D3DPixelContainer*)XTL::EmuD3DActiveTexture[i];

		if(pPixelContainer == NULL || !(pPixelContainer->Common & X_D3DCOMMON_ISLOCKED))
			return;

		DWORD XBFormat = (pPixelContainer->Format & X_D3DFORMAT_FORMAT_MASK) >> X_D3DFORMAT_FORMAT_SHIFT;
		DWORD dwBPP = 0;

		if(!XTL::EmuXBFormatIsSwizzled(XBFormat, &dwBPP))
			return;

		// remove lock
		pPixelContainer->EmuTexture8->UnlockRect(0);
		pPixelContainer->Common &= ~X_D3DCOMMON_ISLOCKED;

		// TODO: potentially CRC to see if this surface was actually modified..

		//
		// unswizzle texture
		//

		{
#ifndef D3D9
			XTL::IDirect3DTexture8 *pTexture = pPixelContainer->EmuTexture8;
#else
			XTL::IDirect3DTexture9 *pTexture = pPixelContainer->EmuTexture8;
#endif

			DWORD dwLevelCount = pTexture->GetLevelCount();

			for(uint32 v=0;v<dwLevelCount;v++)
			{
				XTL::D3DSURFACE_DESC SurfaceDesc;

				HRESULT hRet = pTexture->GetLevelDesc(v, &SurfaceDesc);

				if(FAILED(hRet))
					continue;

				//
				// perform unswizzle
				//

				{
					XTL::D3DLOCKED_RECT LockedRect;

					//if(SurfaceDesc.Format != XTL::D3DFMT_A8R8G8B8)
					//    break;
					//CxbxKrnlCleanup("Temporarily unsupported format for active texture unswizzle (0x%.08X)", SurfaceDesc.Format);

					hRet = pTexture->LockRect(v, &LockedRect, NULL, NULL);

					if(FAILED(hRet))
						continue;

					DWORD dwWidth = SurfaceDesc.Width;
					DWORD dwHeight = SurfaceDesc.Height;
					DWORD dwDepth = 1;
					DWORD dwPitch = LockedRect.Pitch;
					RECT  iRect = {0,0,0,0};
					POINT iPoint = {0,0};

					void *pTemp = malloc(dwHeight*dwPitch);

					XTL::EmuXGUnswizzleRect
					(
						LockedRect.pBits, dwWidth, dwHeight, dwDepth,
						pTemp, dwPitch, iRect, iPoint, dwBPP
					);

					memcpy(LockedRect.pBits, pTemp, dwPitch*dwHeight);

					pTexture->UnlockRect(0);

					free(pTemp);
				}
			}

			DbgPrintf("Texture Stage %d was unswizzled\n", i);
		}
	}
}

// ******************************************************************
// * func: EmuIDirect3D8_CreateDevice
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3D8_CreateDevice
(
    UINT                        Adapter,
    D3DDEVTYPE                  DeviceType,
    HWND                        hFocusWindow,
    DWORD                       BehaviorFlags,
    X_D3DPRESENT_PARAMETERS    *pPresentationParameters,
#ifndef D3D9
    IDirect3DDevice8          **ppReturnedDeviceInterface
#else
    IDirect3DDevice9          **ppReturnedDeviceInterface
#endif
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3D8_CreateDevice\n"
           "(\n"
           "   Adapter                   : 0x%.08X\n"
           "   DeviceType                : 0x%.08X\n"
           "   hFocusWindow              : 0x%.08X\n"
           "   BehaviorFlags             : 0x%.08X\n"
           "   pPresentationParameters   : 0x%.08X\n"
           "   ppReturnedDeviceInterface : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Adapter, DeviceType, hFocusWindow,
           BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);

	// Print a few of the pPresentationParameters contents to the console
	DbgPrintf("BackBufferWidth:        = %d\n"
			  "BackBufferHeight:       = %d\n"
			  "BackBufferFormat:       = 0x%.08X\n"
			  "BackBufferCount:        = 0x%.08X\n"
			  "SwapEffect:             = 0x%.08X\n"
			  "EnableAutoDepthStencil: = 0x%.08X\n"
			  "AutoDepthStencilFormat: = 0x%.08X\n"
			  "Flags:                  = 0x%.08X\n\n",
			  pPresentationParameters->BackBufferWidth, pPresentationParameters->BackBufferHeight,
			  pPresentationParameters->BackBufferFormat, pPresentationParameters->BackBufferCount,
			  pPresentationParameters->SwapEffect, pPresentationParameters->EnableAutoDepthStencil,
			  pPresentationParameters->AutoDepthStencilFormat, pPresentationParameters->Flags );

    // Cache parameters
    g_EmuCDPD.Adapter = Adapter;
    g_EmuCDPD.DeviceType = DeviceType;
    g_EmuCDPD.hFocusWindow = hFocusWindow;
    g_EmuCDPD.pPresentationParameters = pPresentationParameters;
    g_EmuCDPD.ppReturnedDeviceInterface = ppReturnedDeviceInterface;

    // Wait until proxy is done with an existing call (i highly doubt this situation will come up)
    while(g_EmuCDPD.bReady)
        Sleep(10);

    // Signal proxy thread, and wait for completion
    g_EmuCDPD.bReady = true;
    g_EmuCDPD.bCreate = true;

    // Wait until proxy is completed
    while(g_EmuCDPD.bReady)
        Sleep(10);

    EmuSwapFS();   // XBox FS

    return g_EmuCDPD.hRet;
}

// ******************************************************************
// * func: EmuIDirect3DResource8_IsBusy
// ******************************************************************
BOOL WINAPI XTL::EmuIDirect3DDevice8_IsBusy()
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_IsBusy();\n",
           GetCurrentThreadId());

    EmuWarning("EmuIDirect3DDevice8_IsBusy ignored!");

    EmuSwapFS();   // XBox FS

    return FALSE;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetCreationParameters
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetCreationParameters\n"
           "(\n"
           "   pParameters               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pParameters);

    pParameters->AdapterOrdinal = D3DADAPTER_DEFAULT;
    pParameters->DeviceType = D3DDEVTYPE_HAL;
    pParameters->hFocusWindow = NULL;
    pParameters->BehaviorFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING;

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3D8_CheckDeviceFormat
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3D8_CheckDeviceFormat
(
    UINT                        Adapter,
    D3DDEVTYPE                  DeviceType,
    D3DFORMAT                   AdapterFormat,
    DWORD                       Usage,
    X_D3DRESOURCETYPE           RType,
    X_D3DFORMAT                 CheckFormat
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3D8_CheckDeviceFormat\n"
           "(\n"
           "   Adapter                   : 0x%.08X\n"
           "   DeviceType                : 0x%.08X\n"
           "   AdapterFormat             : 0x%.08X\n"
           "   Usage                     : 0x%.08X\n"
           "   RType                     : 0x%.08X\n"
           "   CheckFormat               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Adapter, DeviceType, AdapterFormat,
           Usage, RType, CheckFormat);

    if(RType > 7)
        CxbxKrnlCleanup("RType > 7");

	// HACK: Return true for everything? (Hunter the Reckoning)

    HRESULT hRet = S_OK; /*g_pD3D->CheckDeviceFormat
    (
        g_XBVideo.GetDisplayAdapter(), (g_XBVideo.GetDirect3DDevice() == 0) ? XTL::D3DDEVTYPE_HAL : XTL::D3DDEVTYPE_REF,
        EmuXB2PC_D3DFormat(AdapterFormat), Usage, (D3DRESOURCETYPE)RType, EmuXB2PC_D3DFormat(CheckFormat)
    );*/

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetDisplayFieldStatus
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_GetDisplayFieldStatus(X_D3DFIELD_STATUS *pFieldStatus)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetDisplayFieldStatus\n"
           "(\n"
           "   pFieldStatus              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pFieldStatus);

#if 1
    pFieldStatus->Field = (g_VBData.VBlank%2 == 0) ? X_D3DFIELD_ODD : X_D3DFIELD_EVEN;
    pFieldStatus->VBlankCount = g_VBData.VBlank;
#else
	pFieldStatus->Field = X_D3DFIELD_PROGRESSIVE;
	pFieldStatus->VBlankCount = 0;
#endif

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_BeginPush
// ******************************************************************
PDWORD WINAPI XTL::EmuIDirect3DDevice8_BeginPush(DWORD Count)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_BeginPush(%d);\n", GetCurrentThreadId(), Count);

    DWORD *pRet = new DWORD[Count];

    g_dwPrimaryPBCount = Count;
    g_pPrimaryPB = pRet;

    EmuSwapFS();   // XBox FS

    return pRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_EndPush
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_EndPush(DWORD *pPush)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_EndPush(0x%.08X);\n", GetCurrentThreadId(), pPush);

#ifdef _DEBUG_TRACK_PB
//	DbgDumpPushBuffer(g_pPrimaryPB, g_dwPrimaryPBCount*sizeof(DWORD));
#endif

    EmuExecutePushBufferRaw(g_pPrimaryPB);

    delete[] g_pPrimaryPB;

    g_pPrimaryPB = 0;

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_BeginVisibilityTest
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_BeginVisibilityTest()
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_BeginVisibilityTest();\n", GetCurrentThreadId());

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_EndVisibilityTest
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_EndVisibilityTest
(
    DWORD                       Index
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_EndVisibilityTest\n"
           "(\n"
           "   Index                     : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Index);

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetBackBufferScale
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetBackBufferScale(FLOAT x, FLOAT y)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetBackBufferScale\n"
           "(\n"
           "   x                         : %f\n"
           "   y                         : %f\n"
           ");\n",
           GetCurrentThreadId(), x, y);

    EmuWarning("SetBackBufferScale ignored");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetVisibilityTestResult
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetVisibilityTestResult
(
    DWORD                       Index,
    UINT                       *pResult,
    ULONGLONG                  *pTimeStamp
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetVisibilityTestResult\n"
           "(\n"
           "   Index                     : 0x%.08X\n"
           "   pResult                   : 0x%.08X\n"
           "   pTimeStamp                : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Index, pResult, pTimeStamp);

    // TODO: actually emulate this!?

    if(pResult != 0)
        *pResult = 640*480;

    if(pTimeStamp != 0)
        *pTimeStamp = 0;

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetDeviceCaps
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_GetDeviceCaps
(
#ifndef D3D9
    D3DCAPS8                   *pCaps
#else
    D3DCAPS9                   *pCaps
#endif
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetDeviceCaps\n"
           "(\n"
           "   pCaps                     : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pCaps);

    HRESULT hRet = g_pD3D->GetDeviceCaps(g_XBVideo.GetDisplayAdapter(), (g_XBVideo.GetDirect3DDevice() == 0) ? XTL::D3DDEVTYPE_HAL : XTL::D3DDEVTYPE_REF, pCaps);
	if(FAILED(hRet))
		CxbxKrnlCleanup("EmuIDirect3DDevice8_GetDeviceCaps failed!");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_LoadVertexShader
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_LoadVertexShader
(
    DWORD                       Handle,
    DWORD                       Address
)
{
    EmuSwapFS();   // Win2k/XP FS

    // debug trace
    DbgPrintf( "EmuD3D8 (0x%X): EmuIDirect3DDevice8_LoadVertexShader\n"
               "(\n"
               "   Handle              : 0x%.08X\n"
               "   Address             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Handle,Address);

    if(Address < 136 && VshHandleIsVertexShader(Handle))
    {
        VERTEX_SHADER *pVertexShader = (VERTEX_SHADER *)(VshHandleGetVertexShader(Handle))->Handle;
        for (DWORD i = Address; i < pVertexShader->Size; i++)
        {
            // TODO: This seems very fishy
            g_VertexShaderSlots[i] = Handle;
        }
    }

    EmuSwapFS();   // Xbox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SelectVertexShader
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SelectVertexShader
(
    DWORD                       Handle,
    DWORD                       Address
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SelectVertexShader\n"
           "(\n"
           "   Handle              : 0x%.08X\n"
           "   Address             : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Handle, Address);

#ifndef D3D9
    if(VshHandleIsVertexShader(Handle))
    {
        VERTEX_SHADER *pVertexShader = (VERTEX_SHADER *)(((X_D3DVertexShader *)(Handle & 0x7FFFFFFF))->Handle);
        g_pD3DDevice->SetVertexShader(pVertexShader->Handle);
    }
    else if(Handle == NULL)
    {
        g_pD3DDevice->SetVertexShader(D3DFVF_XYZ | D3DFVF_TEX0);
    }
    else if(Address < 136)
    {
        X_D3DVertexShader *pVertexShader = (X_D3DVertexShader*)g_VertexShaderSlots[Address];

        if(pVertexShader != NULL)
        {
            g_pD3DDevice->SetVertexShader(((VERTEX_SHADER *)((X_D3DVertexShader *)g_VertexShaderSlots[Address])->Handle)->Handle);
        }
        else
        {
            EmuWarning("g_VertexShaderSlots[%d] = 0", Address);
        }
    }
#else
    if(Handle == NULL)
    {
        g_pD3DDevice->SetFVF(D3DFVF_XYZ | D3DFVF_TEX0);
    }
    else
        g_pD3DDevice->SetFVF(Handle);
#endif

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3D8_GetAdapterModeCount
// ******************************************************************
UINT WINAPI XTL::EmuIDirect3D8_GetAdapterModeCount
(
    UINT                        Adapter
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3D8_GetAdapterModeCount\n"
           "(\n"
           "   Adapter                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Adapter);

#ifndef D3D9
    UINT ret = g_pD3D->GetAdapterModeCount(g_XBVideo.GetDisplayAdapter());
#else
    D3DFORMAT D3DFormat = EmuXB2PC_D3DFormat( g_EmuCDPD.pPresentationParameters->BackBufferFormat );
    /* EnumAdapterModes and GetAdapterModeCount now take a D3DFORMAT.
     * 
     * The format supports an expanding set of display modes. 
     * To protect applications from enumerating formats that were 
     * not invented when the application shipped, the application 
     * must tell Direct3D the format for the display modes that 
     * should be enumerated. The resulting array of display modes 
     * will differ only by width, height, and refresh rate.
     */
    UINT ret = g_pD3D->GetAdapterModeCount(g_XBVideo.GetDisplayAdapter(), D3DFormat);
#endif

    D3DDISPLAYMODE Mode;

    for(uint32 v=0;v<ret;v++)
    {
#ifndef D3D9
        HRESULT hRet = g_pD3D->EnumAdapterModes(g_XBVideo.GetDisplayAdapter(), v, &Mode);
#else
        /* EnumAdapterModes and GetAdapterModeCount now take a D3DFORMAT.
         * 
         * The format supports an expanding set of display modes. 
         * To protect applications from enumerating formats that were 
         * not invented when the application shipped, the application 
         * must tell Direct3D the format for the display modes that 
         * should be enumerated. The resulting array of display modes 
         * will differ only by width, height, and refresh rate.
         */
        HRESULT hRet = g_pD3D->EnumAdapterModes(g_XBVideo.GetDisplayAdapter(), D3DFormat, v, &Mode);
#endif

        if(hRet != D3D_OK)
            break;

        if(Mode.Width != 640 || Mode.Height != 480)
            ret--;
    }

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3D8_GetAdapterDisplayMode
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3D8_GetAdapterDisplayMode
(
    UINT                        Adapter,
    X_D3DDISPLAYMODE           *pMode
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3D8_GetAdapterDisplayMode\n"
           "(\n"
           "   Adapter                   : 0x%.08X\n"
           "   pMode                     : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Adapter, pMode);

    // NOTE: WARNING: We should cache the "Emulated" display mode and return
    // This value. We can initialize the cache with the default Xbox mode data.
    HRESULT hRet = g_pD3D->GetAdapterDisplayMode
    (
        g_XBVideo.GetDisplayAdapter(),
        (D3DDISPLAYMODE*)pMode
    );

    // make adjustments to the parameters to make sense with windows direct3d
    {
        D3DDISPLAYMODE *pPCMode = (D3DDISPLAYMODE*)pMode;

        // Convert Format (PC->Xbox)
        pMode->Format = EmuPC2XB_D3DFormat(pPCMode->Format);

        // TODO: Make this configurable in the future?
        // D3DPRESENTFLAG_FIELD | D3DPRESENTFLAG_INTERLACED | D3DPRESENTFLAG_LOCKABLE_BACKBUFFER
        pMode->Flags  = 0x000000A1;

        // TODO: Retrieve from current CreateDevice settings?
        pMode->Width = 640;
        pMode->Height = 480;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3D8_EnumAdapterModes
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3D8_EnumAdapterModes
(
    UINT                        Adapter,
    UINT                        Mode,
    X_D3DDISPLAYMODE           *pMode
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3D8_EnumAdapterModes\n"
           "(\n"
           "   Adapter                   : 0x%.08X\n"
           "   Mode                      : 0x%.08X\n"
           "   pMode                     : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Adapter, Mode, pMode);

    HRESULT hRet;

    static int ModeAdder = 0;
#ifdef D3D9
    D3DFORMAT D3DFormat = EmuXB2PC_D3DFormat( g_EmuCDPD.pPresentationParameters->BackBufferFormat );
#endif

    if(Mode == 0)
        ModeAdder = 0;

    D3DDISPLAYMODE PCMode;

    do
    {
#ifndef D3D9
        hRet = g_pD3D->EnumAdapterModes(g_XBVideo.GetDisplayAdapter(), Mode+ModeAdder, (D3DDISPLAYMODE*)&PCMode);
#else
        /* EnumAdapterModes and GetAdapterModeCount now take a D3DFORMAT.
         * 
         * The format supports an expanding set of display modes. 
         * To protect applications from enumerating formats that were 
         * not invented when the application shipped, the application 
         * must tell Direct3D the format for the display modes that 
         * should be enumerated. The resulting array of display modes 
         * will differ only by width, height, and refresh rate.
         */
        hRet = g_pD3D->EnumAdapterModes(g_XBVideo.GetDisplayAdapter(), D3DFormat, Mode+ModeAdder, (D3DDISPLAYMODE*)&PCMode);
#endif

        if(hRet != D3D_OK || (PCMode.Width == 640 && PCMode.Height == 480))
            break;

        ModeAdder++;
    }
    while(true);

    // make adjustments to parameters to make sense with windows direct3d
    if(hRet == D3D_OK)
    {
        //
        // NOTE: WARNING: PC D3DDISPLAYMODE is different than Xbox D3DDISPLAYMODE!
        //

        // Convert Format (PC->Xbox)
        pMode->Width  = PCMode.Width;
        pMode->Height = PCMode.Height;
        pMode->RefreshRate = PCMode.RefreshRate;

        // TODO: Make this configurable in the future?
        // D3DPRESENTFLAG_FIELD | D3DPRESENTFLAG_INTERLACED | D3DPRESENTFLAG_LOCKABLE_BACKBUFFER
        pMode->Flags  = 0x000000A1;

        pMode->Format = EmuPC2XB_D3DFormat(PCMode.Format);
    }
    else
    {
//		hRet = S_OK;
        hRet = D3DERR_INVALIDCALL;
//		CxbxKrnlCleanup("EnumAdapterModes failed!");
    }

    EmuSwapFS();   // XBox FS

 //   return hRet;
	return S_OK; // Hack
}

// ******************************************************************
// * func: EmuIDirect3D8_KickOffAndWaitForIdle
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3D8_KickOffAndWaitForIdle()
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3D8_KickOffAndWaitForIdle()\n", GetCurrentThreadId());

    // TODO: Actually do something here?

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3D8_KickOffAndWaitForIdle2
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3D8_KickOffAndWaitForIdle2(DWORD dwDummy1, DWORD dwDummy2)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3D8_KickOffAndWaitForIdle\n"
           "(\n"
           "   dwDummy1            : 0x%.08X\n"
           "   dwDummy2            : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), dwDummy1, dwDummy2);

    // TODO: Actually do something here?

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetGammaRamp
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetGammaRamp
(
    DWORD                   dwFlags,
    CONST X_D3DGAMMARAMP   *pRamp
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetGammaRamp\n"
           "(\n"
           "   dwFlags             : 0x%.08X\n"
           "   pRamp               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), dwFlags, pRamp);

    // remove D3DSGR_IMMEDIATE
    DWORD dwPCFlags = dwFlags & (~0x00000002);
    D3DGAMMARAMP PCRamp;

    for(int v=0;v<255;v++)
    {
        PCRamp.red[v]   = pRamp->red[v];
        PCRamp.green[v] = pRamp->green[v];
        PCRamp.blue[v]  = pRamp->blue[v];
    }

#ifndef D3D9
//    g_pD3DDevice->SetGammaRamp(dwPCFlags, &PCRamp);
#else
//    g_pD3DDevice->SetGammaRamp(0, dwPCFlags, &PCRamp);
#endif

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_AddRef
// ******************************************************************
ULONG WINAPI XTL::EmuIDirect3DDevice8_AddRef()
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_AddRef()\n", GetCurrentThreadId());

    ULONG ret = g_pD3DDevice->AddRef();

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_BeginStateBlock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_BeginStateBlock()
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_BeginStateBlock()\n", GetCurrentThreadId());

    ULONG ret = g_pD3DDevice->BeginStateBlock();

    EmuSwapFS();   // XBox FS

    return ret;
}

/*// ******************************************************************
// * func: EmuIDirect3DDevice8_BeginStateBig
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_BeginStateBig()
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_BeginStateBig()\n", GetCurrentThreadId());

    //ULONG ret = g_pD3DDevice->BeginStateBlock();

    CxbxKrnlCleanup("BeginStateBig is not implemented");

    EmuSwapFS();   // XBox FS

    return ret;
}*/

// ******************************************************************
// * func: EmuIDirect3DDevice8_CaptureStateBlock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CaptureStateBlock(DWORD Token)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CaptureStateBlock\n"
           "(\n"
           "   Token               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Token);

#ifndef D3D9
    ULONG ret = g_pD3DDevice->CaptureStateBlock(Token);
#else
    // TODO: Implement D3D9 equivalent
    // CreateStateBlock()
    // d3dDevice->CaptureStateBlock(stateblockHandle) == stateblock->Capture()
    ULONG ret = E_FAIL; //g_pD3DDevice->CaptureStateBlock(Token);
#endif

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_ApplyStateBlock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_ApplyStateBlock(DWORD Token)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_ApplyStateBlock\n"
           "(\n"
           "   Token               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Token);

#ifndef D3D9
    ULONG ret = g_pD3DDevice->ApplyStateBlock(Token);
#else
    // TODO: Implement D3D9 equivalent
    // CreateStateBlock()
    // d3dDevice->ApplyStateBlock(stateblockHandle) == stateblock->Apply()
    ULONG ret = E_FAIL; //g_pD3DDevice->ApplyStateBlock(Token);
#endif

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_EndStateBlock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_EndStateBlock(DWORD *pToken)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_EndStateBlock\n"
           "(\n"
           "   pToken              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pToken);

#ifndef D3D9
    ULONG ret = g_pD3DDevice->EndStateBlock(pToken);
#else
    ULONG ret = g_pD3DDevice->EndStateBlock((XTL::IDirect3DStateBlock9**) &pToken);
#endif

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CopyRects
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CopyRects
(
    X_D3DSurface       *pSourceSurface,
    CONST RECT         *pSourceRectsArray,
    UINT                cRects,
    X_D3DSurface       *pDestinationSurface,
    CONST POINT        *pDestPointsArray
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CopyRects\n"
           "(\n"
           "   pSourceSurface      : 0x%.08X\n"
           "   pSourceRectsArray   : 0x%.08X\n"
           "   cRects              : 0x%.08X\n"
           "   pDestinationSurface : 0x%.08X\n"
           "   pDestPointsArray    : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pSourceSurface, pSourceRectsArray, cRects,
           pDestinationSurface, pDestPointsArray);

    pSourceSurface->EmuSurface8->UnlockRect();

    /*
    static int kthx = 0;
    char fileName[255];

    sprintf(fileName, "C:\\Aaron\\Textures\\SourceSurface-%d.bmp", kthx++);

    D3DXSaveSurfaceToFile(fileName, D3DXIFF_BMP, pSourceSurface->EmuSurface8, NULL, NULL);
    //*/

#ifndef D3D9
    HRESULT hRet = g_pD3DDevice->CopyRects
    (
        pSourceSurface->EmuSurface8,
        pSourceRectsArray,
        cRects,
        pDestinationSurface->EmuSurface8,
        pDestPointsArray
    );
#else
   HRESULT hRet = S_OK;

    for( UINT i = 0; i < cRects; i++ )
    {
        RECT DestRect = { pDestPointsArray[i].x, pDestPointsArray[i].y, pSourceRectsArray[i].right, pSourceRectsArray[i].bottom };

        hRet = g_pD3DDevice->StretchRect
        (
            pSourceSurface->EmuSurface8,
            &pSourceRectsArray[i],
            pDestinationSurface->EmuSurface8,
            &DestRect, D3DTEXF_LINEAR
        );
    }
#endif

    EmuSwapFS();   // Xbox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateImageSurface
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateImageSurface
(
    UINT                Width,
    UINT                Height,
    X_D3DFORMAT         Format,
    X_D3DSurface      **ppBackBuffer
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreateImageSurface\n"
           "(\n"
           "   Width               : 0x%.08X\n"
           "   Height              : 0x%.08X\n"
           "   Format              : 0x%.08X\n"
           "   ppBackBuffer        : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Width, Height, Format, ppBackBuffer);

    *ppBackBuffer = new X_D3DSurface();

    D3DFORMAT PCFormat = EmuXB2PC_D3DFormat(Format);

#ifndef D3D9
    HRESULT hRet = g_pD3DDevice->CreateImageSurface(Width, Height, PCFormat, &((*ppBackBuffer)->EmuSurface8));
#else
    HRESULT hRet = g_pD3DDevice->CreateOffscreenPlainSurface( Width, Height, PCFormat, D3DPOOL_SCRATCH /*D3DPOOL_MANAGED*/, &((*ppBackBuffer)->EmuSurface8), NULL );
#endif
	if(FAILED(hRet) && Format == 0x2E)
	{
		EmuWarning("CreateImageSurface: D3DFMT_LIN_D24S8 -> D3DFMT_A8R8G8B8");

#ifndef D3D9
		hRet = g_pD3DDevice->CreateImageSurface(Width, Height, D3DFMT_A8R8G8B8, &((*ppBackBuffer)->EmuSurface8));
#else
		hRet = g_pD3DDevice->CreateOffscreenPlainSurface(Width, Height, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH /*D3DPOOL_MANAGED*/, &((*ppBackBuffer)->EmuSurface8), NULL );
#endif
	}
	
	if(FAILED(hRet))
		/*EmuWarning*/CxbxKrnlCleanup("CreateImageSurface failed!\nFormat = 0x%8.8X", Format);

    EmuSwapFS();   // Xbox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetGammaRamp
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_GetGammaRamp
(
    X_D3DGAMMARAMP     *pRamp
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetGammaRamp\n"
           "(\n"
           "   pRamp               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pRamp);

    D3DGAMMARAMP *pGammaRamp = (D3DGAMMARAMP *)malloc(sizeof(D3DGAMMARAMP));

#ifndef D3D9
    g_pD3DDevice->GetGammaRamp(pGammaRamp);
#else
    g_pD3DDevice->GetGammaRamp(0, pGammaRamp);
#endif

    for(int v=0;v<256;v++)
    {
        pRamp->red[v] = (BYTE)pGammaRamp->red[v];
        pRamp->green[v] = (BYTE)pGammaRamp->green[v];
        pRamp->blue[v] = (BYTE)pGammaRamp->blue[v];
    }

    free(pGammaRamp);

    EmuSwapFS();   // Xbox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetBackBuffer2
// ******************************************************************
XTL::X_D3DSurface* WINAPI XTL::EmuIDirect3DDevice8_GetBackBuffer2
(
    INT                 BackBuffer
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetBackBuffer2\n"
           "(\n"
           "   BackBuffer          : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), BackBuffer);

    /** unsafe, somehow
    HRESULT hRet = S_OK;

    X_D3DSurface *pBackBuffer = new X_D3DSurface();

    if(BackBuffer == -1)
    {
        static IDirect3DSurface8 *pCachedPrimarySurface = 0;

        if(pCachedPrimarySurface == 0)
        {
            // create a buffer to return
            // TODO: Verify the surface is always 640x480
            g_pD3DDevice->CreateImageSurface(640, 480, D3DFMT_A8R8G8B8, &pCachedPrimarySurface);
        }

        pBackBuffer->EmuSurface8 = pCachedPrimarySurface;

        hRet = g_pD3DDevice->GetFrontBuffer(pBackBuffer->EmuSurface8);

        if(FAILED(hRet))
        {
            EmuWarning("Could not retrieve primary surface, using backbuffer");
            pCachedPrimarySurface = 0;
            pBackBuffer->EmuSurface8->Release();
            pBackBuffer->EmuSurface8 = 0;
            BackBuffer = 0;
        }

        // Debug: Save this image temporarily
        //D3DXSaveSurfaceToFile("C:\\Aaron\\Textures\\FrontBuffer.bmp", D3DXIFF_BMP, pBackBuffer->EmuSurface8, NULL, NULL);
    }

    if(BackBuffer != -1)
        hRet = g_pD3DDevice->GetBackBuffer(BackBuffer, D3DBACKBUFFER_TYPE_MONO, &(pBackBuffer->EmuSurface8));
    //*/

    static X_D3DSurface *pBackBuffer = new X_D3DSurface();

    if(BackBuffer == -1)
        BackBuffer = 0;

#ifndef D3D9
    HRESULT hRet = g_pD3DDevice->GetBackBuffer(BackBuffer, D3DBACKBUFFER_TYPE_MONO, &(pBackBuffer->EmuSurface8));
#else
    HRESULT hRet = g_pD3DDevice->GetBackBuffer(0, BackBuffer, D3DBACKBUFFER_TYPE_MONO, &(pBackBuffer->EmuSurface8));
#endif

    if(FAILED(hRet))
        CxbxKrnlCleanup("Unable to retrieve back buffer");

    // update data pointer
    pBackBuffer->Data = X_D3DRESOURCE_DATA_FLAG_SPECIAL | X_D3DRESOURCE_DATA_FLAG_SURFACE;

    EmuSwapFS();   // Xbox FS

    return pBackBuffer;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetBackBuffer
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_GetBackBuffer
(
    INT                 BackBuffer,
    D3DBACKBUFFER_TYPE  Type,
    X_D3DSurface      **ppBackBuffer
)
{
    // debug trace
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetBackBuffer\n"
               "(\n"
               "   BackBuffer          : 0x%.08X\n"
               "   Type                : 0x%.08X\n"
               "   ppBackBuffer        : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), BackBuffer, Type, ppBackBuffer);
        EmuSwapFS();   // Xbox FS
    }
    #endif

    *ppBackBuffer = EmuIDirect3DDevice8_GetBackBuffer2(BackBuffer);

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetViewport
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetViewport
(
#ifndef D3D9
    CONST D3DVIEWPORT8 *pViewport
#else
    CONST D3DVIEWPORT9 *pViewport
#endif
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetViewport\n"
           "(\n"
           "   pViewport           : 0x%.08X (%d, %d, %d, %d, %f, %f)\n"
           ");\n",
           GetCurrentThreadId(), pViewport, pViewport->X, pViewport->Y, pViewport->Width,
           pViewport->Height, pViewport->MinZ, pViewport->MaxZ);

    DWORD dwWidth  = pViewport->Width;
    DWORD dwHeight = pViewport->Height;

    // resize to fit screen (otherwise crashes occur)
    {
        if(dwWidth > 640)
        {
            EmuWarning("Resizing Viewport->Width to 640");
#ifndef D3D9
            ((D3DVIEWPORT8*)pViewport)->Width = 640;
#else
            ((D3DVIEWPORT9*)pViewport)->Width = 640;
#endif
        }

        if(dwHeight > 480)
        {
            EmuWarning("Resizing Viewport->Height to 480");
#ifndef D3D9
            ((D3DVIEWPORT8*)pViewport)->Height = 480;
#else
            ((D3DVIEWPORT9*)pViewport)->Height = 480;
#endif
        }
    }

    HRESULT hRet = g_pD3DDevice->SetViewport(pViewport);

    // restore originals
    {
        if(dwWidth > 640)
#ifndef D3D9
            ((D3DVIEWPORT8*)pViewport)->Width = dwWidth;
#else
            ((D3DVIEWPORT9*)pViewport)->Width = dwWidth;
#endif

        if(dwHeight > 480)
#ifndef D3D9
            ((D3DVIEWPORT8*)pViewport)->Height = dwHeight;
#else
            ((D3DVIEWPORT9*)pViewport)->Height = dwHeight;
#endif
    }

    if(FAILED(hRet))
    {
        EmuWarning("Unable to set viewport!");
        hRet = D3D_OK;
    }

    EmuSwapFS();   // Xbox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetViewport
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetViewport
(
#ifndef D3D9
    D3DVIEWPORT8 *pViewport
#else
    D3DVIEWPORT9 *pViewport
#endif
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetViewport\n"
           "(\n"
           "   pViewport           : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pViewport);

    HRESULT hRet = g_pD3DDevice->GetViewport(pViewport);

    if(FAILED(hRet))
    {
        EmuWarning("Unable to get viewport!");
        hRet = D3D_OK;
    }

    EmuSwapFS();   // Xbox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetViewportOffsetAndScale
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_GetViewportOffsetAndScale
(
    D3DXVECTOR4 *pOffset,
    D3DXVECTOR4 *pScale
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetViewportOffsetAndScale\n"
           "(\n"
           "   pOffset             : 0x%.08X\n"
           "   pScale              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(),pOffset,pScale);

    float fScaleX = 1.0f;
    float fScaleY = 1.0f;
    float fScaleZ = 1.0f;
    float fOffsetX = 0.5 + 1.0/32;
    float fOffsetY = 0.5 + 1.0/32;
#ifndef D3D9
    D3DVIEWPORT8 Viewport;
#else
    D3DVIEWPORT9 Viewport;
#endif

    EmuSwapFS();
    EmuIDirect3DDevice8_GetViewport(&Viewport);
    EmuSwapFS();


    pScale->x = 1.0f;
    pScale->y = 1.0f;
    pScale->z = 1.0f;
    pScale->w = 1.0f;

    pOffset->x = 0.0f;
    pOffset->y = 0.0f;
    pOffset->z = 0.0f;
    pOffset->w = 0.0f;

/*
    pScale->x = (float)Viewport.Width * 0.5f * fScaleX;
    pScale->y = (float)Viewport.Height * -0.5f * fScaleY;
    pScale->z = (Viewport.MaxZ - Viewport.MinZ) * fScaleZ;
    pScale->w = 0;

    pOffset->x = (float)Viewport.Width * fScaleX * 0.5f + (float)Viewport.X * fScaleX + fOffsetX;
    pOffset->y = (float)Viewport.Height * fScaleY * 0.5f + (float)Viewport.Y * fScaleY + fOffsetY;
    pOffset->z = Viewport.MinZ * fScaleZ;
    pOffset->w = 0;
*/

    EmuSwapFS();   // XBox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetShaderConstantMode
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetShaderConstantMode
(
    XTL::X_VERTEXSHADERCONSTANTMODE Mode
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetShaderConstantMode\n"
           "(\n"
           "   Mode                : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Mode);

    g_VertexShaderConstantMode = Mode;

    EmuSwapFS();   // Xbox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_Reset
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_Reset
(
    X_D3DPRESENT_PARAMETERS *pPresentationParameters
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_Reset\n"
           "(\n"
           "   pPresentationParameters  : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pPresentationParameters);

    EmuWarning("Device Reset is being utterly ignored");

    EmuSwapFS();   // Xbox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetRenderTarget
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetRenderTarget
(
    X_D3DSurface  **ppRenderTarget
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetRenderTarget\n"
           "(\n"
           "   ppRenderTarget      : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), ppRenderTarget);

#ifndef D3D9
    IDirect3DSurface8 *pSurface8 = g_pCachedRenderTarget->EmuSurface8;
#else
    IDirect3DSurface9 *pSurface8 = g_pCachedRenderTarget->EmuSurface8;
#endif

    pSurface8->AddRef();

    *ppRenderTarget = g_pCachedRenderTarget;

    DbgPrintf("EmuD3D8 (0x%X): RenderTarget := 0x%.08X\n", GetCurrentThreadId(), pSurface8);

    EmuSwapFS();   // Xbox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetRenderTarget2
// ******************************************************************
XTL::X_D3DSurface * WINAPI XTL::EmuIDirect3DDevice8_GetRenderTarget2()
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetRenderTarget2()\n", GetCurrentThreadId());

#ifndef D3D9
    IDirect3DSurface8 *pSurface8 = g_pCachedRenderTarget->EmuSurface8;
#else
    IDirect3DSurface9 *pSurface8 = g_pCachedRenderTarget->EmuSurface8;
#endif

    pSurface8->AddRef();

    DbgPrintf("EmuD3D8 (0x%X): RenderTarget := 0x%.08X\n", GetCurrentThreadId(), pSurface8);

    EmuSwapFS();   // Xbox FS

    return g_pCachedRenderTarget;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetDepthStencilSurface
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetDepthStencilSurface
(
    X_D3DSurface  **ppZStencilSurface
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetDepthStencilSurface\n"
           "(\n"
           "   ppZStencilSurface   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), ppZStencilSurface);

#ifndef D3D9
    IDirect3DSurface8 *pSurface8 = g_pCachedZStencilSurface->EmuSurface8;
#else
    IDirect3DSurface9 *pSurface8 = g_pCachedZStencilSurface->EmuSurface8;
#endif

    if(pSurface8 != 0)
        pSurface8->AddRef();

    *ppZStencilSurface = g_pCachedZStencilSurface;

    DbgPrintf("EmuD3D8 (0x%X): DepthStencilSurface := 0x%.08X\n", GetCurrentThreadId(), pSurface8);

    EmuSwapFS();   // Xbox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetDepthStencilSurface2
// ******************************************************************
XTL::X_D3DSurface * WINAPI XTL::EmuIDirect3DDevice8_GetDepthStencilSurface2()
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetDepthStencilSurface2()\n", GetCurrentThreadId());

#ifndef D3D9
    IDirect3DSurface8 *pSurface8 = g_pCachedZStencilSurface->EmuSurface8;
#else
    IDirect3DSurface9 *pSurface8 = g_pCachedZStencilSurface->EmuSurface8;
#endif

    if(pSurface8 != 0)
        pSurface8->AddRef();

    DbgPrintf("EmuD3D8 (0x%X): DepthStencilSurface := 0x%.08X\n", GetCurrentThreadId(), pSurface8);

    EmuSwapFS();   // Xbox FS

    return g_pCachedZStencilSurface;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetTile
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetTile
(
    DWORD           Index,
    X_D3DTILE      *pTile
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetTile\n"
           "(\n"
           "   Index               : 0x%.08X\n"
           "   pTile               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Index, pTile);

    if(pTile != NULL)
        memcpy(pTile, &EmuD3DTileCache[Index], sizeof(X_D3DTILE));

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTileNoWait
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetTileNoWait
(
    DWORD               Index,
    CONST X_D3DTILE    *pTile
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetTileNoWait\n"
           "(\n"
           "   Index               : 0x%.08X\n"
           "   pTile               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Index, pTile);

    if(pTile != NULL)
        memcpy(&EmuD3DTileCache[Index], pTile, sizeof(X_D3DTILE));

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateVertexShader
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateVertexShader
(
    CONST DWORD    *pDeclaration,
    CONST DWORD    *pFunction,
    DWORD          *pHandle,
    DWORD           Usage
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreateVertexShader\n"
           "(\n"
           "   pDeclaration        : 0x%.08X\n"
           "   pFunction           : 0x%.08X\n"
           "   pHandle             : 0x%.08X\n"
           "   Usage               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pDeclaration, pFunction, pHandle, Usage);

#ifndef D3D9
    // create emulated shader struct
    X_D3DVertexShader *pD3DVertexShader = (X_D3DVertexShader*)CxbxMalloc(sizeof(X_D3DVertexShader));
    VERTEX_SHADER     *pVertexShader = (VERTEX_SHADER*)CxbxMalloc(sizeof(VERTEX_SHADER));

    // TODO: Intelligently fill out these fields as necessary
    ZeroMemory(pD3DVertexShader, sizeof(X_D3DVertexShader));
    ZeroMemory(pVertexShader, sizeof(VERTEX_SHADER));

    // HACK: TODO: support this situation
    if(pDeclaration == NULL)
    {
        *pHandle = NULL;

        EmuSwapFS();   // Win2k/XP FS

        return S_OK;
    }

    LPD3DXBUFFER pRecompiledBuffer = NULL;
    DWORD        *pRecompiledDeclaration;
    DWORD        *pRecompiledFunction = NULL;
    DWORD        VertexShaderSize = 0;
    DWORD        DeclarationSize;
    DWORD        Handle = 0;

    HRESULT hRet = XTL::EmuRecompileVshDeclaration((DWORD*)pDeclaration,
                                                   &pRecompiledDeclaration,
                                                   &DeclarationSize,
                                                   pFunction == NULL,
                                                   &pVertexShader->VertexDynamicPatch);

    if(SUCCEEDED(hRet) && pFunction)
    {
		boolean bUseDeclarationOnly = 0;

        hRet = XTL::EmuRecompileVshFunction((DWORD*)pFunction,
                                            &pRecompiledBuffer,
                                            &VertexShaderSize,
                                            g_VertexShaderConstantMode == X_VSCM_NONERESERVED,
											&bUseDeclarationOnly);
        if(SUCCEEDED(hRet))
        {
			if(!bUseDeclarationOnly)
				pRecompiledFunction = (DWORD*)pRecompiledBuffer->GetBufferPointer();
			else
				pRecompiledFunction = NULL;
        }
        else
        {
            pRecompiledFunction = NULL;
            EmuWarning("Couldn't recompile vertex shader function.\n");
            hRet = D3D_OK; // Try using a fixed function vertex shader instead
        }
    }

    //DbgPrintf("MaxVertexShaderConst = %d\n", g_D3DCaps.MaxVertexShaderConst);

    if(SUCCEEDED(hRet))
    {
        hRet = g_pD3DDevice->CreateVertexShader
        (
            pRecompiledDeclaration,
            pRecompiledFunction,
            &Handle,
            g_dwVertexShaderUsage   // TODO: HACK: Xbox has extensions!
        );
        if(pRecompiledBuffer)
        {
            pRecompiledBuffer->Release();
            pRecompiledBuffer = NULL;
        }

        //* Fallback to dummy shader.
        if(FAILED(hRet))
        {
            static const char dummy[] =
                "vs.1.1\n"
                "mov oPos, v0\n";

            EmuWarning("Trying fallback:\n%s\n", dummy);
            hRet = D3DXAssembleShader(dummy,
                                      strlen(dummy),
                                      D3DXASM_SKIPVALIDATION,
                                      NULL,
                                      &pRecompiledBuffer,
                                      NULL);
            hRet = g_pD3DDevice->CreateVertexShader
            (
                pRecompiledDeclaration,
                (DWORD*)pRecompiledBuffer->GetBufferPointer(),
                &Handle,
                g_dwVertexShaderUsage
            );
        }
        //*/
    }
    // Save the status, to remove things later
    pVertexShader->Status = hRet;

    CxbxFree(pRecompiledDeclaration);

    pVertexShader->pDeclaration = (DWORD*)CxbxMalloc(DeclarationSize);
    memcpy(pVertexShader->pDeclaration, pDeclaration, DeclarationSize);

    pVertexShader->FunctionSize = 0;
    pVertexShader->pFunction = NULL;
    pVertexShader->Type = X_VST_NORMAL;
    pVertexShader->Size = (VertexShaderSize - sizeof(VSH_SHADER_HEADER)) / VSH_INSTRUCTION_SIZE_BYTES;
    pVertexShader->DeclarationSize = DeclarationSize;

    if(SUCCEEDED(hRet))
    {
        if(pFunction != NULL)
        {
            pVertexShader->pFunction = (DWORD*)CxbxMalloc(VertexShaderSize);
            memcpy(pVertexShader->pFunction, pFunction, VertexShaderSize);
            pVertexShader->FunctionSize = VertexShaderSize;
        }
        else
        {
            pVertexShader->pFunction = NULL;
            pVertexShader->FunctionSize = 0;
        }
        pVertexShader->Handle = Handle;
    }
    else
    {
        pVertexShader->Handle = D3DFVF_XYZ | D3DFVF_TEX0;
    }

    pD3DVertexShader->Handle = (DWORD)pVertexShader;

    *pHandle = ((DWORD)pD3DVertexShader) | 0x80000000;

    if(FAILED(hRet))
    {
#ifdef _DEBUG_TRACK_VS
        if (pFunction)
        {
            char pFileName[30];
            static int FailedShaderCount = 0;
            VSH_SHADER_HEADER *pHeader = (VSH_SHADER_HEADER*)pFunction;
            EmuWarning("Couldn't create vertex shader!");
            sprintf(pFileName, "failed%05d.xvu", FailedShaderCount);
            FILE *f = fopen(pFileName, "wb");
            if(f)
            {
                fwrite(pFunction, sizeof(VSH_SHADER_HEADER) + pHeader->NumInst * 16, 1, f);
                fclose(f);
            }
            FailedShaderCount++;
        }
#endif // _DEBUG_TRACK_VS
        //hRet = D3D_OK;
    }
#endif

    EmuSwapFS();   // XBox FS

#ifndef D3D9
    return hRet;
#else
    return S_OK; //hRet;
#endif
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetPixelShaderConstant
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetPixelShaderConstant
(
    DWORD       Register,
    CONST PVOID pConstantData,
    DWORD       ConstantCount
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetPixelShaderConstant\n"
           "(\n"
           "   Register            : 0x%.08X\n"
           "   pConstantData       : 0x%.08X\n"
           "   ConstantCount       : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Register, pConstantData, ConstantCount);

	// TODO: This hack is necessary for Vertex Shaders on XDKs prior to 4361, but if this
	// causes problems with pixel shaders, feel free to comment out the hack below.
	if(g_BuildVersion <= 4361)
		Register += 96;

#ifndef D3D9
    HRESULT hRet = g_pD3DDevice->SetPixelShaderConstant
#else
    HRESULT hRet = g_pD3DDevice->SetPixelShaderConstantF
#endif
    (
        Register,
#ifndef D3D9
        pConstantData,
#else
        (float*) pConstantData,
#endif
        ConstantCount
    );

    if(FAILED(hRet))
    {
        EmuWarning("We're lying about setting a pixel shader constant!");

        hRet = D3D_OK;
    }

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexShaderConstant
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetVertexShaderConstant
(
    INT         Register,
    CONST PVOID pConstantData,
    DWORD       ConstantCount
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexShaderConstant\n"
           "(\n"
           "   Register            : 0x%.08X\n"
           "   pConstantData       : 0x%.08X\n"
           "   ConstantCount       : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Register, pConstantData, ConstantCount);

/*#ifdef _DEBUG_TRACK_VS_CONST
    for (uint32 i = 0; i < ConstantCount; i++)
    {
        printf("SetVertexShaderConstant, c%d (c%d) = { %f, %f, %f, %f }\n",
               Register - 96 + i, Register + i,
               *((float*)pConstantData + 4 * i),
               *((float*)pConstantData + 4 * i + 1),
               *((float*)pConstantData + 4 * i + 2),
               *((float*)pConstantData + 4 * i + 3));
    }
#endif*/ // _DEBUG_TRACK_VS_CONST

	// TODO: HACK: Since Xbox vertex shader constants range from -96 to 96, during conversion
	// some shaders need to add 96 to use ranges 0 to 192.  This fixes 3911 - 4361 games and XDK
	// samples, but breaks Turok.

	if(g_BuildVersion <= 4361)
		Register += 96;

#ifndef D3D9
    HRESULT hRet = g_pD3DDevice->SetVertexShaderConstant
#else
    HRESULT hRet = g_pD3DDevice->SetVertexShaderConstantF
#endif
    (
        Register,
#ifndef D3D9
        pConstantData,
#else
        (float*) pConstantData,
#endif
        ConstantCount
    );

    if(FAILED(hRet))
    {
        EmuWarning("We're lying about setting a vertex shader constant!");

        hRet = D3D_OK;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexShaderConstant1
// ******************************************************************
VOID __fastcall XTL::EmuIDirect3DDevice8_SetVertexShaderConstant1
(
    INT         Register,
    CONST PVOID pConstantData
)
{
    // debug trace
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexShaderConstant1\n"
               "(\n"
               "   Register            : 0x%.08X\n"
               "   pConstantData       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Register, pConstantData);
        EmuSwapFS();   // XBox FS
    }
    #endif

    XTL::EmuIDirect3DDevice8_SetVertexShaderConstant(Register, pConstantData, 1);

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexShaderConstant4
// ******************************************************************
VOID __fastcall XTL::EmuIDirect3DDevice8_SetVertexShaderConstant4
(
    INT         Register,
    CONST PVOID pConstantData
)
{
    // debug trace
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexShaderConstant4\n"
               "(\n"
               "   Register            : 0x%.08X\n"
               "   pConstantData       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Register, pConstantData);
        EmuSwapFS();   // XBox FS
    }
    #endif

    XTL::EmuIDirect3DDevice8_SetVertexShaderConstant(Register, pConstantData, 4);

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexShaderConstantNotInline
// ******************************************************************
VOID __fastcall XTL::EmuIDirect3DDevice8_SetVertexShaderConstantNotInline
(
    INT         Register,
    CONST PVOID pConstantData,
    DWORD       ConstantCount
)
{
    // debug trace
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexShaderConstantNotInline\n"
               "(\n"
               "   Register            : 0x%.08X\n"
               "   pConstantData       : 0x%.08X\n"
               "   ConstantCount       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Register, pConstantData, ConstantCount);
        EmuSwapFS();   // XBox FS
    }
    #endif

    XTL::EmuIDirect3DDevice8_SetVertexShaderConstant(Register, pConstantData, ConstantCount / 4);

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_DeletePixelShader
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_DeletePixelShader
(
    DWORD          Handle
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_DeletePixelShader\n"
           "(\n"
           "   Handle              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Handle);

#ifndef D3D9
    if(Handle == X_PIXELSHADER_FAKE_HANDLE)
    {
        // Do Nothing!
    }
    else
    {
        g_pD3DDevice->DeletePixelShader(Handle);
    }

	/*PIXEL_SHADER *pPixelShader = (PIXEL_SHADER*)Handle;

	if (pPixelShader)
	{
		if(pPixelShader->Handle != X_PIXELSHADER_FAKE_HANDLE)
		{
			g_pD3DDevice->DeletePixelShader(pPixelShader->Handle);
		}
		CxbxFree(pPixelShader);
	}*/
#endif

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreatePixelShader
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreatePixelShader
(
    X_D3DPIXELSHADERDEF    *pPSDef,
    DWORD				   *pHandle
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreatePixelShader\n"
           "(\n"
           "   pPSDef              : 0x%.08X\n"
           "   pHandle             : 0x%.08X (0x%.08X)\n"
           ");\n",
           GetCurrentThreadId(), pPSDef, pHandle, *pHandle);

#ifndef D3D9
	HRESULT hRet = E_FAIL;
	DWORD* pFunction = NULL;
	LPD3DXBUFFER pRecompiledBuffer = NULL;
	// DWORD Handle = 0;

	hRet = CreatePixelShaderFunction(pPSDef, &pRecompiledBuffer);

	if (SUCCEEDED(hRet))
	{
		pFunction = (DWORD*)pRecompiledBuffer->GetBufferPointer();

		// Redirect to Windows D3D
		hRet = g_pD3DDevice->CreatePixelShader
		(
			pFunction,
			pHandle
			/*&Handle*/
		);
	}

	if (pRecompiledBuffer)
	{
		pRecompiledBuffer->Release();
	}

	// This additional layer of Cxbx internal indirection seems problematic, as 
	// CreatePixelShader() is expected to return a pHandle directly to a shader interface.

	/*
	PIXEL_SHADER *pPixelShader = (PIXEL_SHADER*)CxbxMalloc(sizeof(PIXEL_SHADER));
	ZeroMemory(pPixelShader, sizeof(PIXEL_SHADER));

	memcpy(&pPixelShader->PSDef, pPSDef, sizeof(X_D3DPIXELSHADERDEF));

	pPixelShader->Handle = Handle;
	pPixelShader->dwStatus = hRet;
	*pHandle = (DWORD)pPixelShader;
	*/

#if 0
    // ================================================================
	pFunction = (DWORD*) pPSDef;

	// Attempt to recompile PixelShader
	EmuRecompilePshDef( pPSDef, NULL );

    // redirect to windows d3d
    hRet = g_pD3DDevice->CreatePixelShader
    (
        pFunction,
        pHandle
    );
    // ================================================================
#endif

    if(FAILED(hRet))
    {
        *pHandle = X_PIXELSHADER_FAKE_HANDLE;

		// This is called too frequently as Azurik creates and destroys a
		// pixel shader every frame, and makes debugging harder.
		// EmuWarning("We're lying about the creation of a pixel shader!");

        hRet = D3D_OK;
    }
    else
    {
        DbgPrintf("pHandle = 0x%.08X (0x%.08X)\n", pHandle, *pHandle);
    }
#endif

    EmuSwapFS();   // XBox FS

#ifndef D3D9
    return hRet;
#else
    return S_OK; //hRet;
#endif
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetPixelShader
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetPixelShader
(
    DWORD           Handle
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetPixelShader\n"
           "(\n"
           "   Handle              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Handle);

    // Redirect to Windows D3D
    HRESULT hRet = D3D_OK;

#ifndef D3D9
    // Fake Programmable Pipeline
    if(Handle == X_PIXELSHADER_FAKE_HANDLE)
    {
        // programmable pipeline
        //*
        static DWORD dwHandle = 0;

        if(dwHandle == 0)
        {
            // simplest possible pixel shader, simply output the texture input
            static const char szDiffusePixelShader[] =
                "ps.1.0\n"
                "tex t0\n"
                "mov r0, t0\n";

            LPD3DXBUFFER pShader = 0;
            LPD3DXBUFFER pErrors = 0;

            // assemble the shader
            D3DXAssembleShader(szDiffusePixelShader, strlen(szDiffusePixelShader) - 1, 0, NULL, &pShader, &pErrors);

            // create the shader device handle
            hRet = g_pD3DDevice->CreatePixelShader((DWORD*)pShader->GetBufferPointer(), &dwHandle);

			g_dwCurrentPixelShader = 0;

            if(FAILED(hRet))
                EmuWarning("Could not create pixel shader");
        }

        if(!FAILED(hRet))
            hRet = g_pD3DDevice->SetPixelShader(dwHandle);

        if(FAILED(hRet))
            EmuWarning("Could not set pixel shader!");
        //*/

        g_bFakePixelShaderLoaded = TRUE;
    }
    // Fixed Pipeline, or Recompiled Programmable Pipeline
    else if(Handle != NULL)
    {
        EmuWarning("Trying fixed or recompiled programmable pipeline pixel shader!");
        g_bFakePixelShaderLoaded = FALSE;
		g_dwCurrentPixelShader = Handle;
        hRet = g_pD3DDevice->SetPixelShader(Handle);
    }

    if(FAILED(hRet))
    {
        EmuWarning("We're lying about setting a pixel shader!");

        hRet = D3D_OK;
    }
#endif

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateTexture2
// ******************************************************************
XTL::X_D3DResource * WINAPI XTL::EmuIDirect3DDevice8_CreateTexture2
(
    UINT                Width,
    UINT                Height,
    UINT                Depth,
    UINT                Levels,
    DWORD               Usage,
    D3DFORMAT           Format,
    D3DRESOURCETYPE     D3DResource
)
{
    X_D3DTexture *pTexture = NULL;

    switch(D3DResource)
    {
        case 3: /*D3DRTYPE_TEXTURE*/
            EmuIDirect3DDevice8_CreateTexture(Width, Height, Levels, Usage, Format, D3DPOOL_MANAGED, &pTexture);
            break;
        case 4: /*D3DRTYPE_VOLUMETEXTURE*/
            EmuIDirect3DDevice8_CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, D3DPOOL_MANAGED, (X_D3DVolumeTexture**)&pTexture);
            break;
        case 5: /*D3DRTYPE_CUBETEXTURE*/
            //DbgPrintf( "D3DDevice_CreateTexture2: Width = 0x%X, Height = 0x%X\n", Width, Height );
			//CxbxKrnlCleanup("Cube textures temporarily not supported!");
			EmuIDirect3DDevice8_CreateCubeTexture(Width, Levels, Usage, Format, D3DPOOL_MANAGED, (X_D3DCubeTexture**) &pTexture);
            break;
        default:
            CxbxKrnlCleanup("D3DResource = %d is not supported!", D3DResource);
    }

    return pTexture;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateTexture
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateTexture
(
    UINT            Width,
    UINT            Height,
    UINT            Levels,
    DWORD           Usage,
    D3DFORMAT       Format,
    D3DPOOL         Pool,
    X_D3DTexture  **ppTexture
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreateTexture\n"
           "(\n"
           "   Width               : 0x%.08X\n"
           "   Height              : 0x%.08X\n"
           "   Levels              : 0x%.08X\n"
           "   Usage               : 0x%.08X\n"
           "   Format              : 0x%.08X\n"
           "   Pool                : 0x%.08X\n"
           "   ppTexture           : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Width, Height, Levels, Usage, Format, Pool, ppTexture);

    // Convert Format (Xbox->PC)
    D3DFORMAT PCFormat = EmuXB2PC_D3DFormat(Format);

    // TODO: HACK: Devices that don't support this should somehow emulate it!
    //* This is OK on my GeForce FX 5600
    if(PCFormat == D3DFMT_D16)
    {
        EmuWarning("D3DFMT_D16 is an unsupported texture format!");
        PCFormat = D3DFMT_R5G6B5;
    }
    //*
    else if(PCFormat == D3DFMT_P8)
    {
        EmuWarning("D3DFMT_P8 is an unsupported texture format!");
        PCFormat = D3DFMT_L8;
    }
    //*/
    //* This is OK on my GeForce FX 5600
    else if(PCFormat == D3DFMT_D24S8)
    {
        EmuWarning("D3DFMT_D24S8 is an unsupported texture format!");
        PCFormat = D3DFMT_X8R8G8B8;
    }//*/
    else if(PCFormat == D3DFMT_YUY2)
    {
        // cache the overlay size
        g_dwOverlayW = Width;
        g_dwOverlayH = Height;
        g_dwOverlayP = RoundUp(g_dwOverlayW, 64)*2;
    }

    HRESULT hRet;

    if(PCFormat != D3DFMT_YUY2)
    {
        DWORD   PCUsage = Usage & (D3DUSAGE_RENDERTARGET);
//        DWORD   PCUsage = Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL);
        D3DPOOL PCPool  = D3DPOOL_MANAGED;

		// DIRTY HACK: Render targets. The D3DUSAGE_RENDERTARGET
		// flag isn't always set by the XDK (if ever).
		/*if( Width != 640 && Height != 480 )
		{
		//	EmuAdjustPower2(&Width, &Height);
		}
		else
		{
			PCUsage = D3DUSAGE_RENDERTARGET;
			PCPool = D3DPOOL_DEFAULT;
		}*/

//		EmuAdjustPower2(&Width, &Height);

        *ppTexture = new X_D3DTexture();

//        if(Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
        if(Usage & (D3DUSAGE_RENDERTARGET))
        {
			PCPool = D3DPOOL_DEFAULT;
        }

        hRet = g_pD3DDevice->CreateTexture
        (
            Width, Height, Levels,
            PCUsage,  // TODO: Xbox Allows a border to be drawn (maybe hack this in software ;[)
#ifndef D3D9
            PCFormat, PCPool, &((*ppTexture)->EmuTexture8)
#else
            PCFormat, PCPool, &((*ppTexture)->EmuTexture8), NULL
#endif
        );

        if(FAILED(hRet))
        {
            EmuWarning("CreateTexture Failed!");
            (*ppTexture)->Data = 0xBEADBEAD;
        }
        else
        {
            D3DLOCKED_RECT LockedRect;

            /**
             * Note: If CreateTexture() called with D3DPOOL_DEFAULT then unable to Lock. 
             * It will cause an Error with the DirectX Debug runtime.
             *
             * This is most commonly seen with
             *      D3DUSAGE_RENDERTARGET or
             *      D3DUSAGE_DEPTHSTENCIL
             * that can only be used with D3DPOOL_DEFAULT per MSDN.
             */
            (*ppTexture)->EmuTexture8->LockRect(0, &LockedRect, NULL, NULL);

            (*ppTexture)->Data = (DWORD)LockedRect.pBits;
            (*ppTexture)->Format = Format << X_D3DFORMAT_FORMAT_SHIFT;

            g_DataToTexture.insert((*ppTexture)->Data, *ppTexture);

            (*ppTexture)->EmuTexture8->UnlockRect(0);
        }

        DbgPrintf("EmuD3D8 (0x%X): Created Texture : 0x%.08X (0x%.08X)\n", GetCurrentThreadId(), *ppTexture, (*ppTexture)->EmuTexture8);
    }
    else
    {
        DWORD dwSize = g_dwOverlayP*g_dwOverlayH;
        DWORD dwPtr = (DWORD)CxbxMalloc(dwSize + sizeof(DWORD));

        DWORD *pRefCount = (DWORD*)(dwPtr + dwSize);

        // initialize ref count
        *pRefCount = 1;

        // If YUY2 is not supported in hardware, we'll actually mark this as a special fake texture (set highest bit)
        *ppTexture = new X_D3DTexture();

        (*ppTexture)->Data = X_D3DRESOURCE_DATA_FLAG_SPECIAL | X_D3DRESOURCE_DATA_FLAG_YUVSURF;
        (*ppTexture)->Lock = dwPtr;
        (*ppTexture)->Format = 0x24;

        (*ppTexture)->Size  = (g_dwOverlayW & X_D3DSIZE_WIDTH_MASK);
        (*ppTexture)->Size |= (g_dwOverlayH << X_D3DSIZE_HEIGHT_SHIFT);
        (*ppTexture)->Size |= (g_dwOverlayP << X_D3DSIZE_PITCH_SHIFT);

        g_YuvSurface = (X_D3DSurface*)*ppTexture;

        hRet = D3D_OK;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateVolumeTexture
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateVolumeTexture
(
    UINT                 Width,
    UINT                 Height,
    UINT                 Depth,
    UINT                 Levels,
    DWORD                Usage,
    D3DFORMAT            Format,
    D3DPOOL              Pool,
    X_D3DVolumeTexture **ppVolumeTexture
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreateVolumeTexture\n"
           "(\n"
           "   Width               : 0x%.08X\n"
           "   Height              : 0x%.08X\n"
           "   Depth               : 0x%.08X\n"
           "   Levels              : 0x%.08X\n"
           "   Usage               : 0x%.08X\n"
           "   Format              : 0x%.08X\n"
           "   Pool                : 0x%.08X\n"
           "   ppVolumeTexture     : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture);

    // Convert Format (Xbox->PC)
    D3DFORMAT PCFormat = EmuXB2PC_D3DFormat(Format);

    // TODO: HACK: Devices that don't support this should somehow emulate it!
    if(PCFormat == D3DFMT_D16)
    {
        EmuWarning("D3DFMT_16 is an unsupported texture format!");
        PCFormat = D3DFMT_X8R8G8B8;
    }
    else if(PCFormat == D3DFMT_P8)
    {
        EmuWarning("D3DFMT_P8 is an unsupported texture format!");
        PCFormat = D3DFMT_L8;
    }
    else if(PCFormat == D3DFMT_D24S8)
    {
        EmuWarning("D3DFMT_D24S8 is an unsupported texture format!");
        PCFormat = D3DFMT_X8R8G8B8;
    }
    else if(PCFormat == D3DFMT_YUY2)
    {
        // cache the overlay size
        g_dwOverlayW = Width;
        g_dwOverlayH = Height;
        g_dwOverlayP = RoundUp(g_dwOverlayW, 64)*2;
    }

    HRESULT hRet;

    if(PCFormat != D3DFMT_YUY2)
    {
        EmuAdjustPower2(&Width, &Height);

        *ppVolumeTexture = new X_D3DVolumeTexture();

        hRet = g_pD3DDevice->CreateVolumeTexture
        (
            Width, Height, Depth, Levels,
            0,  // TODO: Xbox Allows a border to be drawn (maybe hack this in software ;[)
#ifndef D3D9
            PCFormat, D3DPOOL_MANAGED, &((*ppVolumeTexture)->EmuVolumeTexture8)
#else
            PCFormat, D3DPOOL_MANAGED, &((*ppVolumeTexture)->EmuVolumeTexture8), NULL
#endif
        );

        if(FAILED(hRet))
            EmuWarning("CreateVolumeTexture Failed! (0x%.08X)", hRet);

        DbgPrintf("EmuD3D8 (0x%X): Created Volume Texture : 0x%.08X (0x%.08X)\n", GetCurrentThreadId(), *ppVolumeTexture, (*ppVolumeTexture)->EmuVolumeTexture8);
    }
    else
    {
        DWORD dwSize = g_dwOverlayP*g_dwOverlayH;
        DWORD dwPtr = (DWORD)CxbxMalloc(dwSize + sizeof(DWORD));

        DWORD *pRefCount = (DWORD*)(dwPtr + dwSize);

        // initialize ref count
        *pRefCount = 1;

        // If YUY2 is not supported in hardware, we'll actually mark this as a special fake texture (set highest bit)
        (*ppVolumeTexture)->Data = X_D3DRESOURCE_DATA_FLAG_SPECIAL | X_D3DRESOURCE_DATA_FLAG_YUVSURF;
        (*ppVolumeTexture)->Lock = dwPtr;
        (*ppVolumeTexture)->Format = 0x24;

        (*ppVolumeTexture)->Size  = (g_dwOverlayW & X_D3DSIZE_WIDTH_MASK);
        (*ppVolumeTexture)->Size |= (g_dwOverlayH << X_D3DSIZE_HEIGHT_SHIFT);
        (*ppVolumeTexture)->Size |= (g_dwOverlayP << X_D3DSIZE_PITCH_SHIFT);

        hRet = D3D_OK;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateCubeTexture
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateCubeTexture
(
    UINT                 EdgeLength,
    UINT                 Levels,
    DWORD                Usage,
    D3DFORMAT            Format,
    D3DPOOL              Pool,
    X_D3DCubeTexture  **ppCubeTexture
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreateCubeTexture\n"
           "(\n"
           "   EdgeLength          : 0x%.08X\n"
           "   Levels              : 0x%.08X\n"
           "   Usage               : 0x%.08X\n"
           "   Format              : 0x%.08X\n"
           "   Pool                : 0x%.08X\n"
           "   ppCubeTexture       : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture);

    // Convert Format (Xbox->PC)
    D3DFORMAT PCFormat = EmuXB2PC_D3DFormat(Format);

    // TODO: HACK: Devices that don't support this should somehow emulate it!
    if(PCFormat == D3DFMT_D16)
    {
        EmuWarning("D3DFMT_16 is an unsupported texture format!");
        PCFormat = D3DFMT_X8R8G8B8;
    }
    else if(PCFormat == D3DFMT_P8)
    {
        EmuWarning("D3DFMT_P8 is an unsupported texture format!");
        PCFormat = D3DFMT_L8;
    }
    else if(PCFormat == D3DFMT_D24S8)
    {
        EmuWarning("D3DFMT_D24S8 is an unsupported texture format!");
        PCFormat = D3DFMT_X8R8G8B8;
    }
    else if(PCFormat == D3DFMT_YUY2)
    {
        CxbxKrnlCleanup("YUV not supported for cube textures");
    }

    *ppCubeTexture = new X_D3DCubeTexture();

    HRESULT hRet = g_pD3DDevice->CreateCubeTexture
    (
        EdgeLength, Levels,
        0,  // TODO: Xbox Allows a border to be drawn (maybe hack this in software ;[)
#ifndef D3D9
        PCFormat, D3DPOOL_MANAGED, &((*ppCubeTexture)->EmuCubeTexture8)
#else
        PCFormat, D3DPOOL_MANAGED, &((*ppCubeTexture)->EmuCubeTexture8), NULL
#endif
    );

    DbgPrintf("EmuD3D8 (0x%X): Created Cube Texture : 0x%.08X (0x%.08X)\n", GetCurrentThreadId(), *ppCubeTexture, (*ppCubeTexture)->EmuCubeTexture8);

    if(FAILED(hRet))
        EmuWarning("CreateCubeTexture Failed!");

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateIndexBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateIndexBuffer
(
    UINT                 Length,
    DWORD                Usage,
    D3DFORMAT            Format,
    D3DPOOL              Pool,
    X_D3DIndexBuffer   **ppIndexBuffer
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreateIndexBuffer\n"
           "(\n"
           "   Length              : 0x%.08X\n"
           "   Usage               : 0x%.08X\n"
           "   Format              : 0x%.08X\n"
           "   Pool                : 0x%.08X\n"
           "   ppIndexBuffer       : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Length, Usage, Format, Pool, ppIndexBuffer);

    *ppIndexBuffer = new X_D3DIndexBuffer();

    HRESULT hRet = g_pD3DDevice->CreateIndexBuffer
    (
#ifndef D3D9
        Length, NULL, D3DFMT_INDEX16, D3DPOOL_MANAGED, &((*ppIndexBuffer)->EmuIndexBuffer8)
#else
        Length, NULL, D3DFMT_INDEX16, D3DPOOL_MANAGED, &((*ppIndexBuffer)->EmuIndexBuffer8), NULL
#endif
    );

    DbgPrintf("EmuD3D8 (0x%X): EmuIndexBuffer8 := 0x%.08X\n", GetCurrentThreadId(), (*ppIndexBuffer)->EmuIndexBuffer8);

    if(FAILED(hRet))
        EmuWarning("CreateIndexBuffer Failed! (0x%.08X)", hRet);

    //
    // update data ptr
    //

    {
        BYTE *pData = NULL;

#ifndef D3D9
        (*ppIndexBuffer)->EmuIndexBuffer8->Lock(0, Length, &pData, NULL);
#else
        (*ppIndexBuffer)->EmuIndexBuffer8->Lock(0, Length, (void**)&pData, NULL);
#endif

        (*ppIndexBuffer)->Data = (DWORD)pData;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateIndexBuffer2
// ******************************************************************
XTL::X_D3DIndexBuffer * WINAPI XTL::EmuIDirect3DDevice8_CreateIndexBuffer2(UINT Length)
{
    X_D3DIndexBuffer *pIndexBuffer = NULL;

    EmuIDirect3DDevice8_CreateIndexBuffer
    (
        Length,
        NULL,
        D3DFMT_INDEX16,
        D3DPOOL_MANAGED,
        &pIndexBuffer
    );

    return pIndexBuffer;
}

BOOL g_bBadIndexData = FALSE;

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetIndices
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetIndices
(
    X_D3DIndexBuffer   *pIndexData,
    UINT                BaseVertexIndex
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetIndices\n"
           "(\n"
           "   pIndexData          : 0x%.08X\n"
           "   BaseVertexIndex     : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pIndexData, BaseVertexIndex);

    /*
    fflush(stdout);
    if(pIndexData != 0)
    {
        static int chk = 0;
        if(chk++ == 0)
        {
            _asm int 3
        }
    }
    //*/

    HRESULT hRet = D3D_OK;

//#if 0
	if(pIndexData != NULL)
	{
		DbgPrintf("EmuIDirect3DDevice8_SetIndcies(): pIndexData->EmuIndexBuffer8:= 0x%.08X\n", pIndexData->EmuIndexBuffer8 );
		DbgPrintf("EmuIDirect3DDevice8_SetIndcies(): pIndexData->Lock:= 0x%.08X\n", pIndexData->Lock );
	}

    g_dwBaseVertexIndex = BaseVertexIndex;

    if(pIndexData != 0)
    {
        g_pIndexBuffer = pIndexData;

        // HACK: Halo Hack
        if(pIndexData->Lock == 0x00840863)
            pIndexData->Lock = 0;

        EmuVerifyResourceIsRegistered(pIndexData);

		// HACK: Unreal Championship
		if((pIndexData->Lock & 0xFFFF0000) == 0x00490000 || (pIndexData->Lock & 0xF0000000) != 0x00000000 || 
			pIndexData->Lock == 0x10)
		{
			hRet = E_FAIL;
			goto fail;
		}

#ifndef D3D9
        IDirect3DIndexBuffer8 *pIndexBuffer = pIndexData->EmuIndexBuffer8;
#else
        IDirect3DIndexBuffer9 *pIndexBuffer = pIndexData->EmuIndexBuffer8;
#endif

        if(pIndexData->Lock != X_D3DRESOURCE_LOCK_FLAG_NOSIZE)
#ifndef D3D9
            hRet = g_pD3DDevice->SetIndices(pIndexBuffer, BaseVertexIndex);
#else
            hRet = g_pD3DDevice->SetIndices(pIndexBuffer);
#endif
    }
    else
    {
        g_pIndexBuffer = 0;

#ifndef D3D9
        hRet = g_pD3DDevice->SetIndices(0, BaseVertexIndex);
#else
        hRet = g_pD3DDevice->SetIndices(0);
#endif
    }
//#endif
fail:
    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTexture
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetTexture
(
    DWORD           Stage,
    X_D3DResource  *pTexture
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetTexture\n"
           "(\n"
           "   Stage               : 0x%.08X\n"
           "   pTexture            : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Stage, pTexture);

#ifndef D3D9
    IDirect3DBaseTexture8 *pBaseTexture8 = NULL;
#else
    IDirect3DBaseTexture9 *pBaseTexture8 = NULL;
#endif

    EmuD3DActiveTexture[Stage] = pTexture;

    if(pTexture != NULL)
    {
        EmuVerifyResourceIsRegistered(pTexture);

        if(IsSpecialResource(pTexture->Data) && (pTexture->Data & X_D3DRESOURCE_DATA_FLAG_YUVSURF))
        {
            //
            // NOTE: TODO: This is almost a hack! :)
            //

            EmuSwapFS();
            EmuIDirect3DDevice8_EnableOverlay(TRUE);
            EmuIDirect3DDevice8_UpdateOverlay((X_D3DSurface*)pTexture, 0, 0, FALSE, 0);
            EmuSwapFS();
        }
        else
        {			
			// Remove old locks before setting
			/*if(pTexture->Common & X_D3DCOMMON_ISLOCKED)
			{
				pTexture->EmuTexture8->UnlockRect(0);
				pTexture->Common &= ~X_D3DCOMMON_ISLOCKED;
			}*/

            pBaseTexture8 = pTexture->EmuBaseTexture8;

			// Let's be SURE that the texture is unlocked AND unswizzled before
			// we set it!!!
		//	EmuUnswizzleTextureStages();
		//	pBaseTexture8 = EmuD3DActiveTexture[Stage]->EmuBaseTexture8;

            #ifdef _DEBUG_DUMP_TEXTURE_SETTEXTURE
            if(pTexture != NULL && (pTexture->EmuTexture8 != NULL))
            {
                static int dwDumpTexture = 0;

                char szBuffer[256];

                switch(pTexture->EmuResource8->GetType())
                {
                    case D3DRTYPE_TEXTURE:
                    {
                        sprintf(szBuffer, _DEBUG_DUMP_TEXTURE_SETTEXTURE "SetTextureNorm - %.03d (0x%.08X).bmp", dwDumpTexture++, pTexture->EmuTexture8);

                        pTexture->EmuTexture8->UnlockRect(0);

                        D3DXSaveTextureToFile(szBuffer, D3DXIFF_BMP, pTexture->EmuTexture8, NULL);
                    }
                    break;

                    case D3DRTYPE_CUBETEXTURE:
                    {
                        for(int face=0;face<6;face++)
                        {
                            sprintf(szBuffer, _DEBUG_DUMP_TEXTURE_SETTEXTURE "SetTextureCube%d - %.03d (0x%.08X).bmp", face, dwDumpTexture++, pTexture->EmuTexture8);

                            pTexture->EmuCubeTexture8->UnlockRect((D3DCUBEMAP_FACES)face, 0);

                            D3DXSaveTextureToFile(szBuffer, D3DXIFF_BMP, pTexture->EmuTexture8, NULL);
                        }
                    }
                    break;
                }
            }
            #endif
        }
    }

    /*
    static IDirect3DTexture8 *pDummyTexture[4] = {0, 0, 0, 0};

    if(pDummyTexture[Stage] == 0)
    {
        if(Stage == 0)
        {
            if(D3DXCreateTextureFromFile(g_pD3DDevice, "C:\\dummy1.bmp", &pDummyTexture[Stage]) != D3D_OK)
                CxbxKrnlCleanup("Could not create dummy texture!");
        }
        else if(Stage == 1)
        {
            if(D3DXCreateTextureFromFile(g_pD3DDevice, "C:\\dummy2.bmp", &pDummyTexture[Stage]) != D3D_OK)
                CxbxKrnlCleanup("Could not create dummy texture!");
        }
    }
    //*/

    /*
    static int dwDumpTexture = 0;
    char szBuffer[256];
    sprintf(szBuffer, "C:\\Aaron\\Textures\\DummyTexture - %.03d (0x%.08X).bmp", dwDumpTexture++, pDummyTexture);
    pDummyTexture->UnlockRect(0);
    D3DXSaveTextureToFile(szBuffer, D3DXIFF_BMP, pDummyTexture, NULL);
    //*/

    //HRESULT hRet = g_pD3DDevice->SetTexture(Stage, pDummyTexture[Stage]);

    HRESULT hRet = g_pD3DDevice->SetTexture(Stage, (g_iWireframe == 0) ? pBaseTexture8 : 0);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SwitchTexture
// ******************************************************************
VOID __fastcall XTL::EmuIDirect3DDevice8_SwitchTexture
(
    DWORD           Method,
    DWORD           Data,
    DWORD           Format
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SwitchTexture\n"
           "(\n"
           "   Method              : 0x%.08X\n"
           "   Data                : 0x%.08X\n"
           "   Format              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Method, Data, Format);

    DWORD StageLookup[] = { 0x00081b00, 0x00081b40, 0x00081b80, 0x00081bc0 };
    DWORD Stage = -1;

    for(int v=0;v<4;v++)
    {
        if(StageLookup[v] == Method)
        {
            Stage = v;
        }
    }

    if(Stage == -1)
    {
        EmuWarning("Unknown Method (0x%.08X)", Method);
    }
    else
    {
        //
        // WARNING: TODO: Correct reference counting has not been completely verified for this code
        //

        X_D3DTexture *pTexture = (X_D3DTexture *)g_DataToTexture.get(Data);

        EmuWarning("Switching Texture 0x%.08X (0x%.08X) @ Stage %d", pTexture, pTexture->EmuBaseTexture8, Stage);

        HRESULT hRet = g_pD3DDevice->SetTexture(Stage, pTexture->EmuBaseTexture8);

        /*
        if(pTexture->EmuBaseTexture8 != NULL)
        {
            static int dwDumpTexture = 0;

            char szBuffer[255];

            sprintf(szBuffer, "C:\\Aaron\\Textures\\0x%.08X-SwitchTexture%.03d.bmp", pTexture, dwDumpTexture++);

            pTexture->EmuTexture8->UnlockRect(0);

            D3DXSaveTextureToFile(szBuffer, D3DXIFF_BMP, pTexture->EmuBaseTexture8, NULL);
        }
        //*/
    }

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetDisplayMode
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetDisplayMode
(
    X_D3DDISPLAYMODE         *pMode
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetDisplayMode\n"
           "(\n"
           "   pMode               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pMode);

    HRESULT hRet;

    // make adjustments to parameters to make sense with windows d3d
    {
        D3DDISPLAYMODE *pPCMode = (D3DDISPLAYMODE*)pMode;

#ifndef D3D9
        hRet = g_pD3DDevice->GetDisplayMode(pPCMode);
#else
        hRet = g_pD3DDevice->GetDisplayMode(0, pPCMode);
#endif

        // Convert Format (PC->Xbox)
        pMode->Format = EmuPC2XB_D3DFormat(pPCMode->Format);

        // TODO: Make this configurable in the future?
        pMode->Flags  = 0x000000A1; // D3DPRESENTFLAG_FIELD | D3DPRESENTFLAG_INTERLACED | D3DPRESENTFLAG_LOCKABLE_BACKBUFFER

        // TODO: Retrieve from current CreateDevice settings?
        pMode->Width = 640;
        pMode->Height = 480;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_Begin
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_Begin
(
    X_D3DPRIMITIVETYPE     PrimitiveType
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_Begin\n"
           "(\n"
           "   PrimitiveType       : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), PrimitiveType);

    g_IVBPrimitiveType = PrimitiveType;

    if(g_IVBTable == 0)
    {
        g_IVBTable = (struct XTL::_D3DIVB*)CxbxMalloc(sizeof(XTL::_D3DIVB)*1024);
    }

    g_IVBTblOffs = 0;
    g_IVBFVF = 0;

    // default values
    ZeroMemory(g_IVBTable, sizeof(XTL::_D3DIVB)*1024);

    if(g_pIVBVertexBuffer == 0)
    {
        g_pIVBVertexBuffer = (DWORD*)CxbxMalloc(sizeof(XTL::_D3DIVB)*1024);
    }

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexData2f
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetVertexData2f
(
    int     Register,
    FLOAT   a,
    FLOAT   b
)
{
    // debug trace
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexData2f >>\n"
               "(\n"
               "   Register            : 0x%.08X\n"
               "   a                   : %f\n"
               "   b                   : %f\n"
               ");\n",
               GetCurrentThreadId(), Register, a, b);
        EmuSwapFS();   // XBox FS
    }
    #endif

    return EmuIDirect3DDevice8_SetVertexData4f(Register, a, b, 0.0f, 1.0f);
}

static inline DWORD FtoDW(FLOAT f) { return *((DWORD*)&f); }
static inline FLOAT DWtoF(DWORD f) { return *((FLOAT*)&f); }

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexData2s
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetVertexData2s
(
    int     Register,
    SHORT   a,
    SHORT   b
)
{
    // debug trace
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexData2s >>\n"
               "(\n"
               "   Register            : 0x%.08X\n"
               "   a                   : %d\n"
               "   b                   : %d\n"
               ");\n",
               GetCurrentThreadId(), Register, a, b);
        EmuSwapFS();   // XBox FS
    }
    #endif

    DWORD dwA = a, dwB = b;

    return EmuIDirect3DDevice8_SetVertexData4f(Register, DWtoF(dwA), DWtoF(dwB), 0.0f, 1.0f);
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexData4f
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetVertexData4f
(
    int     Register,
    FLOAT   a,
    FLOAT   b,
    FLOAT   c,
    FLOAT   d
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexData4f\n"
           "(\n"
           "   Register            : 0x%.08X\n"
           "   a                   : %f\n"
           "   b                   : %f\n"
           "   c                   : %f\n"
           "   d                   : %f\n"
           ");\n",
           GetCurrentThreadId(), Register, a, b, c, d);

    HRESULT hRet = S_OK;

    switch(Register)
    {
        // TODO: Blend weight.

        case 0: // D3DVSDE_POSITION
        {
            int o = g_IVBTblOffs;

            g_IVBTable[o].Position.x = a;
            g_IVBTable[o].Position.y = b;
            g_IVBTable[o].Position.z = c;
            g_IVBTable[o].Rhw = 1.0f;

            g_IVBTblOffs++;

            g_IVBFVF |= D3DFVF_XYZRHW;
        }
        break;

		case 1: // D3DVSDE_BLENDWEIGHT
		{
            int o = g_IVBTblOffs;

            g_IVBTable[o].Position.x = a;
            g_IVBTable[o].Position.y = b;
            g_IVBTable[o].Position.z = c;
			g_IVBTable[o].Blend1 = d;

            g_IVBTblOffs++;

            g_IVBFVF |= D3DFVF_XYZB1;
        }
        break;

        case 2: // D3DVSDE_NORMAL
        {
            int o = g_IVBTblOffs;

            g_IVBTable[o].Normal.x = a;
            g_IVBTable[o].Normal.y = b;
            g_IVBTable[o].Normal.z = c;

            g_IVBTblOffs++;

            g_IVBFVF |= D3DFVF_NORMAL;
        }
        break;

        case 3: // D3DVSDE_DIFFUSE
        {
            int o = g_IVBTblOffs;

            DWORD ca = FtoDW(d) << 24;
            DWORD cr = FtoDW(a) << 16;
            DWORD cg = FtoDW(b) << 8;
            DWORD cb = FtoDW(c) << 0;

            g_IVBTable[o].dwDiffuse = ca | cr | cg | cb;

            g_IVBFVF |= D3DFVF_DIFFUSE;
        }
        break;

        case 4: // D3DVSDE_SPECULAR
        {
            int o = g_IVBTblOffs;

            DWORD ca = FtoDW(d) << 24;
            DWORD cr = FtoDW(a) << 16;
            DWORD cg = FtoDW(b) << 8;
            DWORD cb = FtoDW(c) << 0;

            g_IVBTable[o].dwSpecular = ca | cr | cg | cb;

            g_IVBFVF |= D3DFVF_SPECULAR;
        }
        break;

        case 9: // D3DVSDE_TEXCOORD0
        {
            int o = g_IVBTblOffs;

            g_IVBTable[o].TexCoord1.x = a;
            g_IVBTable[o].TexCoord1.y = b;

            if( (g_IVBFVF & D3DFVF_TEXCOUNT_MASK) < D3DFVF_TEX1)
            {
                g_IVBFVF |= D3DFVF_TEX1;
            }
        }
        break;

        case 10: // D3DVSDE_TEXCOORD1
        {
            int o = g_IVBTblOffs;

            g_IVBTable[o].TexCoord2.x = a;
            g_IVBTable[o].TexCoord2.y = b;

            if( (g_IVBFVF & D3DFVF_TEXCOUNT_MASK) < D3DFVF_TEX2)
            {
                g_IVBFVF |= D3DFVF_TEX2;
            }
        }
        break;

        case 11: // D3DVSDE_TEXCOORD2
        {
            int o = g_IVBTblOffs;

            g_IVBTable[o].TexCoord3.x = a;
            g_IVBTable[o].TexCoord3.y = b;

            if( (g_IVBFVF & D3DFVF_TEXCOUNT_MASK) < D3DFVF_TEX3)
            {
                g_IVBFVF |= D3DFVF_TEX3;
            }
        }
        break;

        case 12: // D3DVSDE_TEXCOORD3
        {
            int o = g_IVBTblOffs;

            g_IVBTable[o].TexCoord4.x = a;
            g_IVBTable[o].TexCoord4.y = b;

            if( (g_IVBFVF & D3DFVF_TEXCOUNT_MASK) < D3DFVF_TEX4)
            {
                g_IVBFVF |= D3DFVF_TEX4;
            }
        }
        break;

        case 0xFFFFFFFF:
        {
            int o = g_IVBTblOffs;

            g_IVBTable[o].Position.x = a;
            g_IVBTable[o].Position.y = b;
            g_IVBTable[o].Position.z = c;
            g_IVBTable[o].Rhw = d;

            // Copy current color to next vertex
            g_IVBTable[o+1].dwDiffuse  = g_IVBTable[o].dwDiffuse;
            g_IVBTable[o+1].dwSpecular = g_IVBTable[o].dwSpecular;

            g_IVBTblOffs++;

            g_IVBFVF |= D3DFVF_XYZRHW;
        }
        break;

        default:
            CxbxKrnlCleanup("Unknown IVB Register : %d", Register);
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexData4ub
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetVertexData4ub
(
	INT		Register,
	BYTE	a,
	BYTE	b,
	BYTE	c,
	BYTE	d
)
{
	EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexData4ub\n"
           "(\n"
           "   Register            : 0x%.08X\n"
           "   a                   : 0x%.02X\n"
           "   b                   : 0x%.02X\n"
           "   c                   : 0x%.02X\n"
           "   d                   : 0x%.02X\n"
           ");\n",
           GetCurrentThreadId(), Register, a, b, c, d); 

	EmuSwapFS();	// Xbox FS

	DWORD dwA = a, dwB = b, dwC = c, dwD = d;

    return EmuIDirect3DDevice8_SetVertexData4f(Register, DWtoF(dwA), DWtoF(dwB), DWtoF(dwC), DWtoF(dwD));
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexData4s
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetVertexData4s
(
	INT		Register,
	SHORT	a,
	SHORT	b,
	SHORT	c,
	SHORT	d
)
{
	EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexData4s\n"
           "(\n"
           "   Register            : 0x%.08X\n"
           "   a                   : 0x%.04X\n"
           "   b                   : 0x%.04X\n"
           "   c                   : 0x%.04X\n"
           "   d                   : 0x%.04X\n"
           ");\n",
           GetCurrentThreadId(), Register, a, b, c, d); 

	EmuSwapFS();	// Xbox FS

	DWORD dwA = a, dwB = b, dwC = c, dwD = d;

    return EmuIDirect3DDevice8_SetVertexData4f(Register, DWtoF(dwA), DWtoF(dwB), DWtoF(dwC), DWtoF(dwD));
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexDataColor
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetVertexDataColor
(
    int         Register,
    D3DCOLOR    Color
)
{
    // debug trace
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexDataColor >>\n"
               "(\n"
               "   Register            : 0x%.08X\n"
               "   Color               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Register, Color);
        EmuSwapFS();   // XBox FS
    }
    #endif

    FLOAT a = DWtoF((Color & 0xFF000000) >> 24);
    FLOAT r = DWtoF((Color & 0x00FF0000) >> 16);
    FLOAT g = DWtoF((Color & 0x0000FF00) >> 8);
    FLOAT b = DWtoF((Color & 0x000000FF) >> 0);

    return EmuIDirect3DDevice8_SetVertexData4f(Register, r, g, b, a);
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_End
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_End()
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_End();\n", GetCurrentThreadId());

    if(g_IVBTblOffs != 0)
        EmuFlushIVB();

    // TODO: Should technically clean this up at some point..but on XP doesnt matter much
//    CxbxFree(g_pIVBVertexBuffer);
//    CxbxFree(g_IVBTable);

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_RunPushBuffer
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_RunPushBuffer
(
    X_D3DPushBuffer       *pPushBuffer,
    X_D3DFixup            *pFixup
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_RunPushBuffer\n"
           "(\n"
           "   pPushBuffer         : 0x%.08X\n"
           "   pFixup              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pPushBuffer, pFixup);

    XTL::EmuExecutePushBuffer(pPushBuffer, pFixup);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_Clear
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_Clear
(
    DWORD           Count,
    CONST D3DRECT  *pRects,
    DWORD           Flags,
    D3DCOLOR        Color,
    float           Z,
    DWORD           Stencil
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_Clear\n"
           "(\n"
           "   Count               : 0x%.08X\n"
           "   pRects              : 0x%.08X\n"
           "   Flags               : 0x%.08X\n"
           "   Color               : 0x%.08X\n"
           "   Z                   : %f\n"
           "   Stencil             : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Count, pRects, Flags,
           Color, Z, Stencil);

    // make adjustments to parameters to make sense with windows d3d
    {
        // TODO: D3DCLEAR_TARGET_A, *R, *G, *B don't exist on windows
        DWORD newFlags = 0;

        if(Flags & 0x000000f0)
            newFlags |= D3DCLEAR_TARGET;

        if(Flags & 0x00000001)
            newFlags |= D3DCLEAR_ZBUFFER;

        if(Flags & 0x00000002)
            newFlags |= D3DCLEAR_STENCIL;

        if(Flags & ~(0x000000f0 | 0x00000001 | 0x00000002))
            EmuWarning("Unsupported Flag(s) for IDirect3DDevice8_Clear : 0x%.08X", Flags & ~(0x000000f0 | 0x00000001 | 0x00000002));

        // Regardless of above setting, do not needlessly clear Z Buffer
        if (!g_bHasZBuffer) {
            newFlags &= ~D3DCLEAR_ZBUFFER;
            newFlags &= ~D3DCLEAR_STENCIL;
        }

        Flags = newFlags;
    }

	DWORD dwFillMode;

	if(g_iWireframe == 0)
        dwFillMode = D3DFILL_SOLID;
    else if(g_iWireframe == 1)
        dwFillMode = D3DFILL_WIREFRAME;
    else
        dwFillMode = D3DFILL_POINT;

    g_pD3DDevice->SetRenderState(D3DRS_FILLMODE, dwFillMode);

    HRESULT ret = g_pD3DDevice->Clear(Count, pRects, Flags, Color, Z, Stencil);

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_Present
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_Present
(
    CONST RECT* pSourceRect,
    CONST RECT* pDestRect,
    PVOID       pDummy1,
    PVOID       pDummy2
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_Present\n"
           "(\n"
           "   pSourceRect         : 0x%.08X\n"
           "   pDestRect           : 0x%.08X\n"
           "   pDummy1             : 0x%.08X\n"
           "   pDummy2             : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pSourceRect, pDestRect, pDummy1, pDummy2);

	HRESULT hRet = S_OK;
	
	// release back buffer lock
	{
#ifndef D3D9
		IDirect3DSurface8 *pBackBuffer;
#else
        IDirect3DSurface9 *pBackBuffer;
#endif

#ifndef D3D9
		g_pD3DDevice->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
#else
        g_pD3DDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
#endif

		pBackBuffer->UnlockRect();
	}

	// TODO: Make a video option to wait for VBlank before calling Present.
	// Makes syncing to 30fps easier (which is the native frame rate for Azurik
	// and Halo).
//	g_pDD7->WaitForVerticalBlank( DDWAITVB_BLOCKEND, NULL );
//	g_pDD7->WaitForVerticalBlank( DDWAITVB_BLOCKEND, NULL );

	hRet = g_pD3DDevice->Present(pSourceRect, pDestRect, (HWND)pDummy1, (CONST RGNDATA*)pDummy2);

	// Put primitives per frame in the title
	/*{
		char szString[64];

		sprintf( szString, "Cxbx: PPF(%d)", g_dwPrimPerFrame );

		SetWindowText( CxbxKrnl_hEmuParent, szString );

		g_dwPrimPerFrame = 0;
	}*/

	// not really accurate because you definately dont always present on every vblank
	g_VBData.Swap = g_VBData.VBlank;

	if(g_VBData.VBlank == g_VBLastSwap + 1)
		g_VBData.Flags = 1; // D3DVBLANK_SWAPDONE
	else
	{
		g_VBData.Flags = 2; // D3DVBLANK_SWAPMISSED
		g_SwapData.MissedVBlanks++;
	}

	// Handle Swap Callback function
	{
		g_SwapData.Swap++;

		if(g_pSwapCallback != NULL) 
		{
			EmuSwapFS();	// Xbox FS
			g_pSwapCallback(&g_SwapData);
			EmuSwapFS();	// Win2k/XP FS
		}
	}

	g_bHackUpdateSoftwareOverlay = FALSE;

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_Swap
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_Swap
(
    DWORD Flags
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_Swap\n"
           "(\n"
           "   Flags               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Flags);

    // TODO: Ensure this flag is always the same across library versions
    if(Flags != 0)
        EmuWarning("XTL::EmuIDirect3DDevice8_Swap: Flags != 0");

    // release back buffer lock
    {
#ifndef D3D9
        IDirect3DSurface8 *pBackBuffer;
#else
        IDirect3DSurface9 *pBackBuffer;
#endif

#ifndef D3D9
        g_pD3DDevice->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
#else
        g_pD3DDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
#endif

        if(pBackBuffer) pBackBuffer->UnlockRect();
    }

	// TODO: Make a video option to wait for VBlank before calling Present.
	// Makes syncing to 30fps easier (which is the native frame rate for Azurik
	// and Halo).
//	g_pDD7->WaitForVerticalBlank( DDWAITVB_BLOCKEND, NULL );
//	g_pDD7->WaitForVerticalBlank( DDWAITVB_BLOCKEND, NULL );

    HRESULT hRet = g_pD3DDevice->Present(0, 0, 0, 0);

	// Handle Swap Callback function
	{
		g_SwapData.Swap++;

		if(g_pSwapCallback != NULL) 
		{
			EmuSwapFS();	// Xbox FS
			g_pSwapCallback(&g_SwapData);
			EmuSwapFS();	// Win2k/XP FS
		}
	}

	g_bHackUpdateSoftwareOverlay = FALSE;

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DResource8_Register
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DResource8_Register
(
    X_D3DResource      *pThis,
    PVOID               pBase
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DResource8_Register\n"
           "(\n"
           "   pThis               : 0x%.08X (->Data : 0x%.08X)\n"
           "   pBase               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, pThis->Data, pBase);

    HRESULT hRet = S_OK;

    X_D3DResource *pResource = (X_D3DResource*)pThis;

    DWORD dwCommonType = pResource->Common & X_D3DCOMMON_TYPE_MASK;

    // add the offset of the current texture to the base
    pBase = (PVOID)((DWORD)pBase+pThis->Data);

    // Determine the resource type, and initialize
    switch(dwCommonType)
    {
        case X_D3DCOMMON_TYPE_VERTEXBUFFER:
        {
            DbgPrintf("EmuIDirect3DResource8_Register (0x%X) : Creating VertexBuffer...\n", GetCurrentThreadId());

            X_D3DVertexBuffer *pVertexBuffer = (X_D3DVertexBuffer*)pResource;

            // create vertex buffer
            {
                DWORD dwSize = EmuCheckAllocationSize(pBase, true);

                if(dwSize == -1)
                {
                    // TODO: once this is known to be working, remove the warning
                    EmuWarning("Vertex buffer allocation size unknown");
                    dwSize = 0x2000;  // temporarily assign a small buffer, which will be increased later
					/*hRet = E_FAIL;
					goto fail;*/
                }

                hRet = g_pD3DDevice->CreateVertexBuffer
                (
                    dwSize, 0, 0, D3DPOOL_MANAGED,
#ifndef D3D9
                    &pResource->EmuVertexBuffer8
#else
                    &pResource->EmuVertexBuffer8, NULL
#endif
                );

				if(FAILED(hRet))
				{
					// TODO: Hack for Crazy Taxi 3?
					char szString[256];
					sprintf( szString, "CreateVertexBuffer Failed!\n\nVB Size = 0x%X\n\nError: \nDesc: ", dwSize/*,
						DXGetErrorString8A(hRet)*//*, DXGetErrorDescription8A(hRet)*/);
					
					if( dwSize != 0 )
						CxbxKrnlCleanup( szString );
					else
					{
						EmuWarning( szString );

						EmuSwapFS();	// Xbox FS

						return hRet;
					}
				}

                #ifdef _DEBUG_TRACK_VB
                g_VBTrackTotal.insert(pResource->EmuVertexBuffer8);
                #endif

                BYTE *pData = 0;

#ifndef D3D9
                hRet = pResource->EmuVertexBuffer8->Lock(0, 0, &pData, 0);
#else
                hRet = pResource->EmuVertexBuffer8->Lock(0, 0, (void**)&pData, 0);
#endif

                if(FAILED(hRet))
                    CxbxKrnlCleanup("VertexBuffer Lock Failed!\n\nError: \nDesc: "/*,
								DXGetErrorString8A(hRet)*//*, DXGetErrorDescription8A(hRet)*/);


                memcpy(pData, (void*)pBase, dwSize);

                pResource->EmuVertexBuffer8->Unlock();

                pResource->Data = (ULONG)pData;
            }

            DbgPrintf("EmuIDirect3DResource8_Register (0x%X) : Successfully Created VertexBuffer (0x%.08X)\n", GetCurrentThreadId(), pResource->EmuVertexBuffer8);
        }
        break;

        case X_D3DCOMMON_TYPE_INDEXBUFFER:
        {
            DbgPrintf("EmuIDirect3DResource8_Register :-> IndexBuffer...\n");

            X_D3DIndexBuffer *pIndexBuffer = (X_D3DIndexBuffer*)pResource;

            // create index buffer
            {
                DWORD dwSize = EmuCheckAllocationSize(pBase, true);

                if(dwSize == -1 || dwSize == 0)
                {
                    // TODO: once this is known to be working, remove the warning
                    EmuWarning("Index buffer allocation size unknown");

                    pIndexBuffer->Lock = X_D3DRESOURCE_LOCK_FLAG_NOSIZE;

                    break;
                    // Halo dwSize = 0x336;
                }

                HRESULT hRet = g_pD3DDevice->CreateIndexBuffer
                (
                    dwSize, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED,
#ifndef D3D9
                    &pIndexBuffer->EmuIndexBuffer8
#else
                    &pIndexBuffer->EmuIndexBuffer8, NULL
#endif
                );

                if(FAILED(hRet))
					CxbxKrnlCleanup("CreateIndexBuffer Failed!\n\nError: \nDesc: \nSize: %d",
								/*DXGetErrorString8A(hRet), *//*DXGetErrorDescription8A(hRet),*/ dwSize);


                BYTE *pData = 0;

#ifndef D3D9
                hRet = pResource->EmuIndexBuffer8->Lock(0, dwSize, &pData, 0);
#else
                hRet = pResource->EmuIndexBuffer8->Lock(0, dwSize, (void**)&pData, 0);
#endif

                if(FAILED(hRet))
                    CxbxKrnlCleanup("IndexBuffer Lock Failed!\n\nError: %s\nDesc: "/*,
								DXGetErrorString8A(hRet)*//*, DXGetErrorDescription8A(hRet)*/);


                memcpy(pData, (void*)pBase, dwSize);

                pResource->EmuIndexBuffer8->Unlock();

                pResource->Data = (ULONG)pData;
            }

            DbgPrintf("EmuIDirect3DResource8_Register (0x%X) : Successfully Created IndexBuffer (0x%.08X)\n", GetCurrentThreadId(), pResource->EmuIndexBuffer8);
        }
        break;

        case X_D3DCOMMON_TYPE_PUSHBUFFER:
        {
            DbgPrintf("EmuIDirect3DResource8_Register :-> PushBuffer...\n");

            X_D3DPushBuffer *pPushBuffer = (X_D3DPushBuffer*)pResource;

            // create push buffer
            {
                DWORD dwSize = EmuCheckAllocationSize(pBase, true);

                if(dwSize == -1)
                {
                    // TODO: once this is known to be working, remove the warning
                    EmuWarning("Push buffer allocation size unknown");

                    pPushBuffer->Lock = X_D3DRESOURCE_LOCK_FLAG_NOSIZE;

                    break;
                }

                pResource->Data = (ULONG)pBase;
            }

            DbgPrintf("EmuIDirect3DResource8_Register (0x%X) : Successfully Created PushBuffer (0x%.08X, 0x%.08X, 0x%.08X)\n", GetCurrentThreadId(), pResource->Data, pPushBuffer->Size, pPushBuffer->AllocationSize);
        }
        break;

        case X_D3DCOMMON_TYPE_SURFACE:
        case X_D3DCOMMON_TYPE_TEXTURE:
        {
            if(dwCommonType == X_D3DCOMMON_TYPE_SURFACE) {
                DbgPrintf("EmuIDirect3DResource8_Register :-> Surface...\n");
            } else {
                DbgPrintf("EmuIDirect3DResource8_Register :-> Texture...\n");
            }

            X_D3DPixelContainer *pPixelContainer = (X_D3DPixelContainer*)pResource;

            X_D3DFORMAT X_Format = (X_D3DFORMAT)((pPixelContainer->Format & X_D3DFORMAT_FORMAT_MASK) >> X_D3DFORMAT_FORMAT_SHIFT);
            D3DFORMAT   Format   = EmuXB2PC_D3DFormat(X_Format);
            D3DFORMAT   CacheFormat = (XTL::D3DFORMAT)0;
            // TODO: check for dimensions

            // TODO: HACK: Temporary?
            if(X_Format == 0x2E)
            {
                /*CxbxKrnlCleanup*/EmuWarning("D3DFMT_LIN_D24S8 not yet supported!");
                X_Format = 0x12;
                Format   = D3DFMT_A8R8G8B8;
            }

			if(X_Format == 0x30)
            {
                /*CxbxKrnlCleanup*/EmuWarning("D3DFMT_LIN_D16 not yet supported!");
                X_Format = 0x11;
                Format   = D3DFMT_R5G6B5;
            }

            DWORD dwWidth, dwHeight, dwBPP, dwDepth = 1, dwPitch = 0, dwMipMapLevels = 1;
            BOOL  bSwizzled = FALSE, bCompressed = FALSE, dwCompressedSize = 0;
            BOOL  bCubemap = pPixelContainer->Format & X_D3DFORMAT_CUBEMAP;

            // Interpret Width/Height/BPP
            if(X_Format == 0x07 /* X_D3DFMT_X8R8G8B8 */ || X_Format == 0x06 /* X_D3DFMT_A8R8G8B8 */
			|| X_Format == 0x3A /* X_D3DFMT_A8B8G8R8 */)
            {
                bSwizzled = TRUE;

                // Swizzled 32 Bit
                dwWidth  = 1 << ((pPixelContainer->Format & X_D3DFORMAT_USIZE_MASK) >> X_D3DFORMAT_USIZE_SHIFT);
                dwHeight = 1 << ((pPixelContainer->Format & X_D3DFORMAT_VSIZE_MASK) >> X_D3DFORMAT_VSIZE_SHIFT);
                dwMipMapLevels = (pPixelContainer->Format & X_D3DFORMAT_MIPMAP_MASK) >> X_D3DFORMAT_MIPMAP_SHIFT;
                dwDepth  = 1;// HACK? 1 << ((pPixelContainer->Format & X_D3DFORMAT_PSIZE_MASK) >> X_D3DFORMAT_PSIZE_SHIFT);
                dwPitch  = dwWidth*4;
                dwBPP = 4;
            }
            else if(X_Format == 0x05 /* X_D3DFMT_R5G6B5 */ || X_Format == 0x04 /* X_D3DFMT_A4R4G4B4 */
                 || X_Format == 0x02 /* X_D3DFMT_A1R5G5B5 */ || X_Format == 0x03 /* X_D3DFMT_X1R5G5B5 */
                 || X_Format == 0x28 /* X_D3DFMT_G8B8 */ )
            {
                bSwizzled = TRUE;

                // Swizzled 16 Bit
                dwWidth  = 1 << ((pPixelContainer->Format & X_D3DFORMAT_USIZE_MASK) >> X_D3DFORMAT_USIZE_SHIFT);
                dwHeight = 1 << ((pPixelContainer->Format & X_D3DFORMAT_VSIZE_MASK) >> X_D3DFORMAT_VSIZE_SHIFT);
                dwMipMapLevels = (pPixelContainer->Format & X_D3DFORMAT_MIPMAP_MASK) >> X_D3DFORMAT_MIPMAP_SHIFT;
                dwDepth  = 1;// HACK? 1 << ((pPixelContainer->Format & X_D3DFORMAT_PSIZE_MASK) >> X_D3DFORMAT_PSIZE_SHIFT);
                dwPitch  = dwWidth*2;
                dwBPP = 2;
            }
            else if(X_Format == 0x00 /* X_D3DFMT_L8 */ || X_Format == 0x0B /* X_D3DFMT_P8 */
                || X_Format == 0x01 /* X_D3DFMT_AL8 */ || X_Format == 0x1A /* X_D3DFMT_A8L8 */
                || X_Format == 0x19 /* X_D3DFMT_A8 */)
            {
                bSwizzled = TRUE;

                // Swizzled 8 Bit
                dwWidth  = 1 << ((pPixelContainer->Format & X_D3DFORMAT_USIZE_MASK) >> X_D3DFORMAT_USIZE_SHIFT);
                dwHeight = 1 << ((pPixelContainer->Format & X_D3DFORMAT_VSIZE_MASK) >> X_D3DFORMAT_VSIZE_SHIFT);
                dwMipMapLevels = (pPixelContainer->Format & X_D3DFORMAT_MIPMAP_MASK) >> X_D3DFORMAT_MIPMAP_SHIFT;
                dwDepth  = 1;// HACK? 1 << ((pPixelContainer->Format & X_D3DFORMAT_PSIZE_MASK) >> X_D3DFORMAT_PSIZE_SHIFT);
                dwPitch  = dwWidth;
                dwBPP = 1;
            }
            else if(X_Format == 0x1E /* X_D3DFMT_LIN_X8R8G8B8 */ || X_Format == 0x12 /* X_D3DFMT_LIN_A8R8G8B8 */ 
				 || X_Format == 0x2E /* D3DFMT_LIN_D24S8 */ || X_Format == 0x3F /* X_D3DFMT_LIN_A8B8G8R8 */)
            {
                // Linear 32 Bit
                dwWidth  = (pPixelContainer->Size & X_D3DSIZE_WIDTH_MASK) + 1;
                dwHeight = ((pPixelContainer->Size & X_D3DSIZE_HEIGHT_MASK) >> X_D3DSIZE_HEIGHT_SHIFT) + 1;
                dwPitch  = (((pPixelContainer->Size & X_D3DSIZE_PITCH_MASK) >> X_D3DSIZE_PITCH_SHIFT)+1)*64;
                dwBPP = 4;
            }
            else if(X_Format == 0x11 /* D3DFMT_LIN_R5G6B5 */ || X_Format == 0x30 /* D3DFMT_LIN_D16 */
				 || X_Format == 0x1D /* X_D3DFMT_LIN_A4R4G4B4 */ || X_Format == 0x10 /* X_D3DFMT_LIN_A1R5G5B5 */)
            {
                // Linear 16 Bit
                dwWidth  = (pPixelContainer->Size & X_D3DSIZE_WIDTH_MASK) + 1;
                dwHeight = ((pPixelContainer->Size & X_D3DSIZE_HEIGHT_MASK) >> X_D3DSIZE_HEIGHT_SHIFT) + 1;
                dwPitch  = (((pPixelContainer->Size & X_D3DSIZE_PITCH_MASK) >> X_D3DSIZE_PITCH_SHIFT)+1)*64;
                dwBPP = 2;
            }
            else if(X_Format == 0x0C /* D3DFMT_DXT1 */ || X_Format == 0x0E /* D3DFMT_DXT2 */ || X_Format == 0x0F /* D3DFMT_DXT3 */)
            {
                bCompressed = TRUE;

                // Compressed
                dwWidth  = 1 << ((pPixelContainer->Format & X_D3DFORMAT_USIZE_MASK) >> X_D3DFORMAT_USIZE_SHIFT);
                dwHeight = 1 << ((pPixelContainer->Format & X_D3DFORMAT_VSIZE_MASK) >> X_D3DFORMAT_VSIZE_SHIFT);
                dwDepth  = 1 << ((pPixelContainer->Format & X_D3DFORMAT_PSIZE_MASK) >> X_D3DFORMAT_PSIZE_SHIFT);
                dwMipMapLevels = (pPixelContainer->Format & X_D3DFORMAT_MIPMAP_MASK) >> X_D3DFORMAT_MIPMAP_SHIFT;

                // D3DFMT_DXT2...D3DFMT_DXT5 : 128bits per block/per 16 texels
                dwCompressedSize = dwWidth*dwHeight;

                if(X_Format == 0x0C)    // D3DFMT_DXT1 : 64bits per block/per 16 texels
                    dwCompressedSize /= 2;

                dwBPP = 1;
            }
            else if(X_Format == 0x24 /* D3DFMT_YUY2 */)
            {
                // Linear 32 Bit
                dwWidth  = (pPixelContainer->Size & X_D3DSIZE_WIDTH_MASK) + 1;
                dwHeight = ((pPixelContainer->Size & X_D3DSIZE_HEIGHT_MASK) >> X_D3DSIZE_HEIGHT_SHIFT) + 1;
                dwPitch  = (((pPixelContainer->Size & X_D3DSIZE_PITCH_MASK) >> X_D3DSIZE_PITCH_SHIFT)+1)*64;
            }
            else
            {
                CxbxKrnlCleanup("0x%.08X is not a supported format!\n", X_Format);
            }

            if(X_Format == 0x24 /* X_D3DFMT_YUY2 */)
            {
                //
                // cache the overlay size
                //

                g_dwOverlayW = dwWidth;
                g_dwOverlayH = dwHeight;
                g_dwOverlayP = RoundUp(g_dwOverlayW, 64)*2;

                //
                // create texture resource
                //

                DWORD dwSize = g_dwOverlayP*g_dwOverlayH;
                DWORD dwPtr = (DWORD)CxbxMalloc(dwSize + sizeof(DWORD));

                DWORD *pRefCount = (DWORD*)(dwPtr + dwSize);

                // initialize ref count
                *pRefCount = 1;

                // If YUY2 is not supported in hardware, we'll actually mark this as a special fake texture (set highest bit)
                pPixelContainer->Data = X_D3DRESOURCE_DATA_FLAG_SPECIAL | X_D3DRESOURCE_DATA_FLAG_YUVSURF;
                pPixelContainer->Lock = dwPtr;
                pPixelContainer->Format = 0x24;

                pPixelContainer->Size  = (g_dwOverlayW & X_D3DSIZE_WIDTH_MASK);
                pPixelContainer->Size |= (g_dwOverlayH << X_D3DSIZE_HEIGHT_SHIFT);
                pPixelContainer->Size |= (g_dwOverlayP << X_D3DSIZE_PITCH_SHIFT);
            }
            else
            {
                if(bSwizzled || bCompressed)
                {
                    uint32 w = dwWidth;
                    uint32 h = dwHeight;

                    for(uint32 v=0;v<dwMipMapLevels;v++)
                    {
                        if( ((1u << v) >= w) || ((1u << v) >= h))
                        {
                            dwMipMapLevels = v + 1;
                            break;
                        }
                    }
                }

                // create the happy little texture
                if(dwCommonType == X_D3DCOMMON_TYPE_SURFACE)
                {
#ifndef D3D9
                    hRet = g_pD3DDevice->CreateImageSurface(dwWidth, dwHeight, Format, &pResource->EmuSurface8);
#else
                    hRet = g_pD3DDevice->CreateOffscreenPlainSurface(dwWidth, dwHeight, Format, D3DPOOL_SCRATCH /*D3DPOOL_MANAGED*/, &pResource->EmuSurface8, NULL);
#endif

                    if(FAILED(hRet))
                        CxbxKrnlCleanup("CreateImageSurface Failed!\n\nError: %s\nDesc: %s"/*,
								DXGetErrorString8A(hRet), DXGetErrorDescription8A(hRet)*/);
;

                    DbgPrintf("EmuIDirect3DResource8_Register (0x%X) : Successfully Created ImageSurface (0x%.08X, 0x%.08X)\n", GetCurrentThreadId(), pResource, pResource->EmuSurface8);
                    DbgPrintf("EmuIDirect3DResource8_Register (0x%X) : Width : %d, Height : %d, Format : %d\n", GetCurrentThreadId(), dwWidth, dwHeight, Format);
                }
                else
                {
                    // TODO: HACK: Figure out why this is necessary!
                    // TODO: This is necessary for DXT1 textures at least (4x4 blocks minimum)
                    if(dwWidth < 4)
                    {
                        EmuWarning("Expanding texture width (%d->4)", dwWidth);
                        dwWidth = 4;

                        dwMipMapLevels = 3;
                    }

                    if(dwHeight < 4)
                    {
                        EmuWarning("Expanding texture height (%d->4)", dwHeight);
                        dwHeight = 4;

                        dwMipMapLevels = 3;
                    }

                    // HACK HACK HACK HACK HACK HACK HACK HACK HACK HACK
                    // Since most modern graphics cards does not support
                    // palette based textures we need to expand it to
                    // ARGB texture format
                    if (Format == D3DFMT_P8) //Palette
                    {
						/*EmuWarning("D3DFMT_P8 -> D3DFMT_A8R8G8B8");

                        CacheFormat = Format;       // Save this for later
                        Format = D3DFMT_A8R8G8B8; */  // ARGB

						// Temporarily use the LoveMhz hack
						EmuWarning("D3DFMT_P8 -> D3DFMT_L8");
						Format = D3DFMT_L8;
                    }

                    if(bCubemap)
                    {
                        DbgPrintf("CreateCubeTexture(%d, %d, 0, %d, D3DPOOL_MANAGED, 0x%.08X)\n", dwWidth,
                            dwMipMapLevels, Format, &pResource->EmuTexture8);

                        hRet = g_pD3DDevice->CreateCubeTexture
                        (
                            dwWidth, dwMipMapLevels, 0, Format,
#ifndef D3D9
                            D3DPOOL_MANAGED, &pResource->EmuCubeTexture8
#else
                            D3DPOOL_MANAGED, &pResource->EmuCubeTexture8, NULL
#endif
                        );

                        if(FAILED(hRet))
                            CxbxKrnlCleanup("CreateCubeTexture Failed!\n\nError: \nDesc: "/*,
								DXGetErrorString8A(hRet), DXGetErrorDescription8A(hRet)*/);

                        DbgPrintf("EmuIDirect3DResource8_Register (0x%X) : Successfully Created CubeTexture (0x%.08X, 0x%.08X)\n", GetCurrentThreadId(), pResource, pResource->EmuCubeTexture8);
                    }
                    else
                    {
                    //    printf("CreateTexture(%d, %d, %d, 0, %d (X=0x%.08X), D3DPOOL_MANAGED, 0x%.08X)\n", dwWidth, dwHeight,
                     //       dwMipMapLevels, Format, X_Format, &pResource->EmuTexture8);

						// HACK: Quantum Redshift
						/*if( dwMipMapLevels == 8 && X_Format == 0x0C )
						{
							printf( "Dirty Quantum Redshift hack applied!\n" );
							dwMipMapLevels = 1;
						}*/

                        hRet = g_pD3DDevice->CreateTexture
                        (
                            dwWidth, dwHeight, dwMipMapLevels, 0, Format,
#ifndef D3D9
                            D3DPOOL_MANAGED, &pResource->EmuTexture8
#else
                            D3DPOOL_MANAGED, &pResource->EmuTexture8, NULL
#endif
                        );

						/*if(FAILED(hRet))
						{
							hRet = g_pD3DDevice->CreateTexture
							(
								dwWidth, dwHeight, dwMipMapLevels, 0, Format,
								D3DPOOL_SYSTEMMEM, &pResource->EmuTexture8
							);
						}*/

                        if(FAILED(hRet))
							CxbxKrnlCleanup("CreateTexture Failed!\n\nError: \nDesc: "/*,
								DXGetErrorString8A(hRet), DXGetErrorDescription8A(hRet)*/);

                        DbgPrintf("EmuIDirect3DResource8_Register (0x%X) : Successfully Created Texture (0x%.08X, 0x%.08X)\n", GetCurrentThreadId(), pResource, pResource->EmuTexture8);
                    }
                }

                uint32 stop = bCubemap ? 6 : 1;

                for(uint32 r=0;r<stop;r++)
                {
                    // as we iterate through mipmap levels, we'll adjust the source resource offset
                    DWORD dwCompressedOffset = 0;

                    DWORD dwMipOffs = 0;
                    DWORD dwMipWidth = dwWidth;
                    DWORD dwMipHeight = dwHeight;
                    DWORD dwMipPitch = dwPitch;

                    // iterate through the number of mipmap levels
                    for(uint level=0;level<dwMipMapLevels;level++)
                    {
                        D3DLOCKED_RECT LockedRect;

                        // copy over data (deswizzle if necessary)
                        if(dwCommonType == X_D3DCOMMON_TYPE_SURFACE)
                            hRet = pResource->EmuSurface8->LockRect(&LockedRect, NULL, 0);
                        else
                        {
                            if(bCubemap)
                            {
                                hRet = pResource->EmuCubeTexture8->LockRect((D3DCUBEMAP_FACES)r, 0, &LockedRect, NULL, 0);
                            }
                            else
                            {
                                hRet = pResource->EmuTexture8->LockRect(level, &LockedRect, NULL, 0);
                            }
                        }

                        RECT  iRect  = {0,0,0,0};
                        POINT iPoint = {0,0};

                        BYTE *pSrc = (BYTE*)pBase;

                        if( pBase != NULL ) pThis->Data = (DWORD)pSrc;

                        if(( IsSpecialResource(pResource->Data) && (pResource->Data & X_D3DRESOURCE_DATA_FLAG_SURFACE))
                         ||( IsSpecialResource(pBase) && ((DWORD)pBase & X_D3DRESOURCE_DATA_FLAG_SURFACE)))
                        {
                            EmuWarning("Attempt to registered to another resource's data (eww!)");

                            // TODO: handle this horrible situation
                            BYTE *pDest = (BYTE*)LockedRect.pBits;
                            for(DWORD v=0;v<dwMipHeight;v++)
                            {
                                memset(pDest, 0, dwMipWidth*dwBPP);

                                pDest += LockedRect.Pitch;
                                pSrc  += dwMipPitch;
                            }
                        }
                        else
                        {
                            if(bSwizzled)
                            {
                                if((DWORD)pSrc == 0x80000000)
                                {
                                    // TODO: Fix or handle this situation..?
                                }
                                else
                                {
                                    if (CacheFormat == D3DFMT_P8) //Palette
                                    {
                                        EmuWarning("Unsupported texture format D3DFMT_P8,\nexpanding to D3DFMT_A8R8G8B8");
//#if 0
                                        //
                                        // create texture resource
                                        //
										//__asm int 3;

										// Attempt to use correct palette sizes
										DWORD dwPaletteAllocSize = (dwCurrentPaletteSize == -1) ? 256*4 : dwCurrentPaletteSize;

                                        BYTE *pPixelData = (BYTE*)LockedRect.pBits;
                                        DWORD dwDataSize = dwMipWidth*dwMipHeight*4;
                                        DWORD dwPaletteSize = dwPaletteAllocSize; //256*4;    // Note: This is not allways true, it can be 256- 128- 64- or 32*4

                                        BYTE* pTextureCache = (BYTE*)CxbxMalloc(dwDataSize);
                                        BYTE* pExpandedTexture = (BYTE*)CxbxMalloc(dwDataSize);
                                        BYTE* pTexturePalette = (BYTE*)CxbxMalloc(dwPaletteAllocSize);
	
										//__asm int 3;
                                        // First we need to unswizzle the texture data
                                        XTL::EmuXGUnswizzleRect
                                        (
                                            pSrc + dwMipOffs, dwMipWidth, dwMipHeight, dwDepth, LockedRect.pBits,
                                            LockedRect.Pitch, iRect, iPoint, dwBPP
                                        );

										//__asm int 3;
                                        // Copy the unswizzled data to a temporary buffer
                                        memcpy(pTextureCache, pPixelData, dwDataSize);

										//__asm int 3;
                                        // Copy the currently selected palette's data to the buffer
                                        memcpy(pTexturePalette, pCurrentPalette, dwPaletteSize);

										//__asm int 3;
                                        unsigned int w = 0;
                                        unsigned int c = 0;
                                        unsigned char p = 0;
                                        for (unsigned int y = 0;y < dwDataSize/4;y++)
                                        {
                                            if(c == dwMipWidth)
                                            {
                                                w += dwMipWidth*3;
                                                c = 0;
                                            }
                                            p = (unsigned char)pTextureCache[w];
                                            pExpandedTexture[y*4+0] = pTexturePalette[p*4+0];
                                            pExpandedTexture[y*4+1] = pTexturePalette[p*4+1];
                                            pExpandedTexture[y*4+2] = pTexturePalette[p*4+2];
                                            pExpandedTexture[y*4+3] = pTexturePalette[p*4+3];
                                            w++;
                                            c++;
                                        }

										//__asm int 3;
                                        // Copy the expanded texture back to the buffer
                                        memcpy(pPixelData, pExpandedTexture, dwDataSize);

                                        // Flush unused data buffers
                                        CxbxFree(pTexturePalette);
                                        CxbxFree(pExpandedTexture);
                                        CxbxFree(pTextureCache);
//#endif
                                    }
                                    else
                                    {
                                        XTL::EmuXGUnswizzleRect
                                        (
                                            pSrc + dwMipOffs, dwMipWidth, dwMipHeight, dwDepth, LockedRect.pBits,
                                            LockedRect.Pitch, iRect, iPoint, dwBPP
                                        );
                                    }
                                }
                            }
                            else if(bCompressed)
                            {
                                // NOTE: compressed size is (dwWidth/2)*(dwHeight/2)/2, so each level divides by 4

                                memcpy(LockedRect.pBits, pSrc + dwCompressedOffset, dwCompressedSize >> (level*2));

                                dwCompressedOffset += (dwCompressedSize >> (level*2));
                            }
                            else
                            {
                                BYTE *pDest = (BYTE*)LockedRect.pBits;

								if( pSrc )
								{
                                if((DWORD)LockedRect.Pitch == dwMipPitch && dwMipPitch == dwMipWidth*dwBPP)
                                {
                                    memcpy(pDest, pSrc + dwMipOffs, dwMipWidth*dwMipHeight*dwBPP);
                                }
                                else
                                {
                                    for(DWORD v=0;v<dwMipHeight;v++)
                                    {
                                        memcpy(pDest, pSrc + dwMipOffs, dwMipWidth*dwBPP);

                                        pDest += LockedRect.Pitch;
                                        pSrc  += dwMipPitch;
                                    }
                                }
								}
                            }
                        }

                        if(dwCommonType == X_D3DCOMMON_TYPE_SURFACE)
                            pResource->EmuSurface8->UnlockRect();
                        else
                        {
                            if(bCubemap)
                                pResource->EmuCubeTexture8->UnlockRect((D3DCUBEMAP_FACES)r, 0);
                            else
                                pResource->EmuTexture8->UnlockRect(level);
                        }

                        dwMipOffs += dwMipWidth*dwMipHeight*dwBPP;

                        dwMipWidth /= 2;
                        dwMipHeight /= 2;
                        dwMipPitch /= 2;
                    }
                }

                // Debug Texture Dumping
                #ifdef _DEBUG_DUMP_TEXTURE_REGISTER
                if(dwCommonType == X_D3DCOMMON_TYPE_SURFACE)
                {
                    static int dwDumpSurface = 0;

                    char szBuffer[255];

                    sprintf(szBuffer, _DEBUG_DUMP_TEXTURE_REGISTER "%.03d-RegSurface%.03d.dds", X_Format, dwDumpSurface++);

                    D3DXSaveSurfaceToFile(szBuffer, D3DXIFF_DDS, pResource->EmuSurface8, NULL, NULL);
                }
                else
                {
                    if(bCubemap)
                    {
                        static int dwDumpCube = 0;

                        char szBuffer[255];

                        for(int v=0;v<6;v++)
                        {
                            IDirect3DSurface8 *pSurface=0;

                            sprintf(szBuffer, _DEBUG_DUMP_TEXTURE_REGISTER "%.03d-RegCubeTex%.03d-%d.dds", X_Format, dwDumpCube++, v);

                            pResource->EmuCubeTexture8->GetCubeMapSurface((D3DCUBEMAP_FACES)v, 0, &pSurface);

                            D3DXSaveSurfaceToFile(szBuffer, D3DXIFF_DDS, pSurface, NULL, NULL);
                        }
                    }
                    else
                    {
                        static int dwDumpTex = 0;

                        char szBuffer[255];

                        sprintf(szBuffer, _DEBUG_DUMP_TEXTURE_REGISTER "%.03d-RegTexture%.03d.dds", X_Format, dwDumpTex++);

                        D3DXSaveTextureToFile(szBuffer, D3DXIFF_DDS, pResource->EmuTexture8, NULL);
                    }
                }
                #endif
            }
        }
        break;

        case X_D3DCOMMON_TYPE_PALETTE:
        {
            DbgPrintf("EmuIDirect3DResource8_Register :-> Palette...\n");

            X_D3DPalette *pPalette = (X_D3DPalette*)pResource;

            // create palette
            {
                DWORD dwSize = EmuCheckAllocationSize(pBase, true);

                if(dwSize == -1)
                {
                    // TODO: once this is known to be working, remove the warning
                    EmuWarning("Palette allocation size unknown");

                    pPalette->Lock = X_D3DRESOURCE_LOCK_FLAG_NOSIZE;
                }

                pCurrentPalette = pBase;
				dwCurrentPaletteSize = dwSize;

                pResource->Data = (ULONG)pBase;
            }

            //DbgPrintf("EmuIDirect3DResource8_Register (0x%X) : Successfully Created Palette (0x%.08X, 0x%.08X, 0x%.08X)\n", GetCurrentThreadId(), pResource->Data, pResource->Size, pResource->AllocationSize);
        }
        break;

        case X_D3DCOMMON_TYPE_FIXUP:
        {
            X_D3DFixup *pFixup = (X_D3DFixup*)pResource;

            CxbxKrnlCleanup("IDirect3DReosurce8::Register -> X_D3DCOMMON_TYPE_FIXUP is not yet supported\n"
            "0x%.08X (pFixup->Common) \n"
            "0x%.08X (pFixup->Data)   \n"
            "0x%.08X (pFixup->Lock)   \n"
            "0x%.08X (pFixup->Run)    \n"
            "0x%.08X (pFixup->Next)   \n"
            "0x%.08X (pFixup->Size)   \n", pFixup->Common, pFixup->Data, pFixup->Lock, pFixup->Run, pFixup->Next, pFixup->Size);
        }

        default:
            CxbxKrnlCleanup("IDirect3DResource8::Register -> Common Type 0x%.08X not yet supported", dwCommonType);
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DResource8_AddRef
// ******************************************************************
ULONG WINAPI XTL::EmuIDirect3DResource8_AddRef
(
    X_D3DResource      *pThis
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DResource8_AddRef\n"
           "(\n"
           "   pThis               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis);

    ULONG uRet = 0;

    if (!pThis) {
        EmuWarning("IDirect3DResource8::AddRef() was not passed a valid pointer!");
    }
    else 
    {
        if(IsSpecialResource(pThis->Data) && (pThis->Data & X_D3DRESOURCE_DATA_FLAG_YUVSURF))
        {
            DWORD  dwPtr = (DWORD)pThis->Lock;
            DWORD *pRefCount = (DWORD*)(dwPtr + g_dwOverlayP*g_dwOverlayH);
            ++(*pRefCount);
        }
        else
        {
#ifndef D3D9
            IDirect3DResource8 *pResource8 = pThis->EmuResource8;
#else
            IDirect3DResource9 *pResource8 = pThis->EmuResource8;
#endif

            if(pThis->Lock == 0x8000BEEF)
                uRet = ++pThis->Lock;
            else if(pResource8 != 0)
                uRet = pResource8->AddRef();

		    if(!pResource8)
			    __asm int 3;
			    //EmuWarning("EmuResource is not a valid pointer!");

            pThis->Common = (pThis->Common & ~X_D3DCOMMON_REFCOUNT_MASK) | ((pThis->Common & X_D3DCOMMON_REFCOUNT_MASK) + 1);
        }
    }

    EmuSwapFS();   // XBox FS

    return uRet;
}

// ******************************************************************
// * func: EmuIDirect3DResource8_Release
// ******************************************************************
ULONG WINAPI XTL::EmuIDirect3DResource8_Release
(
    X_D3DResource      *pThis
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DResource8_Release\n"
           "(\n"
           "   pThis               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis);

    ULONG uRet = 0;

	// HACK: In case the clone technique fails...
	if(!pThis)
	{
		EmuWarning("NULL texture!");

		EmuSwapFS(); // Xbox FS

		return 0;
	}

	// HACK: Clone textures generated by D3DDevice::GetTexture2
	if(IsSpecialResource(pThis->Data) && (pThis->Data & X_D3DRESOURCE_DATA_FLAG_TEXCLON))
	{
		EmuWarning( "Deleting clone texture (from D3DDevice::GetTexture2)...\n" );
//		uRet = pThis->EmuBaseTexture8->Release();
		delete pThis;
	}
    else if(IsSpecialResource(pThis->Data) && (pThis->Data & X_D3DRESOURCE_DATA_FLAG_YUVSURF))
    {
        DWORD  dwPtr = (DWORD)pThis->Lock;
        DWORD *pRefCount = (DWORD*)(dwPtr + g_dwOverlayP*g_dwOverlayH);

        if(--(*pRefCount) == 0)
        {
            if(g_YuvSurface == pThis)
                g_YuvSurface = NULL;

            // free memory associated with this special resource handle
            CxbxFree((PVOID)dwPtr);
        }

        EmuSwapFS();
        EmuIDirect3DDevice8_EnableOverlay(FALSE);
        EmuSwapFS();
    }
    else
    {
#ifndef D3D9
        IDirect3DResource8 *pResource8 = pThis->EmuResource8;
#else
        IDirect3DResource9 *pResource8 = pThis->EmuResource8;
#endif

        if(pThis->Lock == 0x8000BEEF)
        {
            delete[] (PVOID)pThis->Data;
            uRet = --pThis->Lock;
        }
        else if(pResource8 != 0)
        {
            for(int v=0;v<16;v++)
            {
                if(pCache[v].Data == pThis->Data && pThis->Data != 0)
                {
                    pCache[v].Data = 0;
                    break;
                }
            }

            #ifdef _DEBUG_TRACE_VB
            D3DRESOURCETYPE Type = pResource8->GetType();
            #endif

            uRet = pResource8->Release();

            if(uRet == 0)
            {
                DbgPrintf("EmuIDirect3DResource8_Release (0x%X): Cleaned up a Resource!\n", GetCurrentThreadId());

                #ifdef _DEBUG_TRACE_VB
                if(Type == D3DRTYPE_VERTEXBUFFER)
                {
                    g_VBTrackTotal.remove(pResource8);
                    g_VBTrackDisable.remove(pResource8);
                }
                #endif

                //delete pThis;
            }
        }

        pThis->Common = (pThis->Common & ~X_D3DCOMMON_REFCOUNT_MASK) | ((pThis->Common & X_D3DCOMMON_REFCOUNT_MASK) - 1);
    }


    EmuSwapFS();   // XBox FS

    return uRet;
}

// ******************************************************************
// * func: EmuIDirect3DResource8_IsBusy
// ******************************************************************
BOOL WINAPI XTL::EmuIDirect3DResource8_IsBusy
(
    X_D3DResource      *pThis
)
{
    EmuSwapFS();   // Win2k/XP FS

    /* too much output
    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DResource8_IsBusy\n"
           "(\n"
           "   pThis               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis);
    //*/

#ifndef D3D9
    IDirect3DResource8 *pResource8 = pThis->EmuResource8;
#else
    IDirect3DResource9 *pResource8 = pThis->EmuResource8;
#endif

    EmuSwapFS();   // XBox FS

    return FALSE;
}

// ******************************************************************
// * func: EmuIDirect3DResource8_GetType
// ******************************************************************
XTL::X_D3DRESOURCETYPE WINAPI XTL::EmuIDirect3DResource8_GetType
(
    X_D3DResource      *pThis
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DResource8_GetType\n"
           "(\n"
           "   pThis               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis);

	D3DRESOURCETYPE rType;

	// Check for Xbox specific resources (Azurik may need this)
	DWORD dwType = pThis->Common & X_D3DCOMMON_TYPE_MASK;

	switch(dwType)
	{
	case X_D3DCOMMON_TYPE_PUSHBUFFER:
		rType = (D3DRESOURCETYPE) 8; break;
	case X_D3DCOMMON_TYPE_PALETTE:
		rType = (D3DRESOURCETYPE) 9; break;
	case X_D3DCOMMON_TYPE_FIXUP:
		rType = (D3DRESOURCETYPE) 10; break;
	default:
		rType = pThis->EmuResource8->GetType(); break;
	}

    EmuSwapFS();   // XBox FS

    return (X_D3DRESOURCETYPE)rType;
}

// ******************************************************************
// * func: EmuLock2DSurface
// ******************************************************************
VOID WINAPI XTL::EmuLock2DSurface
(
    X_D3DPixelContainer *pPixelContainer,
    D3DCUBEMAP_FACES     FaceType,
    UINT                 Level,
    D3DLOCKED_RECT      *pLockedRect,
    RECT                *pRect,
    DWORD                Flags
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuLock2DSurface\n"
           "(\n"
           "   pPixelContainer     : 0x%.08X\n"
           "   FaceType            : 0x%.08X\n"
           "   Level               : 0x%.08X\n"
           "   pLockedRect         : 0x%.08X\n"
           "   pRect               : 0x%.08X\n"
           "   Flags               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pPixelContainer, FaceType, Level, pLockedRect, pRect, Flags);

    EmuVerifyResourceIsRegistered(pPixelContainer);

    HRESULT hRet = pPixelContainer->EmuCubeTexture8->LockRect(FaceType, Level, pLockedRect, pRect, Flags);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuLock3DSurface
// ******************************************************************
VOID WINAPI XTL::EmuLock3DSurface
(
    X_D3DPixelContainer *pPixelContainer,
    UINT				Level,
	D3DLOCKED_BOX		*pLockedVolume,
	D3DBOX				*pBox,
	DWORD				Flags
)
{
	EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuLock2DSurface\n"
           "(\n"
           "   pPixelContainer     : 0x%.08X\n"
           "   Level               : 0x%.08X\n"
           "   pLockedVolume       : 0x%.08X\n"
           "   pBox                : 0x%.08X\n"
           "   Flags               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pPixelContainer, Level, pLockedVolume, pBox, Flags);

    EmuVerifyResourceIsRegistered(pPixelContainer);

    HRESULT hRet = pPixelContainer->EmuVolumeTexture8->LockBox(Level, pLockedVolume, pBox, Flags);
	if(FAILED(hRet))
		EmuWarning("Lock3DSurface failed!");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuGet2DSurfaceDesc
// ******************************************************************
VOID WINAPI XTL::EmuGet2DSurfaceDesc
(
    X_D3DPixelContainer *pPixelContainer,
    DWORD                dwLevel,
    X_D3DSURFACE_DESC   *pDesc
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuGet2DSurfaceDesc\n"
           "(\n"
           "   pPixelContainer     : 0x%.08X\n"
           "   dwLevel             : 0x%.08X\n"
           "   pDesc               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pPixelContainer, dwLevel, pDesc);

    EmuVerifyResourceIsRegistered(pPixelContainer);

    D3DSURFACE_DESC SurfaceDesc;

    ZeroMemory(&SurfaceDesc, sizeof(SurfaceDesc));

    HRESULT hRet;

    if(dwLevel == 0xFEFEFEFE)
    {
		DbgPrintf("EmuSurface8: = 0x%.08X\n", pPixelContainer->EmuSurface8 );
        hRet = pPixelContainer->EmuSurface8->GetDesc(&SurfaceDesc);

        /*
        static int dwDumpSurface = 0;

        char szBuffer[255];

        sprintf(szBuffer, "C:\\Aaron\\Textures\\Surface%.03d.bmp", dwDumpSurface++);

        D3DXSaveSurfaceToFile(szBuffer, D3DXIFF_BMP, pPixelContainer->EmuSurface8, NULL, NULL);
        */
    }
    else
    {
		DbgPrintf("EmuTexture8: = 0x%.08X\n", pPixelContainer->EmuTexture8 );

		if(pPixelContainer->Data == 0xFFFF0002)
		{
			hRet = E_FAIL;
		}
		else
		{
			hRet = pPixelContainer->EmuTexture8->GetLevelDesc(dwLevel, &SurfaceDesc);
			if(FAILED(hRet))
				EmuWarning("IDirect3DTexture8::GetSurfaceDesc failed!");
		}

        /*
        static int dwDumpTexture = 0;

        char szBuffer[255];

        sprintf(szBuffer, "C:\\Aaron\\Textures\\GetDescTexture%.03d.bmp", dwDumpTexture++);

        D3DXSaveTextureToFile(szBuffer, D3DXIFF_BMP, pPixelContainer->EmuTexture8, NULL);
        */
    }

    // rearrange into xbox format (remove D3DPOOL)
	if(SUCCEEDED(hRet))
    {
        // Convert Format (PC->Xbox)
        pDesc->Format = EmuPC2XB_D3DFormat(SurfaceDesc.Format);
        pDesc->Type   = (X_D3DRESOURCETYPE)SurfaceDesc.Type;

        if(pDesc->Type > 7)
            CxbxKrnlCleanup("EmuGet2DSurfaceDesc: pDesc->Type > 7");

        pDesc->Usage  = SurfaceDesc.Usage;
#ifndef D3D9
        pDesc->Size   = SurfaceDesc.Size;
#else
        // TODO: Size!
#endif

        // TODO: Convert from Xbox to PC!!
        if(SurfaceDesc.MultiSampleType == D3DMULTISAMPLE_NONE)
            pDesc->MultiSampleType = (XTL::D3DMULTISAMPLE_TYPE)0x0011;
        else
            CxbxKrnlCleanup("EmuGet2DSurfaceDesc Unknown Multisample format! (%d)", SurfaceDesc.MultiSampleType);

        pDesc->Width  = SurfaceDesc.Width;
        pDesc->Height = SurfaceDesc.Height;
    }

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuGet2DSurfaceDescD
// ******************************************************************
VOID WINAPI XTL::EmuGet2DSurfaceDescD
(
    X_D3DPixelContainer *pPixelContainer,
    X_D3DSURFACE_DESC   *pDesc
)
{
    // debug trace
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        DbgPrintf("EmuD3D8 (0x%X): EmuGet2DSurfaceDescD\n"
               "(\n"
               "   pPixelContainer     : 0x%.08X\n"
               "   pDesc               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pPixelContainer, pDesc);
        EmuSwapFS();   // Xbox FS
    }
    #endif

    EmuGet2DSurfaceDesc(pPixelContainer, 0xFEFEFEFE, pDesc);

    return;
}

// ******************************************************************
// * func: EmuIDirect3DSurface8_GetDesc
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DSurface8_GetDesc
(
    X_D3DResource      *pThis,
    X_D3DSURFACE_DESC  *pDesc
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DSurface8_GetDesc\n"
           "(\n"
           "   pThis               : 0x%.08X\n"
           "   pDesc               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, pDesc);

    HRESULT hRet;

    EmuVerifyResourceIsRegistered(pThis);

    if(IsSpecialResource(pThis->Data) && (pThis->Data & X_D3DRESOURCE_DATA_FLAG_YUVSURF))
    {
        pDesc->Format = EmuPC2XB_D3DFormat(D3DFMT_YUY2);
        pDesc->Height = g_dwOverlayH;
        pDesc->Width  = g_dwOverlayW;
        pDesc->MultiSampleType = (D3DMULTISAMPLE_TYPE)0;
        pDesc->Size   = g_dwOverlayP*g_dwOverlayH;
        pDesc->Type   = X_D3DRTYPE_SURFACE;
        pDesc->Usage  = 0;

        hRet = D3D_OK;
    }
    else
    {
#ifndef D3D9
        IDirect3DSurface8 *pSurface8 = pThis->EmuSurface8;
#else
        IDirect3DSurface9 *pSurface8 = pThis->EmuSurface8;
#endif

        D3DSURFACE_DESC SurfaceDesc;

        hRet = pSurface8->GetDesc(&SurfaceDesc);

        // rearrange into windows format (remove D3DPool)
        {
            // Convert Format (PC->Xbox)
            pDesc->Format = EmuPC2XB_D3DFormat(SurfaceDesc.Format);
            pDesc->Type   = (X_D3DRESOURCETYPE)SurfaceDesc.Type;

            if(pDesc->Type > 7)
                CxbxKrnlCleanup("EmuIDirect3DSurface8_GetDesc: pDesc->Type > 7");

            pDesc->Usage  = SurfaceDesc.Usage;
#ifndef D3D9
            pDesc->Size   = SurfaceDesc.Size;
#else
            // TODO: Size!
#endif

            // TODO: Convert from Xbox to PC!!
            if(SurfaceDesc.MultiSampleType == D3DMULTISAMPLE_NONE)
                pDesc->MultiSampleType = (XTL::D3DMULTISAMPLE_TYPE)0x0011;
            else
                CxbxKrnlCleanup("EmuIDirect3DSurface8_GetDesc Unknown Multisample format! (%d)", SurfaceDesc.MultiSampleType);

            pDesc->Width  = SurfaceDesc.Width;
            pDesc->Height = SurfaceDesc.Height;
        }
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DSurface8_LockRect
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DSurface8_LockRect
(
    X_D3DResource      *pThis,
    D3DLOCKED_RECT     *pLockedRect,
    CONST RECT         *pRect,
    DWORD               Flags
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DSurface8_LockRect\n"
           "(\n"
           "   pThis               : 0x%.08X\n"
           "   pLockedRect         : 0x%.08X\n"
           "   pRect               : 0x%.08X\n"
           "   Flags               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, pLockedRect, pRect, Flags);

    HRESULT hRet;

//	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DSurface8_LockRect (pThis->Surface = 0x%8.8X)\n", pThis->EmuSurface8 );

	if(!pThis->EmuSurface8)
	{
		EmuWarning("Invalid Surface!" );
		__asm int 3;
		EmuSwapFS();
		return E_FAIL;
	}
	
    EmuVerifyResourceIsRegistered(pThis);

    if(IsSpecialResource(pThis->Data) && (pThis->Data & X_D3DRESOURCE_DATA_FLAG_YUVSURF))
    {
        pLockedRect->Pitch = g_dwOverlayP;
        pLockedRect->pBits = (PVOID)pThis->Lock;

        hRet = D3D_OK;
    }
    else
    {
#ifndef D3D9
        IDirect3DSurface8 *pSurface8 = pThis->EmuSurface8;
#else
        IDirect3DSurface9 *pSurface8 = pThis->EmuSurface8;
#endif

        DWORD NewFlags = 0;

        if(Flags & 0x80)
            NewFlags |= D3DLOCK_READONLY;

        if(Flags & 0x40)
            EmuWarning("D3DLOCK_TILED ignored!");

        if(!(Flags & 0x80) && !(Flags & 0x40) && Flags != 0)
            CxbxKrnlCleanup("EmuIDirect3DSurface8_LockRect: Unknown Flags! (0x%.08X)", Flags);

		try
		{
			// Remove old lock(s)
			pSurface8->UnlockRect();

			hRet = pSurface8->LockRect(pLockedRect, pRect, NewFlags);

			if(FAILED(hRet))
				EmuWarning("LockRect Failed!");
		}
		catch(...)
		{
			EmuWarning("Invalid Surface!");
		}
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DBaseTexture8_GetLevelCount
// ******************************************************************
DWORD WINAPI XTL::EmuIDirect3DBaseTexture8_GetLevelCount
(
    X_D3DBaseTexture   *pThis
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DBaseTexture8_GetLevelCount\n"
           "(\n"
           "   pThis               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis);

    EmuVerifyResourceIsRegistered(pThis);

#ifndef D3D9
    IDirect3DBaseTexture8 *pBaseTexture8 = pThis->EmuBaseTexture8;
#else
    IDirect3DBaseTexture9 *pBaseTexture8 = pThis->EmuBaseTexture8;
#endif

    DWORD dwRet = pBaseTexture8->GetLevelCount();

    EmuSwapFS();   // XBox FS

    return dwRet;
}

// ******************************************************************
// * func: EmuIDirect3DTexture8_GetSurfaceLevel2
// ******************************************************************
XTL::X_D3DResource * WINAPI XTL::EmuIDirect3DTexture8_GetSurfaceLevel2
(
    X_D3DTexture   *pThis,
    UINT            Level
)
{
    X_D3DSurface *pSurfaceLevel;

    // In a special situation, we are actually returning a memory ptr with high bit set
    if(IsSpecialResource(pThis->Data) && (pThis->Data & X_D3DRESOURCE_DATA_FLAG_YUVSURF))
    {
        DWORD dwSize = g_dwOverlayP*g_dwOverlayH;

        DWORD *pRefCount = (DWORD*)((DWORD)pThis->Lock + dwSize);

        // initialize ref count
        (*pRefCount)++;

        return pThis;
    }

    EmuIDirect3DTexture8_GetSurfaceLevel(pThis, Level, &pSurfaceLevel);

    return pSurfaceLevel;
}

// ******************************************************************
// * func: EmuIDirect3DTexture8_LockRect
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DTexture8_LockRect
(
    X_D3DTexture   *pThis,
    UINT            Level,
    D3DLOCKED_RECT *pLockedRect,
    CONST RECT     *pRect,
    DWORD           Flags
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DTexture8_LockRect\n"
           "(\n"
           "   pThis               : 0x%.08X\n"
           "   Level               : 0x%.08X\n"
           "   pLockedRect         : 0x%.08X\n"
           "   pRect               : 0x%.08X\n"
           "   Flags               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, Level, pLockedRect, pRect, Flags);

    HRESULT hRet;

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DTexture8_LockRect (pThis->Texture = 0x%8.8X)\n", GetCurrentThreadId(), pThis->EmuTexture8);

    EmuVerifyResourceIsRegistered(pThis);

    // check if we have an unregistered YUV2 resource
    if( (pThis != 0) && IsSpecialResource(pThis->Data) && (pThis->Data & X_D3DRESOURCE_DATA_FLAG_YUVSURF))
    {
        pLockedRect->Pitch = g_dwOverlayP;
        pLockedRect->pBits = (PVOID)pThis->Lock;

        hRet = D3D_OK;
    }
    else
    {
#ifndef D3D9
        IDirect3DTexture8 *pTexture8 = pThis->EmuTexture8;
#else
        IDirect3DTexture9 *pTexture8 = pThis->EmuTexture8;
#endif

        DWORD NewFlags = 0;

        if(Flags & 0x80)
            NewFlags |= D3DLOCK_READONLY;

        if(Flags & 0x40)
            EmuWarning("D3DLOCK_TILED ignored!"); 

        if(Flags & 0x20)
            NewFlags |= D3DLOCK_NOOVERWRITE;

        if(Flags & 0x10)
            EmuWarning("D3DLOCK_NOFLUSH ignored!");

        if(!(Flags & 0x80) && !(Flags & 0x40) && !(Flags & 0x20) && !(Flags & 0x10) && Flags != 0)
            CxbxKrnlCleanup("EmuIDirect3DTexture8_LockRect: Unknown Flags! (0x%.08X)", Flags);

        // Remove old lock(s)
		if(Level == 6 || Level == 7 || Level == 8 || Level == 9)
		{
			// HACK: Unreal Championship crashes when the texture level reaches 9...
			EmuWarning("Unreal Championship texture hack applied!");
			hRet = D3DERR_INVALIDCALL;
		}
		else
		{
			pTexture8->UnlockRect(Level);

			hRet = pTexture8->LockRect(Level, pLockedRect, pRect, NewFlags);

			pThis->Common |= X_D3DCOMMON_ISLOCKED;
		}
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DTexture8_GetSurfaceLevel
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DTexture8_GetSurfaceLevel
(
    X_D3DTexture       *pThis,
    UINT                Level,
    X_D3DSurface      **ppSurfaceLevel
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DTexture8_GetSurfaceLevel\n"
           "(\n"
           "   pThis               : 0x%.08X\n"
           "   Level               : 0x%.08X\n"
           "   ppSurfaceLevel      : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, Level, ppSurfaceLevel);

    HRESULT hRet = D3DERR_INVALIDCALL;

    EmuVerifyResourceIsRegistered(pThis);

	if(pThis)
	{
		// if highest bit is set, this is actually a raw memory pointer (for YUY2 simulation)
		if(IsSpecialResource(pThis->Data) && (pThis->Data & X_D3DRESOURCE_DATA_FLAG_YUVSURF))
		{
			DWORD dwSize = g_dwOverlayP*g_dwOverlayH;

			DWORD *pRefCount = (DWORD*)((DWORD)pThis->Lock + dwSize);

			// initialize ref count
			(*pRefCount)++;

			*ppSurfaceLevel = (X_D3DSurface*)pThis;

			hRet = D3D_OK;
		}
		else
		{
#ifndef D3D9
			IDirect3DTexture8 *pTexture8 = pThis->EmuTexture8;
#else
            IDirect3DTexture9 *pTexture8 = pThis->EmuTexture8;
#endif

			*ppSurfaceLevel = new X_D3DSurface();

			(*ppSurfaceLevel)->Data = 0xB00BBABE;
			(*ppSurfaceLevel)->Common = 0;
			(*ppSurfaceLevel)->Format = 0;
			(*ppSurfaceLevel)->Size = 0;

			hRet = pTexture8->GetSurfaceLevel(Level, &((*ppSurfaceLevel)->EmuSurface8));

			if(FAILED(hRet))
			{
				EmuWarning("EmuIDirect3DTexture8_GetSurfaceLevel Failed!\n");
			}
			else
			{
				DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DTexture8_GetSurfaceLevel := 0x%.08X\n", GetCurrentThreadId(), (*ppSurfaceLevel)->EmuSurface8);
			}
		}
	}

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DVolumeTexture8_LockBox
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DVolumeTexture8_LockBox
(
    X_D3DVolumeTexture *pThis,
    UINT                Level,
    D3DLOCKED_BOX      *pLockedVolume,
    CONST D3DBOX       *pBox,
    DWORD               Flags
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DVolumeTexture8_LockBox\n"
           "(\n"
           "   pThis               : 0x%.08X\n"
           "   Level               : 0x%.08X\n"
           "   pLockedVolume       : 0x%.08X\n"
           "   pBox                : 0x%.08X\n"
           "   Flags               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, Level, pLockedVolume, pBox, Flags);

    EmuVerifyResourceIsRegistered(pThis);

#ifndef D3D9
    IDirect3DVolumeTexture8 *pVolumeTexture8 = pThis->EmuVolumeTexture8;
#else
    IDirect3DVolumeTexture9 *pVolumeTexture8 = pThis->EmuVolumeTexture8;
#endif

    HRESULT hRet = pVolumeTexture8->LockBox(Level, pLockedVolume, pBox, Flags);

    if(FAILED(hRet))
        EmuWarning("LockBox Failed!");

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DCubeTexture8_LockRect
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DCubeTexture8_LockRect
(
    X_D3DCubeTexture   *pThis,
    D3DCUBEMAP_FACES    FaceType,
    UINT                Level,
    D3DLOCKED_RECT     *pLockedBox,
    CONST RECT         *pRect,
    DWORD               Flags
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DCubeTexture8_LockRect\n"
           "(\n"
           "   pThis               : 0x%.08X\n"
           "   FaceType            : 0x%.08X\n"
           "   Level               : 0x%.08X\n"
           "   pLockedBox          : 0x%.08X\n"
           "   pRect               : 0x%.08X\n"
           "   Flags               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, FaceType, Level, pLockedBox, pRect, Flags);

    EmuVerifyResourceIsRegistered(pThis);

#ifndef D3D9
    IDirect3DCubeTexture8 *pCubeTexture8 = pThis->EmuCubeTexture8;
#else
    IDirect3DCubeTexture9 *pCubeTexture8 = pThis->EmuCubeTexture8;
#endif

    HRESULT hRet = pCubeTexture8->LockRect(FaceType, Level, pLockedBox, pRect, Flags);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_Release
// ******************************************************************
ULONG WINAPI XTL::EmuIDirect3DDevice8_Release()
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_Release();\n", GetCurrentThreadId());

    g_pD3DDevice->AddRef();
    DWORD RefCount = g_pD3DDevice->Release();
    if (RefCount == 1)
    {
        // Signal proxy thread, and wait for completion
        g_EmuCDPD.bReady = true;
        g_EmuCDPD.bCreate = false;

        while(g_EmuCDPD.bReady)
            Sleep(10);
        RefCount = g_EmuCDPD.hRet;
    }
    else
    {
        RefCount = g_pD3DDevice->Release();
    }

    EmuSwapFS();   // XBox FS

    return RefCount;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateVertexBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateVertexBuffer
(
    UINT                Length,
    DWORD               Usage,
    DWORD               FVF,
    D3DPOOL             Pool,
    X_D3DVertexBuffer **ppVertexBuffer
)
{
    *ppVertexBuffer = EmuIDirect3DDevice8_CreateVertexBuffer2(Length);

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateVertexBuffer2
// ******************************************************************
XTL::X_D3DVertexBuffer* WINAPI XTL::EmuIDirect3DDevice8_CreateVertexBuffer2
(
    UINT Length
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreateVertexBuffer2\n"
           "(\n"
           "   Length              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Length);

    X_D3DVertexBuffer *pD3DVertexBuffer = new X_D3DVertexBuffer();

    HRESULT hRet = g_pD3DDevice->CreateVertexBuffer
    (
        Length,
        0,
        0,
        D3DPOOL_MANAGED,
#ifndef D3D9
        &pD3DVertexBuffer->EmuVertexBuffer8
#else
        &pD3DVertexBuffer->EmuVertexBuffer8, NULL
#endif
    );

    if(FAILED(hRet))
        CxbxKrnlCleanup("CreateVertexBuffer Failed!");

    #ifdef _DEBUG_TRACK_VB
    g_VBTrackTotal.insert(pD3DVertexBuffer->EmuVertexBuffer8);
    #endif

    EmuSwapFS();   // XBox FS

    return pD3DVertexBuffer;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_EnableOverlay
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_EnableOverlay
(
    BOOL Enable
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_EnableOverlay\n"
           "(\n"
           "   Enable              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Enable);

    if(Enable == FALSE && (g_pDDSOverlay7 != NULL))
    {
        g_pDDSOverlay7->UpdateOverlay(NULL, g_pDDSPrimary, NULL, DDOVER_HIDE, 0);

        // cleanup overlay clipper
        if(g_pDDClipper != 0)
        {
            g_pDDClipper->Release();
            g_pDDClipper = 0;
        }

        // cleanup overlay surface
        if(g_pDDSOverlay7 != 0)
        {
            g_pDDSOverlay7->Release();
            g_pDDSOverlay7 = 0;
        }
    }
    else if(Enable == TRUE && (g_pDDSOverlay7 == 0))
    {
        // initialize overlay surface
        if(g_bSupportsYUY2)
        {
            XTL::DDSURFACEDESC2 ddsd2;

            ZeroMemory(&ddsd2, sizeof(ddsd2));

            ddsd2.dwSize = sizeof(ddsd2);
            ddsd2.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
            ddsd2.ddsCaps.dwCaps = DDSCAPS_OVERLAY;
            ddsd2.dwWidth = g_dwOverlayW;
            ddsd2.dwHeight = g_dwOverlayH;
            ddsd2.ddpfPixelFormat.dwSize = sizeof(XTL::DDPIXELFORMAT);
            ddsd2.ddpfPixelFormat.dwFlags = DDPF_FOURCC;
            ddsd2.ddpfPixelFormat.dwFourCC = MAKEFOURCC('Y','U','Y','2');

            HRESULT hRet = g_pDD7->CreateSurface(&ddsd2, &g_pDDSOverlay7, NULL);

            if(FAILED(hRet))
                CxbxKrnlCleanup("Could not create overlay surface");

            hRet = g_pDD7->CreateClipper(0, &g_pDDClipper, NULL);

            if(FAILED(hRet))
                CxbxKrnlCleanup("Could not create overlay clipper");

            hRet = g_pDDClipper->SetHWnd(0, g_hEmuWindow);
        }
    }

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_UpdateOverlay
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_UpdateOverlay
(
    X_D3DSurface *pSurface,
    CONST RECT   *SrcRect,
    CONST RECT   *DstRect,
    BOOL          EnableColorKey,
    D3DCOLOR      ColorKey
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_UpdateOverlay\n"
           "(\n"
           "   pSurface            : 0x%.08X\n"
           "   SrcRect             : 0x%.08X\n"
           "   DstRect             : 0x%.08X\n"
           "   EnableColorKey      : 0x%.08X\n"
           "   ColorKey            : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pSurface, SrcRect, DstRect, EnableColorKey, ColorKey);

#ifndef UnrealChampionshipHack
	if(pSurface)
	{
		// manually copy data over to overlay
		if(g_bSupportsYUY2)
		{
			DDSURFACEDESC2  ddsd2;

			ZeroMemory(&ddsd2, sizeof(ddsd2));

			ddsd2.dwSize = sizeof(ddsd2);

			if(FAILED(g_pDDSOverlay7->Lock(NULL, &ddsd2, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL)))
				EmuWarning("Unable to lock overlay surface!");

			// copy data
			{
				char *pDest = (char*)ddsd2.lpSurface;
				char *pSour = (char*)pSurface->Lock;

				int w = g_dwOverlayW;
				int h = g_dwOverlayH;

				// TODO: sucker the game into rendering directly to the overlay (speed boost)
				if( (ddsd2.lPitch == w*2) && ((int)g_dwOverlayP == w*2) )
					memcpy(pDest, pSour, h*w*2);
				else
				{
					for(int y=0;y<h;y++)
					{
						memcpy(pDest, pSour, w*2);

						pDest += ddsd2.lPitch;
						pSour += g_dwOverlayP;
					}
				}
			}

			g_pDDSOverlay7->Unlock(NULL);
		}

		// update overlay!
		if(g_bSupportsYUY2)
		{
			RECT SourRect = {0, 0, g_dwOverlayW, g_dwOverlayH}, DestRect;
			MONITORINFO MonitorInfo = {0};

			int nTitleHeight  = 0;//GetSystemMetrics(SM_CYCAPTION);
			int nBorderWidth  = 0;//GetSystemMetrics(SM_CXSIZEFRAME);
			int nBorderHeight = 0;//GetSystemMetrics(SM_CYSIZEFRAME);

			MonitorInfo.cbSize = sizeof(MONITORINFO);
			GetMonitorInfo(g_hMonitor, &MonitorInfo);

			GetWindowRect(g_hEmuWindow, &DestRect);

			DestRect.left   += nBorderWidth;
			DestRect.right  -= nBorderWidth;
			DestRect.top    += nTitleHeight + nBorderHeight;
			DestRect.bottom -= nBorderHeight;

			DestRect.left   -= MonitorInfo.rcMonitor.left;
			DestRect.right  -= MonitorInfo.rcMonitor.left;
			DestRect.top    -= MonitorInfo.rcMonitor.top;
			DestRect.bottom -= MonitorInfo.rcMonitor.top;

			DDOVERLAYFX ddofx;

			ZeroMemory(&ddofx, sizeof(ddofx));

			ddofx.dwSize = sizeof(DDOVERLAYFX);
			ddofx.dckDestColorkey.dwColorSpaceLowValue = 0;
			ddofx.dckDestColorkey.dwColorSpaceHighValue = 0;

			HRESULT hRet = g_pDDSOverlay7->UpdateOverlay(&SourRect, g_pDDSPrimary, &DestRect, /*DDOVER_KEYDESTOVERRIDE | */DDOVER_SHOW, /*&ddofx*/0);
		}
		else
		{
			// TODO: dont assume X8R8G8B8 ?
			D3DLOCKED_RECT LockedRectDest;

#ifndef D3D9
			IDirect3DSurface8 *pBackBuffer=0;
#else
            IDirect3DSurface9 *pBackBuffer=0;
#endif

#ifndef D3D9
			HRESULT hRet = g_pD3DDevice->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
#else
            HRESULT hRet = g_pD3DDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
#endif

			// if we obtained the backbuffer, manually translate the YUY2 into the backbuffer format
			if(hRet == D3D_OK && pBackBuffer->LockRect(&LockedRectDest, NULL, NULL) == D3D_OK)
			{
				uint08 *pCurByte = (uint08*)pSurface->Lock;

				uint08 *pDest = (uint08*)LockedRectDest.pBits;

				uint32 dx=0, dy=0;

				uint32 dwImageSize = g_dwOverlayP*g_dwOverlayH;

				// grayscale
				if(false)
				{
					for(uint32 y=0;y<g_dwOverlayH;y++)
					{
						uint32 stop = g_dwOverlayW*4;
						for(uint32 x=0;x<stop;x+=4)
						{
							uint08 Y = *pCurByte;

							pDest[x+0] = Y;
							pDest[x+1] = Y;
							pDest[x+2] = Y;
							pDest[x+3] = 0xFF;

							pCurByte+=2;
						}

						pDest += LockedRectDest.Pitch;
					}
				}
				// full color conversion (YUY2->XRGB)
				else
				{
					for(uint32 v=0;v<dwImageSize;v+=4)
					{
						float Y[2], U, V;

						Y[0] = *pCurByte++;
						U    = *pCurByte++;
						Y[1] = *pCurByte++;
						V    = *pCurByte++;

						int a=0;
						for(int x=0;x<2;x++)
						{
							float R = Y[a] + 1.402f*(V-128);
							float G = Y[a] - 0.344f*(U-128) - 0.714f*(V-128);
							float B = Y[a] + 1.772f*(U-128);

							R = (R < 0) ? 0 : ((R > 255) ? 255 : R);
							G = (G < 0) ? 0 : ((G > 255) ? 255 : G);
							B = (B < 0) ? 0 : ((B > 255) ? 255 : B);

							uint32 i = (dy*LockedRectDest.Pitch+(dx+x)*4);

							pDest[i+0] = (uint08)B;
							pDest[i+1] = (uint08)G;
							pDest[i+2] = (uint08)R;
							pDest[i+3] = 0xFF;

							a++;
						}

						dx+=2;

						if((dx % g_dwOverlayW) == 0)
						{
							dy++;
							dx=0;
						}

					}
				}

				pBackBuffer->UnlockRect();
			}

			// Update overlay if present was not called since the last call to
			// EmuIDirect3DDevice8_UpdateOverlay.
			if(g_bHackUpdateSoftwareOverlay)
				g_pD3DDevice->Present(0, 0, 0, 0);

			g_bHackUpdateSoftwareOverlay = TRUE;
		}
	}
	else
	{
		EmuWarning("pSurface == NULL!");
	}
#endif

    EmuSwapFS();   // XBox FS

    return;
}
// ******************************************************************
// * func: EmuIDirect3DDevice8_GetOverlayUpdateStatus
// ******************************************************************
BOOL WINAPI XTL::EmuIDirect3DDevice8_GetOverlayUpdateStatus()
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetOverlayUpdateStatus();\n",
           GetCurrentThreadId());

    EmuSwapFS();   // XBox FS

    // TODO: Actually check for update status
    return FALSE;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_BlockUntilVerticalBlank
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_BlockUntilVerticalBlank()
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_BlockUntilVerticalBlank();\n",
           GetCurrentThreadId());

    // segaGT tends to freeze with this on
//    if(g_XBVideo.GetVSync())
//        g_pDD7->WaitForVerticalBlank(DDWAITVB_BLOCKBEGIN, 0);

	// HACK: For many games (when used in mutithreaded code), using 
	// DDraw::WaitForVerticalBlank will wreak havoc on CPU usage and really
	// slow things down. This is the case for various SEGA and other Japanese
	// titles.  On Xbox, it isn't a big deal, but for PC, I can't even
	// guarantee this is a good idea.  So instead, I'll be "faking" 
	// the vertical blank thing.

	Sleep( 1000/60 );

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVerticalBlankCallback
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetVerticalBlankCallback
(
    D3DVBLANKCALLBACK pCallback
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVerticalBlankCallback\n"
           "(\n"
           "   pCallback           : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pCallback);

    g_pVBCallback = pCallback;

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTextureState_TexCoordIndex
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetTextureState_TexCoordIndex
(
    DWORD Stage,
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetTextureState_TexCoordIndex\n"
           "(\n"
           "   Stage               : 0x%.08X\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Stage, Value);

	// TODO: Xbox Direct3D supports sphere mapping OpenGL style.

	// BUG FIX: The lower 16 bits were causing false Unknown TexCoordIndex errors.
	// Check for 0x00040000 instead.

    if(Value >= 0x00040000)
        CxbxKrnlCleanup("EmuIDirect3DDevice8_SetTextureState_TexCoordIndex: Unknown TexCoordIndex Value (0x%.08X)", Value);

    g_pD3DDevice->SetTextureStageState(Stage, D3DTSS_TEXCOORDINDEX, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTextureState_TwoSidedLighting
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetTextureState_TwoSidedLighting
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetTextureState_TwoSidedLighting\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    EmuWarning("TwoSidedLighting is not supported!");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_BackFillMode
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_BackFillMode
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_BackFillMode\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    EmuWarning("BackFillMode is not supported!");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTextureState_BorderColor
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetTextureState_BorderColor
(
    DWORD Stage,
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetTextureState_BorderColor\n"
           "(\n"
           "   Stage               : 0x%.08X\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Stage, Value);

#ifndef D3D9
    g_pD3DDevice->SetTextureStageState(Stage, D3DTSS_BORDERCOLOR, Value);
#else
    g_pD3DDevice->SetSamplerState(Stage, D3DSAMP_BORDERCOLOR, Value);
#endif

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTextureState_ColorKeyColor
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetTextureState_ColorKeyColor
(
    DWORD Stage,
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetTextureState_ColorKeyColor\n"
           "(\n"
           "   Stage               : 0x%.08X\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Stage, Value);

    EmuWarning("SetTextureState_ColorKeyColor is not supported!");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTextureState_BumpEnv
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetTextureState_BumpEnv
(
    DWORD                      Stage,
    X_D3DTEXTURESTAGESTATETYPE Type,
    DWORD                      Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetTextureState_BumpEnv\n"
           "(\n"
           "   Stage               : 0x%.08X\n"
           "   Type                : 0x%.08X\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Stage, Type, Value);

    switch(Type)
    {
        case 22:    // X_D3DTSS_BUMPENVMAT00
            g_pD3DDevice->SetTextureStageState(Stage, D3DTSS_BUMPENVMAT00, Value);
            break;
        case 23:    // X_D3DTSS_BUMPENVMAT01
            g_pD3DDevice->SetTextureStageState(Stage, D3DTSS_BUMPENVMAT01, Value);
            break;
        case 24:    // X_D3DTSS_BUMPENVMAT11
            g_pD3DDevice->SetTextureStageState(Stage, D3DTSS_BUMPENVMAT11, Value);
            break;
        case 25:    // X_D3DTSS_BUMPENVMAT10
            g_pD3DDevice->SetTextureStageState(Stage, D3DTSS_BUMPENVMAT10, Value);
            break;
        case 26:    // X_D3DTSS_BUMPENVLSCALE
            g_pD3DDevice->SetTextureStageState(Stage, D3DTSS_BUMPENVLSCALE, Value);
            break;
    }

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_FrontFace
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_FrontFace
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_FrontFace\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    EmuWarning("SetRenderState_FrontFace not supported!\n");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_LogicOp
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_LogicOp
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_LogicOp\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    EmuWarning("SetRenderState_LogicOp is not supported!");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_NormalizeNormals
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_NormalizeNormals
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_NormalizeNormals\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    g_pD3DDevice->SetRenderState(D3DRS_NORMALIZENORMALS, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_TextureFactor
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_TextureFactor
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_TextureFactor\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    g_pD3DDevice->SetRenderState(D3DRS_TEXTUREFACTOR, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_ZBias
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_ZBias
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_ZBias\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

#ifndef D3D9
    g_pD3DDevice->SetRenderState(D3DRS_ZBIAS, Value);
#else
    // Using the new DirectX 9 Depth Bias
    // https://software.intel.com/en-us/articles/alternatives-to-using-z-bias-to-fix-z-fighting-issues
    g_pD3DDevice->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, 1 /*F2DW1.0)*/); // Check this one
    g_pD3DDevice->SetRenderState(D3DRS_DEPTHBIAS, Value);
#endif

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_EdgeAntiAlias
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_EdgeAntiAlias
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_EdgeAntiAlias\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

//  TODO: Analyze performance and compatibility (undefined behavior on PC with triangles or points)
//  g_pD3DDevice->SetRenderState(D3DRS_EDGEANTIALIAS, Value);

//    EmuWarning("SetRenderState_EdgeAntiAlias not implemented!");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_FillMode
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_FillMode
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_FillMode\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    DWORD dwFillMode;

    if(g_iWireframe == 0)
        dwFillMode = EmuXB2PC_D3DFILLMODE(Value);
    else if(g_iWireframe == 1)
        dwFillMode = D3DFILL_WIREFRAME;
    else
        dwFillMode = D3DFILL_POINT;

    g_pD3DDevice->SetRenderState(D3DRS_FILLMODE, dwFillMode);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_FogColor
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_FogColor
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_FogColor\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    g_pD3DDevice->SetRenderState(D3DRS_FOGCOLOR, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_Dxt1NoiseEnable
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_Dxt1NoiseEnable
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_Dxt1NoiseEnable\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    EmuWarning("SetRenderState_Dxt1NoiseEnable not implemented!");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_Simple
// ******************************************************************
VOID __fastcall XTL::EmuIDirect3DDevice8_SetRenderState_Simple
(
    DWORD Method,
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_Simple\n"
           "(\n"
           "   Method              : 0x%.08X\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Method, Value);

    int State = -1;

    // Todo: make this faster and more elegant
    for(int v=0;v<174;v++)
    {
        if(EmuD3DRenderStateSimpleEncoded[v] == Method)
        {
            State = v;
            break;
        }
    }

    if(State == -1)
        EmuWarning("RenderState_Simple(0x%.08X, 0x%.08X) is unsupported!", Method, Value);
    else
    {
        switch(State)
        {
            case D3DRS_COLORWRITEENABLE:
            {
                DWORD OrigValue = Value;

                Value = 0;

                if(OrigValue & (1L<<16))
                    Value |= D3DCOLORWRITEENABLE_RED;
                if(OrigValue & (1L<<8))
                    Value |= D3DCOLORWRITEENABLE_GREEN;
                if(OrigValue & (1L<<0))
                    Value |= D3DCOLORWRITEENABLE_BLUE;
                if(OrigValue & (1L<<24))
                    Value |= D3DCOLORWRITEENABLE_ALPHA;

                DbgPrintf("D3DRS_COLORWRITEENABLE := 0x%.08X\n", Value);
            }
            break;

            case D3DRS_SHADEMODE:
                Value = EmuXB2PC_D3DSHADEMODE(Value);
                DbgPrintf("D3DRS_SHADEMODE := 0x%.08X\n", Value);
                break;

            case D3DRS_BLENDOP:
                Value = EmuXB2PC_D3DBLENDOP(Value);
                DbgPrintf("D3DRS_BLENDOP := 0x%.08X\n", Value);
                break;

            case D3DRS_SRCBLEND:
                Value = EmuXB2PC_D3DBLEND(Value);
                DbgPrintf("D3DRS_SRCBLEND := 0x%.08X\n", Value);
                break;

            case D3DRS_DESTBLEND:
                Value = EmuXB2PC_D3DBLEND(Value);
                DbgPrintf("D3DRS_DESTBLEND := 0x%.08X\n", Value);
                break;

            case D3DRS_ZFUNC:
                Value = EmuXB2PC_D3DCMPFUNC(Value);
                DbgPrintf("D3DRS_ZFUNC := 0x%.08X\n", Value);
                break;

            case D3DRS_ALPHAFUNC:
                Value = EmuXB2PC_D3DCMPFUNC(Value);
                DbgPrintf("D3DRS_ALPHAFUNC := 0x%.08X\n", Value);
                break;

            case D3DRS_ALPHATESTENABLE:
                DbgPrintf("D3DRS_ALPHATESTENABLE := 0x%.08X\n", Value);
                break;

            case D3DRS_ALPHABLENDENABLE:
                DbgPrintf("D3DRS_ALPHABLENDENABLE := 0x%.08X\n", Value);
                break;

            case D3DRS_ALPHAREF:
                DbgPrintf("D3DRS_ALPHAREF := %lf\n", DWtoF(Value));
                break;

            case D3DRS_ZWRITEENABLE:
                DbgPrintf("D3DRS_ZWRITEENABLE := 0x%.08X\n", Value);
                break;

            case D3DRS_DITHERENABLE:
                DbgPrintf("D3DRS_DITHERENABLE := 0x%.08X\n", Value);
                break;

			case D3DRS_STENCILZFAIL:
				Value = EmuXB2PC_D3DSTENCILOP(Value);
				DbgPrintf("D3DRS_STENCILZFAIL := 0x%.08X\n", Value);
				break;

			case D3DRS_STENCILPASS:
				Value = EmuXB2PC_D3DSTENCILOP(Value);
				DbgPrintf("D3DRS_STENCILPASS := 0x%.08X\n", Value);
				break;

			case D3DRS_STENCILFUNC:
				Value = EmuXB2PC_D3DCMPFUNC(Value);
				DbgPrintf("D3DRS_STENCILFUNC := 0x%.08X\n", Value);
				break;

			case D3DRS_STENCILREF:
				DbgPrintf("D3DRS_STENCILREF := 0x%.08X\n", Value);
				break;

			case D3DRS_STENCILMASK:
				DbgPrintf("D3DRS_STENCILMASK := 0x%.08X\n", Value);
				break;

			case D3DRS_STENCILWRITEMASK:
				DbgPrintf("D3DRS_STENCILWRITEMASK := 0x%.08X\n", Value);
				break;

            default:
                CxbxKrnlCleanup("Unsupported RenderState (0x%.08X)", State);
                break;
        };

        // TODO: verify these params as you add support for them!
        g_pD3DDevice->SetRenderState((D3DRENDERSTATETYPE)State, Value);
    }

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_VertexBlend
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_VertexBlend
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_VertexBlend\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    // convert from Xbox direct3d to PC direct3d enumeration
    if(Value <= 1)
        Value = Value;
    else if(Value == 3)
        Value = 2;
    else if(Value == 5)
        Value = 3;
    else
        CxbxKrnlCleanup("Unsupported D3DVERTEXBLENDFLAGS (%d)", Value);

    g_pD3DDevice->SetRenderState(D3DRS_VERTEXBLEND, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_PSTextureModes
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_PSTextureModes
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_PSTextureModes\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    // TODO: do something..

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_CullMode
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_CullMode
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_CullMode\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    // convert from Xbox D3D to PC D3D enumeration
    // TODO: XDK-Specific Tables? So far they are the same
    switch(Value)
    {
        case 0:
            Value = D3DCULL_NONE;
            break;
        case 0x900:
            Value = D3DCULL_CW;
            break;
        case 0x901:
            Value = D3DCULL_CCW;
            break;
        default:
            CxbxKrnlCleanup("EmuIDirect3DDevice8_SetRenderState_CullMode: Unknown Cullmode (%d)", Value);
    }

    g_pD3DDevice->SetRenderState(D3DRS_CULLMODE, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_LineWidth
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_LineWidth
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_LineWidth\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    // TODO: Convert to PC format??
//    g_pD3DDevice->SetRenderState(D3DRS_LINEPATTERN, Value);
    EmuWarning("SetRenderState_LineWidth is not supported!");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_StencilFail
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_StencilFail
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_StencilFail\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    g_pD3DDevice->SetRenderState(D3DRS_STENCILFAIL, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_OcclusionCullEnable
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_OcclusionCullEnable
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_OcclusionCullEnable\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    EmuWarning("SetRenderState_OcclusionCullEnable not supported!");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_StencilCullEnable
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_StencilCullEnable
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_StencilCullEnable\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    EmuWarning("SetRenderState_StencilCullEnable not supported!");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_RopZCmpAlwaysRead
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_RopZCmpAlwaysRead
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_RopZCmpAlwaysRead\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    EmuWarning("SetRenderState_RopZCmpAlwaysRead not supported!");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_RopZRead
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_RopZRead
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_RopZRead\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    EmuWarning("SetRenderState_RopZRead not supported!");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_DoNotCullUncompressed
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_DoNotCullUncompressed
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_DoNotCullUncompressed\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    EmuWarning("SetRenderState_DoNotCullUncompressed not supported!");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_ZEnable
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_ZEnable
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_ZEnable\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    g_pD3DDevice->SetRenderState(D3DRS_ZENABLE, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_StencilEnable
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_StencilEnable
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_StencilEnable\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    g_pD3DDevice->SetRenderState(D3DRS_STENCILENABLE, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_MultiSampleAntiAlias
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_MultiSampleAntiAlias
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_MultiSampleAntiAlias\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    g_pD3DDevice->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_MultiSampleMask
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_MultiSampleMask
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_MultiSampleMask\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    g_pD3DDevice->SetRenderState(D3DRS_MULTISAMPLEMASK, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_MultiSampleMode
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_MultiSampleMode
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_MultiSampleMode\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    EmuWarning("SetRenderState_MultiSampleMode is not supported!");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_MultiSampleRenderTargetMode
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_MultiSampleRenderTargetMode
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_MultiSampleRenderTargetMode\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    EmuWarning("SetRenderState_MultiSampleRenderTargetMode is not supported!");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_ShadowFunc
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_ShadowFunc
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_ShadowFunc\n"
           "(\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

    // ShadowFunc reflects the following Xbox-only extension
    //
    // typedef enum _D3DRENDERSTATETYPE {
    //   ...
    //   D3DRS_SHADOWFUNC = 156, // D3DCMPFUNC
    //   ...
    // } D3DRENDERSTATETYPE;
    //
    // Value is a member of the D3DCMPFUNC enumeration that 
    // specifies what function to use with a shadow buffer. 
    // The default value is D3DCMP_NEVER. 

    // EmuXB2PC_D3DCMPFUNC(Value);

    // this warning just gets annoying
    // EmuWarning("ShadowFunc not implemented");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_YuvEnable
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_YuvEnable
(
    BOOL Enable
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_YuvEnable\n"
           "(\n"
           "   Enable              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Enable);

    // HACK: Display YUV surface by using an overlay.
    if(Enable != g_fYuvEnabled)
    {
        g_fYuvEnabled = Enable;

        EmuWarning("EmuIDirect3DDevice8_SetRenderState_YuvEnable using overlay!");

        EmuSwapFS();
        EmuIDirect3DDevice8_EnableOverlay(g_fYuvEnabled);
        EmuSwapFS();
    }

    if(g_fYuvEnabled)
    {
        EmuSwapFS();
        EmuIDirect3DDevice8_UpdateOverlay(g_YuvSurface, 0, 0, FALSE, 0);
        EmuSwapFS();
    }

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTransform
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetTransform
(
    D3DTRANSFORMSTATETYPE State,
    CONST D3DMATRIX      *pMatrix
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetTransform\n"
           "(\n"
           "   State               : 0x%.08X\n"
           "   pMatrix             : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), State, pMatrix);

    /*
    printf("pMatrix (%d)\n", State);
    printf("{\n");
    printf("    %.08f,%.08f,%.08f,%.08f\n", pMatrix->_11, pMatrix->_12, pMatrix->_13, pMatrix->_14);
    printf("    %.08f,%.08f,%.08f,%.08f\n", pMatrix->_21, pMatrix->_22, pMatrix->_23, pMatrix->_24);
    printf("    %.08f,%.08f,%.08f,%.08f\n", pMatrix->_31, pMatrix->_32, pMatrix->_33, pMatrix->_34);
    printf("    %.08f,%.08f,%.08f,%.08f\n", pMatrix->_41, pMatrix->_42, pMatrix->_43, pMatrix->_44);
    printf("}\n");

    if(State == 6 && (pMatrix->_11 == 1.0f) && (pMatrix->_22 == 1.0f) && (pMatrix->_33 == 1.0f) && (pMatrix->_44 == 1.0f))
    {
        g_bSkipPush = TRUE;
        printf("SkipPush ON\n");
    }
    else
    {
        g_bSkipPush = FALSE;
        printf("SkipPush OFF\n");
    }
    */

    State = EmuXB2PC_D3DTS(State);

    HRESULT hRet = g_pD3DDevice->SetTransform(State, pMatrix);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetTransform
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetTransform
(
    D3DTRANSFORMSTATETYPE State,
    D3DMATRIX            *pMatrix
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetTransform\n"
           "(\n"
           "   State               : 0x%.08X\n"
           "   pMatrix             : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), State, pMatrix);

    State = EmuXB2PC_D3DTS(State);

    HRESULT hRet = g_pD3DDevice->GetTransform(State, pMatrix);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DVertexBuffer8_Lock
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DVertexBuffer8_Lock
(
    X_D3DVertexBuffer  *ppVertexBuffer,
    UINT                OffsetToLock,
    UINT                SizeToLock,
    BYTE              **ppbData,
    DWORD               Flags
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DVertexBuffer8_Lock\n"
           "(\n"
           "   ppVertexBuffer      : 0x%.08X\n"
           "   OffsetToLock        : 0x%.08X\n"
           "   SizeToLock          : 0x%.08X\n"
           "   ppbData             : 0x%.08X\n"
           "   Flags               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), ppVertexBuffer, OffsetToLock, SizeToLock, ppbData, Flags);

#ifndef D3D9
    IDirect3DVertexBuffer8 *pVertexBuffer8 = ppVertexBuffer->EmuVertexBuffer8;
#else
    IDirect3DVertexBuffer9 *pVertexBuffer8 = ppVertexBuffer->EmuVertexBuffer8;
#endif

#ifndef D3D9
    HRESULT hRet = pVertexBuffer8->Lock(OffsetToLock, SizeToLock, ppbData, Flags);
#else
    HRESULT hRet = pVertexBuffer8->Lock(OffsetToLock, SizeToLock, (void**)ppbData, Flags);
#endif

    if(FAILED(hRet))
        EmuWarning("VertexBuffer Lock Failed!");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DVertexBuffer8_Lock2
// ******************************************************************
BYTE* WINAPI XTL::EmuIDirect3DVertexBuffer8_Lock2
(
    X_D3DVertexBuffer  *ppVertexBuffer,
    DWORD               Flags
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DVertexBuffer8_Lock2\n"
           "(\n"
           "   ppVertexBuffer      : 0x%.08X\n"
           "   Flags               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), ppVertexBuffer, Flags);

#ifndef D3D9
    IDirect3DVertexBuffer8 *pVertexBuffer8 = ppVertexBuffer->EmuVertexBuffer8;
#else
    IDirect3DVertexBuffer9 *pVertexBuffer8 = ppVertexBuffer->EmuVertexBuffer8;
#endif

    BYTE *pbData = NULL;

#ifndef D3D9
    HRESULT hRet = pVertexBuffer8->Lock(0, 0, &pbData, EmuXB2PC_D3DLock(Flags));    // Fixed flags check, Battlestar Galactica now displays graphics correctly
#else
    HRESULT hRet = pVertexBuffer8->Lock(0, 0, (void**)&pbData, EmuXB2PC_D3DLock(Flags));    // Fixed flags check, Battlestar Galactica now displays graphics correctly
#endif

    EmuSwapFS();   // XBox FS

    return pbData;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetStreamSource2
// ******************************************************************
XTL::X_D3DVertexBuffer* WINAPI XTL::EmuIDirect3DDevice8_GetStreamSource2
(
    UINT  StreamNumber,
    UINT *pStride
)
{
    EmuSwapFS();

    // debug trace
    DbgPrintf( "EmuD3D8 (0x%.08X): EmuIDirect3DDevice8_GetStreamSource2\n"
               "(\n"
               "   StreamNumber               : 0x%.08X\n"
               "   pStride                    : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), StreamNumber, pStride);

    EmuWarning("Not correctly implemented yet!");
    X_D3DVertexBuffer* pVertexBuffer = new X_D3DVertexBuffer();
#ifndef D3D9
    g_pD3DDevice->GetStreamSource(StreamNumber, (struct XTL::IDirect3DVertexBuffer8 **)&pVertexBuffer, pStride);
#else
    g_pD3DDevice->GetStreamSource(StreamNumber, (struct XTL::IDirect3DVertexBuffer9 **)&pVertexBuffer, 0, pStride);
#endif

    EmuSwapFS();
    return pVertexBuffer;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetStreamSource
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetStreamSource
(
    UINT                StreamNumber,
    X_D3DVertexBuffer  *pStreamData,
    UINT                Stride
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetStreamSource\n"
           "(\n"
           "   StreamNumber        : 0x%.08X\n"
           "   pStreamData         : 0x%.08X (0x%.08X)\n"
           "   Stride              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), StreamNumber, pStreamData, (pStreamData != 0) ? pStreamData->EmuVertexBuffer8 : 0, Stride);

	// Cache stream number
	g_dwLastSetStream = StreamNumber;

    if(StreamNumber == 0)
        g_pVertexBuffer = pStreamData;

	// Test for a non-zero stream source.  Unreal Championship gives us
	// some funky number when going ingame.
//	if(StreamNumber != 0)
//	{
//		EmuWarning( "StreamNumber: = %d", StreamNumber );
//		EmuWarning( "pStreamData: = 0x%.08X (0x%.08X)", pStreamData, (pStreamData != 0) ? pStreamData->EmuVertexBuffer8 : NULL );
////		__asm int 3;
//	}

#ifndef D3D9
    IDirect3DVertexBuffer8 *pVertexBuffer8 = NULL;
#else
    IDirect3DVertexBuffer9 *pVertexBuffer8 = NULL;
#endif

    if(pStreamData != NULL)
    {
        EmuVerifyResourceIsRegistered(pStreamData);

        pVertexBuffer8 = pStreamData->EmuVertexBuffer8;
        pVertexBuffer8->Unlock();
    }

    #ifdef _DEBUG_TRACK_VB
    if(pStreamData != NULL)
    {
        g_bVBSkipStream = g_VBTrackDisable.exists(pStreamData->EmuVertexBuffer8);
    }
    #endif

#ifndef D3D9
    HRESULT hRet = g_pD3DDevice->SetStreamSource(StreamNumber, pVertexBuffer8, Stride);
#else
    HRESULT hRet = g_pD3DDevice->SetStreamSource(StreamNumber, pVertexBuffer8, 0, Stride);
#endif

    if(FAILED(hRet))
        CxbxKrnlCleanup("SetStreamSource Failed!");

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexShader
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetVertexShader
(
    DWORD Handle
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexShader\n"
           "(\n"
           "   Handle              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Handle);

    HRESULT hRet = D3D_OK;

#ifndef D3D9
    g_CurrentVertexShader = Handle;

    // Store viewport offset and scale in constant registers 58 (c-38) and
    // 59 (c-37) used for screen space transformation.
    if(g_VertexShaderConstantMode != X_VSCM_NONERESERVED)
    {
        // TODO: Proper solution.
        static float vScale[] = { (2.0f / 640), (-2.0f / 480), 0.0f, 0.0f };
        static float vOffset[] = { -1.0f, 1.0f, 0.0f, 1.0f };

#ifndef D3D9
        g_pD3DDevice->SetVertexShaderConstant(58, vScale, 1);
        g_pD3DDevice->SetVertexShaderConstant(59, vOffset, 1);
#else
        g_pD3DDevice->SetVertexShaderConstantF(58, vScale, 1);
        g_pD3DDevice->SetVertexShaderConstantF(59, vOffset, 1);
#endif
    }

    DWORD RealHandle;
    if(VshHandleIsVertexShader(Handle))
    {
        RealHandle = ((VERTEX_SHADER *)(VshHandleGetVertexShader(Handle))->Handle)->Handle;
    }
    else
    {
        RealHandle = Handle;
		RealHandle &= ~D3DFVF_XYZ;
		RealHandle &= ~D3DFVF_XYZRHW;
		RealHandle &= ~D3DFVF_XYZB1;
		RealHandle &= ~D3DFVF_XYZB2;
		RealHandle &= ~D3DFVF_XYZB3;
		RealHandle &= ~D3DFVF_XYZB4;
		RealHandle &= ~D3DFVF_DIFFUSE;
		RealHandle &= ~D3DFVF_NORMAL;
		RealHandle &= ~D3DFVF_SPECULAR;
		RealHandle &= 0x00FF;
		if( RealHandle != 0 )
			printf( "EmuWarning: Handle = 0x%.08X\n", RealHandle );
		RealHandle = Handle;
    }
    hRet = g_pD3DDevice->SetVertexShader(RealHandle);
#endif

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_DrawVertices
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_DrawVertices
(
    X_D3DPRIMITIVETYPE PrimitiveType,
    UINT               StartVertex,
    UINT               VertexCount
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_DrawVertices\n"
           "(\n"
           "   PrimitiveType       : 0x%.08X\n"
           "   StartVertex         : 0x%.08X\n"
           "   VertexCount         : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), PrimitiveType, StartVertex, VertexCount);

    EmuUpdateDeferredStates();
	EmuUnswizzleTextureStages();

    VertexPatchDesc VPDesc;

    VPDesc.PrimitiveType = PrimitiveType;
    VPDesc.dwVertexCount = VertexCount;
    VPDesc.dwOffset = StartVertex;
    VPDesc.pVertexStreamZeroData = 0;
    VPDesc.uiVertexStreamZeroStride = 0;
    VPDesc.hVertexShader = g_CurrentVertexShader;

    VertexPatcher VertPatch;

    bool bPatched = VertPatch.Apply(&VPDesc, NULL);

    if(IsValidCurrentShader())
    {
        #ifdef _DEBUG_TRACK_VB
        if(!g_bVBSkipStream)
        {
        #endif

        g_pD3DDevice->DrawPrimitive
        (
            EmuPrimitiveType(VPDesc.PrimitiveType),
            StartVertex,
            VPDesc.dwPrimitiveCount
        );

		g_dwPrimPerFrame += VPDesc.dwPrimitiveCount;

        #ifdef _DEBUG_TRACK_VB
        }
        #endif
    }

    VertPatch.Restore();

	// Execute callback procedure
	if( g_CallbackType == X_D3DCALLBACK_WRITE )
	{
		if( g_pCallback )
		{
			EmuSwapFS();
			g_pCallback(g_CallbackParam);
			EmuSwapFS();

			// TODO: Reset pointer?
		}
	}

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_DrawVerticesUP
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_DrawVerticesUP
(
    X_D3DPRIMITIVETYPE  PrimitiveType,
    UINT                VertexCount,
    CONST PVOID         pVertexStreamZeroData,
    UINT                VertexStreamZeroStride
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_DrawVerticesUP\n"
           "(\n"
           "   PrimitiveType            : 0x%.08X\n"
           "   VertexCount              : 0x%.08X\n"
           "   pVertexStreamZeroData    : 0x%.08X\n"
           "   VertexStreamZeroStride   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), PrimitiveType, VertexCount, pVertexStreamZeroData,
           VertexStreamZeroStride);

    EmuUpdateDeferredStates();
	EmuUnswizzleTextureStages();

/*#if 0
	// HACK: Phantom Crash...
//	if( (*(DWORD*)&pVertexStreamZeroData) == 0x81FC3080 || (*(DWORD*)&pVertexStreamZeroData) == 0x8191E080 )
	if( ((*(DWORD*)&pVertexStreamZeroData) & 0xFF000FFF) == 0x81000080 )
	{
//		EmuWarning( "Invalid vertex data! (0x%.08X)", pVertexStreamZeroData );
		EmuWarning( "Phantom Crash hack applied!" );
		return;
	}
#else
	return;
#endif*/

    VertexPatchDesc VPDesc;

    VPDesc.PrimitiveType = PrimitiveType;
    VPDesc.dwVertexCount = VertexCount;
    VPDesc.dwOffset = 0;
    VPDesc.pVertexStreamZeroData = pVertexStreamZeroData;
    VPDesc.uiVertexStreamZeroStride = VertexStreamZeroStride;
    VPDesc.hVertexShader = g_CurrentVertexShader;

    VertexPatcher VertPatch;

    bool bPatched = VertPatch.Apply(&VPDesc, NULL);

    if (IsValidCurrentShader())
    {
        #ifdef _DEBUG_TRACK_VB
        if(!g_bVBSkipStream)
        {
        #endif

        g_pD3DDevice->DrawPrimitiveUP
        (
            EmuPrimitiveType(VPDesc.PrimitiveType),
            VPDesc.dwPrimitiveCount,
            VPDesc.pVertexStreamZeroData,
            VPDesc.uiVertexStreamZeroStride
        );

		g_dwPrimPerFrame += VPDesc.dwPrimitiveCount;

        #ifdef _DEBUG_TRACK_VB
        }
        #endif
    }

    VertPatch.Restore();

	// Execute callback procedure
	if( g_CallbackType == X_D3DCALLBACK_WRITE )
	{
		if( g_pCallback )
		{
			EmuSwapFS();
			g_pCallback(g_CallbackParam);
			EmuSwapFS();

			// TODO: Reset pointer?
		}
	}

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_DrawIndexedVertices
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_DrawIndexedVertices
(
    X_D3DPRIMITIVETYPE  PrimitiveType,
    UINT                VertexCount,
    CONST PWORD         pIndexData
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_DrawIndexedVertices\n"
           "(\n"
           "   PrimitiveType       : 0x%.08X\n"
           "   VertexCount         : 0x%.08X\n"
           "   pIndexData          : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), PrimitiveType, VertexCount, pIndexData);
//#if 0
    // update index buffer, if necessary
    if(g_pIndexBuffer != 0 && g_pIndexBuffer->Lock == X_D3DRESOURCE_LOCK_FLAG_NOSIZE)
    {
        DWORD dwSize = VertexCount*2;   // 16-bit indices

        HRESULT hRet = g_pD3DDevice->CreateIndexBuffer
        (
            dwSize, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED,
#ifndef D3D9
            &g_pIndexBuffer->EmuIndexBuffer8
#else
            &g_pIndexBuffer->EmuIndexBuffer8, NULL
#endif
        );

        if(FAILED(hRet))
            CxbxKrnlCleanup("CreateIndexBuffer Failed!");

        BYTE *pData = 0;

#ifndef D3D9
        hRet = g_pIndexBuffer->EmuIndexBuffer8->Lock(0, dwSize, &pData, 0);
#else
        hRet = g_pIndexBuffer->EmuIndexBuffer8->Lock(0, dwSize, (void**)&pData, 0);
#endif

        if(FAILED(hRet))
            CxbxKrnlCleanup("IndexBuffer Lock Failed!");

        memcpy(pData, (void*)g_pIndexBuffer->Data, dwSize);

        g_pIndexBuffer->EmuIndexBuffer8->Unlock();

        g_pIndexBuffer->Data = (ULONG)pData;

#ifndef D3D9
        hRet = g_pD3DDevice->SetIndices(g_pIndexBuffer->EmuIndexBuffer8, g_dwBaseVertexIndex);
#else
        hRet = g_pD3DDevice->SetIndices(g_pIndexBuffer->EmuIndexBuffer8);
#endif

        if(FAILED(hRet))
            CxbxKrnlCleanup("SetIndices Failed!");
    }

    EmuUpdateDeferredStates();
	EmuUnswizzleTextureStages();

    if( (PrimitiveType == X_D3DPT_LINELOOP) || (PrimitiveType == X_D3DPT_QUADLIST) )
        EmuWarning("Unsupported PrimitiveType! (%d)", (DWORD)PrimitiveType);

	// HACK: Azurik :(
//	if( ((DWORD)PrimitiveType) > 11 )
	/*if( PrimitiveType != X_D3DPT_POINTLIST && PrimitiveType != X_D3DPT_LINELIST && 
		PrimitiveType != X_D3DPT_LINELOOP && PrimitiveType != X_D3DPT_LINESTRIP &&
		PrimitiveType != X_D3DPT_TRIANGLELIST && PrimitiveType != X_D3DPT_TRIANGLESTRIP && 
		PrimitiveType != X_D3DPT_TRIANGLEFAN && PrimitiveType != X_D3DPT_QUADLIST &&
		PrimitiveType != X_D3DPT_QUADSTRIP && PrimitiveType != X_D3DPT_POLYGON )
	{
		CxbxKrnlCleanup("Invalid Primitive type! %d (0x%X)", (int)PrimitiveType, (DWORD)PrimitiveType);
	}*/

    VertexPatchDesc VPDesc;

    VPDesc.PrimitiveType = PrimitiveType;
    VPDesc.dwVertexCount = VertexCount;
    VPDesc.dwOffset = 0;
    VPDesc.pVertexStreamZeroData = 0;
    VPDesc.uiVertexStreamZeroStride = 0;
    VPDesc.hVertexShader = g_CurrentVertexShader;

    VertexPatcher VertPatch;
	bool FatalError = false;

    bool bPatched = VertPatch.Apply(&VPDesc, &FatalError);

    #ifdef _DEBUG_TRACK_VB
    if(!g_bVBSkipStream)
    {
    #endif

    bool bActiveIB = false;

#ifndef D3D9
    IDirect3DIndexBuffer8 *pIndexBuffer = 0;
#else
    IDirect3DIndexBuffer9 *pIndexBuffer = 0;
#endif

    // check if there is an active index buffer
    {
        UINT BaseIndex = 0;

#ifndef D3D9
        g_pD3DDevice->GetIndices(&pIndexBuffer, &BaseIndex);
#else
        g_pD3DDevice->GetIndices(&pIndexBuffer/*, &BaseIndex*/);
#endif

        if(pIndexBuffer != 0)
        {
            bActiveIB = true;
            pIndexBuffer->Release();
        }
    }

    UINT uiNumVertices = 0;
    UINT uiStartIndex = 0;

    // TODO: caching (if it becomes noticably slow to recreate the buffer each time)
    if(!bActiveIB)
    {
#ifndef D3D9
        if(FAILED(g_pD3DDevice->CreateIndexBuffer(VertexCount*2, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &pIndexBuffer)))
#else
        if(FAILED(g_pD3DDevice->CreateIndexBuffer(VertexCount*2, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &pIndexBuffer, NULL)))
#endif
			CxbxKrnlCleanup("Cound not create index buffer! (%d bytes)", VertexCount*2);

        if(pIndexBuffer == 0)
            CxbxKrnlCleanup("Could not create index buffer! (%d bytes)", VertexCount*2);

        BYTE *pbData = 0;

#ifndef D3D9
        pIndexBuffer->Lock(0, 0, &pbData, 0);
#else
        pIndexBuffer->Lock(0, 0, (void**)&pbData, 0);
#endif

        if(pbData == 0)
            CxbxKrnlCleanup("Could not lock index buffer!");

		if(pIndexData)
			memcpy(pbData, pIndexData, VertexCount*2);

        pIndexBuffer->Unlock();

#ifndef D3D9
        g_pD3DDevice->SetIndices(pIndexBuffer, g_dwBaseVertexIndex);
#else
        g_pD3DDevice->SetIndices(pIndexBuffer);
#endif

        uiNumVertices = VertexCount;
        uiStartIndex = 0;
    }
    else
    {
        uiNumVertices = ((DWORD)pIndexData)/2 + VertexCount;
        uiStartIndex = ((DWORD)pIndexData)/2;
    }

    if(IsValidCurrentShader() && !FatalError)
    {
        g_pD3DDevice->DrawIndexedPrimitive
        (
#ifndef D3D9
            EmuPrimitiveType(VPDesc.PrimitiveType), 0, uiNumVertices, uiStartIndex, VPDesc.dwPrimitiveCount
#else
            EmuPrimitiveType(VPDesc.PrimitiveType), g_dwBaseVertexIndex, 0, uiNumVertices, uiStartIndex, VPDesc.dwPrimitiveCount
#endif
        );

		g_dwPrimPerFrame += VPDesc.dwPrimitiveCount;

		/*if( (PrimitiveType == X_D3DPT_LINELOOP) || (PrimitiveType == X_D3DPT_QUADLIST) ) 
		{ 
			g_pD3DDevice->DrawPrimitive 
			( 
				EmuPrimitiveType(VPDesc.PrimitiveType), 0, VPDesc.dwPrimitiveCount 
			); 
		} 
		else 
		{ 
			g_pD3DDevice->DrawIndexedPrimitive 
			( 
				EmuPrimitiveType(VPDesc.PrimitiveType), 0, uiNumVertices, uiStartIndex, VPDesc.dwPrimitiveCount 
			); 
		} */
    }

    if(!bActiveIB)
    {
#ifndef D3D9
        g_pD3DDevice->SetIndices(0, 0);
#else
        g_pD3DDevice->SetIndices(0);
#endif
        pIndexBuffer->Release();
    }

    #ifdef _DEBUG_TRACK_VB
    }
    #endif

    VertPatch.Restore();

	// Execute callback procedure
	if( g_CallbackType == X_D3DCALLBACK_WRITE )
	{
		if( g_pCallback )
		{
			EmuSwapFS();
			g_pCallback(g_CallbackParam);
			EmuSwapFS();

			// TODO: Reset pointer?
		}
	}

//#endif
    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_DrawIndexedVerticesUP
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_DrawIndexedVerticesUP
(
    X_D3DPRIMITIVETYPE  PrimitiveType,
    UINT                VertexCount,
    CONST PVOID         pIndexData,
    CONST PVOID         pVertexStreamZeroData,
    UINT                VertexStreamZeroStride
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_DrawIndexedVerticesUP\n"
           "(\n"
           "   PrimitiveType            : 0x%.08X\n"
           "   VertexCount              : 0x%.08X\n"
           "   pIndexData               : 0x%.08X\n"
           "   pVertexStreamZeroData    : 0x%.08X\n"
           "   VertexStreamZeroStride   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), PrimitiveType, VertexCount, pIndexData, pVertexStreamZeroData, VertexStreamZeroStride);

    // update index buffer, if necessary
    if(g_pIndexBuffer != 0 && g_pIndexBuffer->Lock == X_D3DRESOURCE_LOCK_FLAG_NOSIZE)
        CxbxKrnlCleanup("g_pIndexBuffer != 0");

    EmuUpdateDeferredStates();
	EmuUnswizzleTextureStages();

    if( (PrimitiveType == X_D3DPT_LINELOOP) || (PrimitiveType == X_D3DPT_QUADLIST) )
        EmuWarning("Unsupported PrimitiveType! (%d)", (DWORD)PrimitiveType);

    VertexPatchDesc VPDesc;

    VPDesc.PrimitiveType = PrimitiveType;
    VPDesc.dwVertexCount = VertexCount;
    VPDesc.dwOffset = 0;
    VPDesc.pVertexStreamZeroData = pVertexStreamZeroData;
    VPDesc.uiVertexStreamZeroStride = VertexStreamZeroStride;
    VPDesc.hVertexShader = g_CurrentVertexShader;

    VertexPatcher VertPatch;

    bool bPatched = VertPatch.Apply(&VPDesc, NULL);

    #ifdef _DEBUG_TRACK_VB
    if(!g_bVBSkipStream)
    {
    #endif

    if (IsValidCurrentShader())
    {
        g_pD3DDevice->DrawIndexedPrimitiveUP
        (
            EmuPrimitiveType(VPDesc.PrimitiveType), 0, VPDesc.dwVertexCount, VPDesc.dwPrimitiveCount, pIndexData, D3DFMT_INDEX16, VPDesc.pVertexStreamZeroData, VPDesc.uiVertexStreamZeroStride
        );

		g_dwPrimPerFrame += VPDesc.dwPrimitiveCount;
    }

    #ifdef _DEBUG_TRACK_VB
    }
    #endif

    VertPatch.Restore();

	// Execute callback procedure
	if( g_CallbackType == X_D3DCALLBACK_WRITE )
	{
		if( g_pCallback )
		{
			EmuSwapFS();
			g_pCallback(g_CallbackParam);
			EmuSwapFS();

			// TODO: Reset pointer?
		}
	}

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetLight
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetLight
(
    DWORD            Index,
#ifndef D3D9
    CONST D3DLIGHT8 *pLight
#else
    CONST D3DLIGHT9 *pLight
#endif
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetLight\n"
           "(\n"
           "   Index               : 0x%.08X\n"
           "   pLight              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Index, pLight);

    HRESULT hRet = g_pD3DDevice->SetLight(Index, pLight);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetMaterial
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetMaterial
(
#ifndef D3D9
    CONST D3DMATERIAL8 *pMaterial
#else
    CONST D3DMATERIAL9 *pMaterial
#endif
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetMaterial\n"
           "(\n"
           "   pMaterial           : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pMaterial);

    HRESULT hRet = g_pD3DDevice->SetMaterial(pMaterial);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_LightEnable
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_LightEnable
(
    DWORD            Index,
    BOOL             bEnable
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_LightEnable\n"
           "(\n"
           "   Index               : 0x%.08X\n"
           "   bEnable             : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Index, bEnable);

    HRESULT hRet = g_pD3DDevice->LightEnable(Index, bEnable);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderTarget
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetRenderTarget
(
    X_D3DSurface    *pRenderTarget,
    X_D3DSurface    *pNewZStencil
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderTarget\n"
           "(\n"
           "   pRenderTarget       : 0x%.08X (0x%.08X)\n"
           "   pNewZStencil        : 0x%.08X (0x%.08X)\n"
           ");\n",
           GetCurrentThreadId(), pRenderTarget, (pRenderTarget != 0) ? pRenderTarget->EmuSurface8 : 0, pNewZStencil,
           (pNewZStencil != 0) ? pNewZStencil->EmuSurface8 : 0);

#ifndef D3D9
    IDirect3DSurface8 *pPCRenderTarget = 0;
    IDirect3DSurface8 *pPCNewZStencil  = 0;
#else
    IDirect3DSurface9 *pPCRenderTarget = 0;
    IDirect3DSurface9 *pPCNewZStencil  = 0;
#endif

    if(pRenderTarget != 0)
    {
		if(pRenderTarget->EmuSurface8)
		{
			EmuVerifyResourceIsRegistered(pRenderTarget);
			pPCRenderTarget = pRenderTarget->EmuSurface8;
		}
		else
		{
			pPCRenderTarget = g_pCachedRenderTarget->EmuSurface8;
		}
    }

    if(pNewZStencil != 0)
    {
        if(pNewZStencil->EmuSurface8 != 0)
        {
            EmuVerifyResourceIsRegistered(pNewZStencil);
            pPCNewZStencil = pNewZStencil->EmuSurface8;
        }
        else
        {
            pPCNewZStencil = g_pCachedZStencilSurface->EmuSurface8;
        }
    }

    // TODO: Follow that stencil!
#ifndef D3D9
    HRESULT hRet = g_pD3DDevice->SetRenderTarget(pPCRenderTarget, pPCNewZStencil);
#else
    HRESULT hRet = g_pD3DDevice->SetRenderTarget(0, pPCRenderTarget);
#endif

    if(FAILED(hRet))
        EmuWarning("SetRenderTarget Failed! (0x%.08X)", hRet);

#ifdef D3D9
    hRet = g_pD3DDevice->SetDepthStencilSurface(pPCNewZStencil);

    if(FAILED(hRet))
        EmuWarning("SetDepthStencilSurface Failed! (0x%.08X)", hRet);
#endif

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreatePalette
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreatePalette
(
    X_D3DPALETTESIZE    Size,
    X_D3DPalette      **ppPalette
)
{
    *ppPalette = EmuIDirect3DDevice8_CreatePalette2(Size);

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreatePalette2
// ******************************************************************
XTL::X_D3DPalette * WINAPI XTL::EmuIDirect3DDevice8_CreatePalette2
(
    X_D3DPALETTESIZE    Size
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreatePalette2\n"
           "(\n"
           "   Size                : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Size);

    X_D3DPalette *pPalette = new X_D3DPalette();

    static int lk[4] =
    {
        256*sizeof(D3DCOLOR),    // D3DPALETTE_256
        128*sizeof(D3DCOLOR),    // D3DPALETTE_128
        64*sizeof(D3DCOLOR),     // D3DPALETTE_64
        32*sizeof(D3DCOLOR)      // D3DPALETTE_32
    };

    pPalette->Common = (Size << 30) | 0x1030001;
    pPalette->Data = (DWORD)new uint08[lk[Size]];

    pPalette->Lock = 0x8000BEEF; // emulated reference count for palettes

	// TODO: Should't we register the palette with a call to
	// EmuIDirect3DResource8_Register? So far, it doesn't look
	// like the palette registration code gets used.  If not, then we
	// need to cache the palette manually during any calls to
	// EmuIDirect3DDevice8_SetPalette for 8-bit textures to work properly.

    DbgPrintf("pPalette: = 0x%.08X\n", pPalette);

    EmuSwapFS();   // XBox FS

    return pPalette;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetPalette
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetPalette
(
    DWORD         Stage,
    X_D3DPalette *pPalette
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetPalette\n"
           "(\n"
           "   Stage               : 0x%.08X\n"
           "   pPalette            : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Stage, pPalette);

//    g_pD3DDevice->SetPaletteEntries(0, (PALETTEENTRY*)pPalette->Data);

	// Cache palette data and size
	if( pPalette )
	{
		if( pPalette->Data )
		{
			pCurrentPalette = (LPVOID) pPalette->Data;
			dwCurrentPaletteSize = EmuCheckAllocationSize( (LPVOID) pPalette->Data, false );
		}
	}

    EmuWarning("Not setting palette");

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetFlickerFilter
// ******************************************************************
void WINAPI XTL::EmuIDirect3DDevice8_SetFlickerFilter
(
    DWORD         Filter
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetFlickerFilter\n"
           "(\n"
           "   Filter              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Filter);

    EmuWarning("Not setting flicker filter");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetSoftDisplayFilter
// ******************************************************************
void WINAPI XTL::EmuIDirect3DDevice8_SetSoftDisplayFilter
(
    BOOL Enable
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetSoftDisplayFilter\n"
           "(\n"
           "   Enable              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Enable);

    EmuWarning("Not setting soft display filter");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DPalette8_Lock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DPalette8_Lock
(
    X_D3DPalette   *pThis,
    D3DCOLOR      **ppColors,
    DWORD           Flags
)
{
	HRESULT hRet = D3D_OK;

	if( pThis )
		*ppColors = EmuIDirect3DPalette8_Lock2(pThis, Flags);
	else
	{
		EmuWarning( "EmuIDirect3DPalette8_Lock: pThis == NULL!" );
		hRet = E_FAIL;
	}

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DPalette8_Lock2
// ******************************************************************
XTL::D3DCOLOR * WINAPI XTL::EmuIDirect3DPalette8_Lock2
(
    X_D3DPalette   *pThis,
    DWORD           Flags
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DPalette8_Lock\n"
           "(\n"
           "   pThis               : 0x%.08X (->Data : 0x%.08X)\n"
           "   Flags               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, pThis->Data, Flags);

    // If X_D3DLOCK_READONLY and X_D3DLOCK_NOOVERWRITE bitflags not set
    if( !(Flags & (X_D3DLOCK_READONLY | X_D3DLOCK_NOOVERWRITE)) )
    {
        EmuIDirect3DResource8_BlockUntilNotBusy(pThis);
    }

    D3DCOLOR *pColors = (D3DCOLOR*)pThis->Data;

    EmuSwapFS();   // XBox FS

    return pColors;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetVertexShaderSize
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_GetVertexShaderSize
(
    DWORD Handle,
    UINT* pSize
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf( "EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetVertexShaderSize\n"
               "(\n"
               "   Handle               : 0x%.08X\n"
               "   pSize                : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Handle, pSize);

    if(pSize  && VshHandleIsVertexShader(Handle))
    {
        X_D3DVertexShader *pD3DVertexShader = (X_D3DVertexShader *)(Handle & 0x7FFFFFFF);
        VERTEX_SHADER *pVertexShader = (VERTEX_SHADER *)pD3DVertexShader->Handle;
        *pSize = pVertexShader->Size;
    }
    else if(pSize)
    {
        *pSize = 0;
    }

    EmuSwapFS();   // Xbox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_DeleteVertexShader
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_DeleteVertexShader
(
    DWORD Handle
)
{
    EmuSwapFS();

    DbgPrintf( "EmuD3D8 (0x%.08X): EmuIDirect3DDevice8_DeleteVertexShader\n"
               "(\n"
               "   Handle                : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Handle);

#ifndef D3D9
    DWORD RealHandle = 0;

    if(VshHandleIsVertexShader(Handle))
    {
        X_D3DVertexShader *pD3DVertexShader = (X_D3DVertexShader *)(Handle & 0x7FFFFFFF);
        VERTEX_SHADER *pVertexShader = (VERTEX_SHADER *)pD3DVertexShader->Handle;

        RealHandle = pVertexShader->Handle;
        CxbxFree(pVertexShader->pDeclaration);

        if(pVertexShader->pFunction)
        {
            CxbxFree(pVertexShader->pFunction);
        }

        FreeVertexDynamicPatch(pVertexShader);

        CxbxFree(pVertexShader);
        CxbxFree(pD3DVertexShader);
    }

    HRESULT hRet = g_pD3DDevice->DeleteVertexShader(RealHandle);
#endif

    EmuSwapFS();
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SelectVertexShaderDirect
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SelectVertexShaderDirect
(
    X_VERTEXATTRIBUTEFORMAT *pVAF,
    DWORD                    Address
)
{
    EmuSwapFS();   // Win2k/XP FS

    // debug trace
    DbgPrintf( "EmuD3D8 (0x%X): EmuIDirect3DDevice8_SelectVertexShaderDirect\n"
               "(\n"
               "   pVAF                : 0x%.08X\n"
               "   Address             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pVAF,Address);

    DbgPrintf("NOT YET IMPLEMENTED!\n");

    EmuSwapFS();   // Xbox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetShaderConstantMode
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_GetShaderConstantMode
(
    DWORD *pMode
)
{
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        DbgPrintf( "EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetShaderConstantMode\n"
                   "(\n"
                   "   pMode               : 0x%.08X\n"
                   ");\n",
                   GetCurrentThreadId(), pMode);
        EmuSwapFS();   // Xbox FS
    }
    #endif

    if(pMode)
    {
        *pMode = g_VertexShaderConstantMode;
    }
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetVertexShader
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_GetVertexShader
(
    DWORD *pHandle
)
{
    EmuSwapFS();

    // debug trace
    DbgPrintf( "EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetVertexShader\n"
               "(\n"
               "   pHandle             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pHandle);

    if(pHandle)
    {
        (*pHandle) = g_CurrentVertexShader;
    }

    EmuSwapFS();
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetVertexShaderConstant
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_GetVertexShaderConstant
(
    INT   Register,
    void  *pConstantData,
    DWORD ConstantCount
)
{
    EmuSwapFS();   // Win2k/XP FS

    // debug trace
    DbgPrintf( "EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetVertexShaderConstant\n"
               "(\n"
               "   Register            : 0x%.08X\n"
               "   pConstantData       : 0x%.08X\n"
               "   ConstantCount       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Register, pConstantData, ConstantCount);

#ifndef D3D9
    HRESULT hRet = g_pD3DDevice->GetVertexShaderConstant
#else
    HRESULT hRet = g_pD3DDevice->GetVertexShaderConstantF
#endif
    (
        Register + 96,
#ifndef D3D9
        pConstantData,
#else
        (float*) pConstantData,
#endif
        ConstantCount
    );

    EmuSwapFS();   // XBox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexShaderInputDirect
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetVertexShaderInputDirect
(
    X_VERTEXATTRIBUTEFORMAT *pVAF,
    UINT                     StreamCount,
    X_STREAMINPUT           *pStreamInputs
)
{
    EmuSwapFS();   // Win2k/XP FS

    // debug trace
    DbgPrintf( "EmuD3D8 (0x%X): EmuIDirect3DDevice8_SelectVertexShaderDirect\n"
               "(\n"
               "   pVAF                : 0x%.08X\n"
               "   StreamCount         : 0x%.08X\n"
               "   pStreamInputs       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pVAF, StreamCount, pStreamInputs);

    DbgPrintf("NOT YET IMPLEMENTED!\n");

    EmuSwapFS();   // Xbox FS

    return 0;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetVertexShaderInput
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetVertexShaderInput
(
    DWORD              *pHandle,
    UINT               *pStreamCount,
    X_STREAMINPUT      *pStreamInputs
)
{
    EmuSwapFS();   // Win2k/XP FS

    // debug trace
    DbgPrintf( "EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetVertexShaderInput\n"
               "(\n"
               "   pHandle             : 0x%.08X\n"
               "   pStreamCount        : 0x%.08X\n"
               "   pStreamInputs       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pHandle, pStreamCount, pStreamInputs);

    DbgPrintf("NOT YET IMPLEMENTED!\n");

    EmuSwapFS();   // Xbox FS

    return 0;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexShaderInput
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetVertexShaderInput
(
    DWORD              Handle,
    UINT               StreamCount,
    X_STREAMINPUT     *pStreamInputs
)
{
    EmuSwapFS();   // Win2k/XP FS

    // debug trace
    DbgPrintf( "EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexShaderInput\n"
               "(\n"
               "   Handle              : 0x%.08X\n"
               "   StreamCount         : 0x%.08X\n"
               "   pStreamInputs       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Handle, StreamCount, pStreamInputs);

    DbgPrintf("NOT YET IMPLEMENTED!\n");

    EmuSwapFS();   // Xbox FS

    return 0;
}


// ******************************************************************
// * func: EmuIDirect3DDevice8_RunVertexStateShader
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_RunVertexStateShader
(
    DWORD Address,
    CONST FLOAT *pData
)
{
    EmuSwapFS();   // Win2k/XP FS

    // debug trace
    DbgPrintf( "EmuD3D8 (0x%X): EmuIDirect3DDevice8_RunVertexStateShader\n"
               "(\n"
               "   Address              : 0x%.08X\n"
               "   pData                : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Address,pData);

    DbgPrintf("NOT YET IMPLEMENTED!\n");

    EmuSwapFS();   // Xbox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_LoadVertexShaderProgram
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_LoadVertexShaderProgram
(
    CONST DWORD *pFunction,
    DWORD        Address
)
{
    EmuSwapFS();   // Win2k/XP FS

    // debug trace
    DbgPrintf( "EmuD3D8 (0x%X): EmuIDirect3DDevice8_LoadVertexShaderProgram\n"
               "(\n"
               "   pFunction           : 0x%.08X\n"
               "   Address             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pFunction,Address);

    DbgPrintf("NOT YET IMPLEMENTED!\n");

    EmuSwapFS();   // Xbox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetVertexShaderType
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_GetVertexShaderType
(
    DWORD  Handle,
    DWORD *pType
)
{
    EmuSwapFS();   // Win2k/XP FS

    // debug trace
    DbgPrintf( "EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetVertexShaderType\n"
               "(\n"
               "   Handle               : 0x%.08X\n"
               "   pType                : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Handle, pType);

    if(pType && VshHandleIsVertexShader(Handle))
    {
        *pType = ((VERTEX_SHADER *)(VshHandleGetVertexShader(Handle))->Handle)->Type;
    }

    EmuSwapFS();   // Xbox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetVertexShaderDeclaration
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetVertexShaderDeclaration
(
    DWORD  Handle,
    PVOID  pData,
    DWORD *pSizeOfData
)
{
    EmuSwapFS();   // Win2k/XP FS

    // debug trace
    DbgPrintf( "EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetVertexShaderDeclaration\n"
               "(\n"
               "   Handle               : 0x%.08X\n"
               "   pData                : 0x%.08X\n"
               "   pSizeOfData          : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Handle, pData, pSizeOfData);

    HRESULT hRet = D3DERR_INVALIDCALL;

    if(pSizeOfData && VshHandleIsVertexShader(Handle))
    {
        VERTEX_SHADER *pVertexShader = (VERTEX_SHADER *)(VshHandleGetVertexShader(Handle))->Handle;
        if(*pSizeOfData < pVertexShader->DeclarationSize || !pData)
        {
            *pSizeOfData = pVertexShader->DeclarationSize;

            hRet = !pData ? D3D_OK : D3DERR_MOREDATA;
        }
        else
        {
            memcpy(pData, pVertexShader->pDeclaration, pVertexShader->DeclarationSize);
            hRet = D3D_OK;
        }
    }

    EmuSwapFS();   // Xbox FS
    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetVertexShaderFunction
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetVertexShaderFunction
(
    DWORD  Handle,
    PVOID *pData,
    DWORD *pSizeOfData
)
{
    EmuSwapFS();   // Win2k/XP FS

    // debug trace
    DbgPrintf( "EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetVertexShaderFunction\n"
               "(\n"
               "   Handle               : 0x%.08X\n"
               "   pData                : 0x%.08X\n"
               "   pSizeOfData          : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Handle,pData,pSizeOfData);

    HRESULT hRet = D3DERR_INVALIDCALL;

    if(pSizeOfData && VshHandleIsVertexShader(Handle))
    {
        VERTEX_SHADER *pVertexShader = (VERTEX_SHADER *)(VshHandleGetVertexShader(Handle))->Handle;
        if(*pSizeOfData < pVertexShader->FunctionSize || !pData)
        {
            *pSizeOfData = pVertexShader->FunctionSize;

            hRet = !pData ? D3D_OK : D3DERR_MOREDATA;
        }
        else
        {
            memcpy(pData, pVertexShader->pFunction, pVertexShader->FunctionSize);
            hRet = D3D_OK;
        }
    }

    EmuSwapFS();   // Xbox FS
    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetDepthClipPlanes
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetDepthClipPlanes
(
    FLOAT Near,
    FLOAT Far,
    DWORD Flags
)
{
    EmuSwapFS();   // Win2k/XP FS

    // debug trace
    DbgPrintf( "EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetDepthClipPlanes\n"
               "(\n"
               "   Near                : 0x%.08X\n"
               "   Far                 : 0x%.08X\n"
               "   Flags               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Near, Far, Flags);

    HRESULT hRet = D3D_OK;

    switch(Flags) // Member of X_D3DSET_DEPTH_CLIP_PLANES_FLAGS enum
    {
        case X_D3DSDCP_SET_VERTEXPROGRAM_PLANES:
        {
            // Sets the depth-clipping planes used whenever vertex shader programs are active
            // TODO

            // pDevice->fNear = Near
            // pDevice->fFar  = Far
        }
        break;

        case X_D3DSDCP_SET_FIXEDFUNCTION_PLANES:
        {
            // Sets the depth-clipping planes used whenever the fixed-function pipeline is in use. 
            // TODO

            // pDevice->fNear = Near
            // pDevice->fFar  = Far
        }
        break;

        case X_D3DSDCP_USE_DEFAULT_VERTEXPROGRAM_PLANES:
        {
            // Causes Direct3D to disregard the depth-clipping planes set when using X_D3DSDCP_SET_VERTEXPROGRAM_PLANE. 
            // Direct3D will resume using its own internally calculated clip planes when vertex shader programs are active. 
            // TODO
        }
        break;

        case X_D3DSDCP_USE_DEFAULT_FIXEDFUNCTION_PLANES:
        {
            // Causes Direct3D to disregard the depth-clipping planes set when using X_D3DSDCP_SET_FIXEDFUNCTION_PLANES. 
            // Direct3D will resume using its own internally calculated clip planes when the fixed-function pipeline is active.
            // TODO
        }
        break;

        default:
            EmuWarning("Unknown SetDepthClipPlanes Flags provided");;
    }

    // TODO

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3D8_AllocContiguousMemory
// ******************************************************************
PVOID WINAPI XTL::EmuIDirect3D8_AllocContiguousMemory
(
    SIZE_T dwSize,
    DWORD dwAllocAttributes
)
{
    EmuSwapFS();   // Win2k/XP FS

    // debug trace
    DbgPrintf( "EmuD3D8 (0x%X): EmuIDirect3D8_AllocContiguousMemory\n"
               "(\n"
               "   dwSize              : 0x%.08X\n"
               "   dwAllocAttributes   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), dwSize,dwAllocAttributes);

    //
    // NOTE: Kludgey (but necessary) solution:
    //
    // Since this memory must be aligned on a page boundary, we must allocate an extra page
    // so that we can return a valid page aligned pointer
    //

    PVOID pRet = CxbxMalloc(dwSize + 0x1000);

    // align to page boundary
    {
        DWORD dwRet = (DWORD)pRet;

        dwRet += 0x1000 - dwRet%0x1000;

        g_AlignCache.insert(dwRet, pRet);

        pRet = (PVOID)dwRet;
    }

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3D8_AllocContiguousMemory returned 0x%.08X\n", GetCurrentThreadId(), pRet);

    EmuSwapFS();   // Xbox FS

    return pRet;
}

// ******************************************************************
// * func: EmuIDirect3DTexture8_GetLevelDesc
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DTexture8_GetLevelDesc
(
    UINT Level,
    X_D3DSURFACE_DESC* pDesc
)
{
    EmuSwapFS();   // Win2k/XP FS

    // debug trace
    DbgPrintf( "EmuD3D8 (0x%X): EmuIDirect3DTexture8_GetLevelDesc\n"
               "(\n"
               "   Level               : 0x%.08X\n"
               "   pDesc               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Level,pDesc);

    EmuSwapFS();   // Xbox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3D8_CheckDeviceMultiSampleType
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3D8_CheckDeviceMultiSampleType
(
    UINT                 Adapter,
    D3DDEVTYPE           DeviceType,
    D3DFORMAT            SurfaceFormat,
    BOOL                 Windowed,
    D3DMULTISAMPLE_TYPE  MultiSampleType
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf( "EmuD3D8 (0x%X): EmuIDirect3D8_CheckDeviceMultiSampleType\n"
               "(\n"
               "   Adapter             : 0x%.08X\n"
               "   DeviceType          : 0x%.08X\n"
               "   SurfaceFormat       : 0x%.08X\n"
               "   Windowed            : 0x%.08X\n"
               "   MultiSampleType     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Adapter, DeviceType, SurfaceFormat, Windowed, MultiSampleType);

    if(Adapter != D3DADAPTER_DEFAULT)
    {
        EmuWarning("Adapter is not D3DADAPTER_DEFAULT, correcting!");
        Adapter = D3DADAPTER_DEFAULT;
    }

    if(DeviceType == D3DDEVTYPE_FORCE_DWORD)
        EmuWarning("DeviceType == D3DDEVTYPE_FORCE_DWORD");

    // Convert SurfaceFormat (Xbox->PC)
    D3DFORMAT PCSurfaceFormat = EmuXB2PC_D3DFormat(SurfaceFormat);

    // TODO: HACK: Devices that don't support this should somehow emulate it!
    if(PCSurfaceFormat == D3DFMT_D16)
    {
        EmuWarning("D3DFMT_D16 is an unsupported texture format!");
        PCSurfaceFormat = D3DFMT_X8R8G8B8;
    }
    else if(PCSurfaceFormat == D3DFMT_P8)
    {
        EmuWarning("D3DFMT_P8 is an unsupported texture format!");
        PCSurfaceFormat = D3DFMT_X8R8G8B8;
    }
    else if(PCSurfaceFormat == D3DFMT_D24S8)
    {
        EmuWarning("D3DFMT_D24S8 is an unsupported texture format!");
        PCSurfaceFormat = D3DFMT_X8R8G8B8;
    }

    if(Windowed != FALSE)
        Windowed = FALSE;

    // TODO: Convert from Xbox to PC!!
    D3DMULTISAMPLE_TYPE PCMultiSampleType = EmuXB2PC_D3DMultiSampleFormat((DWORD) MultiSampleType);

    // Now call the real CheckDeviceMultiSampleType with the corrected parameters.
    HRESULT hRet = g_pD3D->CheckDeviceMultiSampleType
    (
        Adapter,
        DeviceType,
        PCSurfaceFormat,
        Windowed,
#ifndef D3D9
        PCMultiSampleType
#else
        PCMultiSampleType, 0
#endif
    );

    EmuSwapFS();   // Xbox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3D8_GetDeviceCaps
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3D8_GetDeviceCaps
(
    UINT        Adapter,
    D3DDEVTYPE  DeviceType,
#ifndef D3D9
    D3DCAPS8    *pCaps
#else
    D3DCAPS9    *pCaps
#endif
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3D8_GetDeviceCaps\n"
           "(\n"
           "   Adapter             : 0x%.08X\n"
           "   DeviceType          : 0x%.08X\n"
           "   pCaps               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Adapter, DeviceType, pCaps);

    HRESULT hRet = g_pD3D->GetDeviceCaps(Adapter, DeviceType, pCaps);
	if(FAILED(hRet))
		CxbxKrnlCleanup("IDirect3D8::GetDeviceCaps failed!");

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3D8_SetPushBufferSize
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3D8_SetPushBufferSize
(
    DWORD PushBufferSize,
    DWORD KickOffSize
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3D8_SetPushBufferSize\n"
           "(\n"
           "   PushBufferSize      : 0x%.08X\n"
           "   KickOffSize         : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), PushBufferSize, KickOffSize);

    HRESULT hRet = D3D_OK;

    // This is a Xbox extension, meaning there is no pc counterpart.

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_InsertFence
// ******************************************************************
DWORD WINAPI XTL::EmuIDirect3DDevice8_InsertFence()
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_InsertFence()\n", GetCurrentThreadId());

    // TODO: Actually implement this
    DWORD dwRet = 0x8000BEEF;

    EmuSwapFS();   // XBox FS

    return dwRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_IsFencePending
// ******************************************************************
BOOL WINAPI XTL::EmuIDirect3DDevice8_IsFencePending
(
    DWORD Fence
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_IsFencePending\n"
           "(\n"
           "   Fence               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Fence);

    // TODO: Implement

    EmuSwapFS();   // XBox FS

    return FALSE;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_BlockOnFence
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_BlockOnFence
(
    DWORD Fence
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_BlockOnFence\n"
           "(\n"
           "   Fence               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Fence);

    // TODO: Implement

    EmuSwapFS();   // XBox FS
}

// ******************************************************************
// * func: EmuIDirect3DResource8_BlockUntilNotBusy
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DResource8_BlockUntilNotBusy
(
    X_D3DResource *pThis
)
{
    // EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DResource8_BlockUntilNotBusy\n"
           "(\n"
           "   pThis               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis);

    // TODO: Implement

    // EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DVertexBuffer8_GetDesc
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DVertexBuffer8_GetDesc
(
    X_D3DVertexBuffer    *pThis,
    D3DVERTEXBUFFER_DESC *pDesc
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DVertexBuffer8_GetDesc\n"
           "(\n"
           "   pThis               : 0x%.08X\n"
           "   pDesc               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, pDesc);

    // TODO: Implement

    EmuSwapFS();   // XBox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetScissors
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetScissors
(
    DWORD          Count,
    BOOL           Exclusive,
    CONST D3DRECT  *pRects
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetScissors\n"
           "(\n"
           "   Count               : 0x%.08X\n"
           "   Exclusive           : 0x%.08X\n"
           "   pRects              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Count, Exclusive, pRects);

    // TODO: Implement

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetScreenSpaceOffset
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetScreenSpaceOffset
(
    FLOAT x,
    FLOAT y
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetScreenSpaceOffset\n"
           "(\n"
           "   x                   : %f\n"
           "   y                   : %f\n"
           ");\n",
           GetCurrentThreadId(), x, y);

    EmuWarning("EmuIDirect3DDevice8_SetScreenSpaceOffset ignored");

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetPixelShaderProgram
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetPixelShaderProgram
(
	X_D3DPIXELSHADERDEF* pPSDef
)
{
	EmuSwapFS();	// Win2kXP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetPixelShaderProgram\n"
		   "(\n"
		   "    pPSDef             : 0x%.08X\n"
		   ");\n",
		   GetCurrentThreadId(), pPSDef );

	HRESULT hRet = E_FAIL;
#ifndef D3D9
	DWORD dwHandle;

	// Redirect this call to windows Direct3D
	hRet = g_pD3DDevice->CreatePixelShader
    (
        (DWORD*) pPSDef,
        &dwHandle
    );

    if(FAILED(hRet))
    {
        dwHandle = X_PIXELSHADER_FAKE_HANDLE;

        EmuWarning("We're lying about the creation of a pixel shader!");
    }

	// Now, redirect this to Xbox Direct3D 
	//EmuSwapFS();
	//EmuIDirect3DDevice8_CreatePixelShader(pPSDef, &dwHandle);
	//hRet = XTL::EmuIDirect3DDevice8_SetPixelShader( dwHandle );
	//EmuSwapFS();
#endif

	EmuSwapFS();	// Xbox FS

	return S_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice_CreateStateBlock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateStateBlock
(
	D3DSTATEBLOCKTYPE Type,
	DWORD			  *pToken
)
{
	EmuSwapFS();	// Win2K/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreateStateBlock\n"
           "(\n"
           "   Type                      : 0x%.08X\n"
           "   pToken                    : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Type, pToken);

	// blueshogun96 10/1/07
	// I'm assuming this is the same as the PC version...

#ifndef D3D9
	HRESULT hRet = g_pD3DDevice->CreateStateBlock( Type, pToken );
#else
    HRESULT hRet = g_pD3DDevice->CreateStateBlock( Type, (XTL::IDirect3DStateBlock9**) pToken );
#endif

	if(FAILED(hRet))
		EmuWarning("CreateStateBlock failed!\n");

	EmuSwapFS();	// Xbox FS

	return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_InsertCallback
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_InsertCallback
(
	X_D3DCALLBACKTYPE	Type,
	X_D3DCALLBACK		pCallback,
	DWORD				Context
)
{
	EmuSwapFS();	// Win2K/XP FS

	/*DbgP*/printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_InsertCallback\n"
           "(\n"
		   "   Type                      : 0x%.08X\n"
		   "   pCallback                 : 0x%.08X\n"
		   "   Context                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Type, pCallback, Context);

	EmuWarning("InsertCallback ignored!\n");

	// TODO: Implement
	g_pCallback = (D3DCALLBACK) pCallback;
	g_CallbackType = Type;
	g_CallbackParam = Context;

	EmuSwapFS();	// Xbox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_DrawRectPatch
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_DrawRectPatch
(
	UINT					Handle,
	CONST FLOAT				*pNumSegs,
	CONST D3DRECTPATCH_INFO *pRectPatchInfo
)
{
	EmuSwapFS();	// Win2K/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_DrawRectPatch\n"
           "(\n"
		   "   Handle                    : 0x%.08X\n"
		   "   pNumSegs                  : %f\n"
		   "   pRectPatchInfo            : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Handle, *pNumSegs, pRectPatchInfo);

	HRESULT hRet = g_pD3DDevice->DrawRectPatch( Handle, pNumSegs, pRectPatchInfo );

	if(FAILED(hRet))
		EmuWarning("DrawRectPatch failed!\n");

	EmuSwapFS();	// Xbox FS

	return hRet;
}

#pragma warning(disable:4244)
// ******************************************************************
// * func: EmuIDirect3DDevice8_GetProjectionViewportMatrix
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetProjectionViewportMatrix
(
	D3DXMATRIX *pProjectionViewport
)
{
	EmuSwapFS();	// Win2K/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetProjectionViewportMatrix\n"
           "(\n"
		   "   pProjectionViewport       : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pProjectionViewport);

	// blueshogun96 1/25/10
	// It's been almost 3 years, but I think this is a better 
	// implementation.  Still probably not right, but better
	// then before.

	HRESULT hRet;
	D3DXMATRIX Out, mtxProjection, mtxViewport;
#ifndef D3D9
	D3DVIEWPORT8 Viewport;
#else
    D3DVIEWPORT9 Viewport;
#endif

	// Get current viewport
	hRet = g_pD3DDevice->GetViewport(&Viewport);
	if(FAILED(hRet))
		EmuWarning("Unable to get viewport!\n");

	// Get current projection matrix
	hRet = g_pD3DDevice->GetTransform(D3DTS_PROJECTION, &mtxProjection);
	if(FAILED(hRet))
		EmuWarning("Unable to get projection matrix!\n");

	// Clear the destination matrix
	::ZeroMemory(&Out, sizeof(D3DMATRIX));

	// Create the Viewport matrix manually
	// Direct3D8 doesn't give me everything I need in a viewport structure
	// (one thing I REALLY HATE!) so some constants will have to be used
	// instead.

	float ClipWidth = 2.0f;
	float ClipHeight = 2.0f;
	float ClipX = -1.0f;
	float ClipY = 1.0f;
	float Width = DWtoF(Viewport.Width);
	float Height = DWtoF(Viewport.Height);

	D3DXMatrixIdentity(&mtxViewport);
	mtxViewport._11 = Width / ClipWidth;
	mtxViewport._22 = -(Height / ClipHeight);
	mtxViewport._41 = -(ClipX * mtxViewport._11);
	mtxViewport._42 = -(ClipY * mtxViewport._22);

	// Multiply projection and viewport matrix together
	Out = mtxProjection * mtxViewport;

	*pProjectionViewport = Out;

//	__asm int 3;

	EmuSwapFS();	// Xbox FS

	return S_OK;
}
#pragma warning(default:4244)

// ******************************************************************
// * func: EmuIDirect3DDevice8_BackFillMode
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_BackFillMode
(
	DWORD Value
)
{
	EmuSwapFS();	// Win2K/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_BackFillMode\n"
           "(\n"
		   "   Value       : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Value);

	
	// blueshogun96 12/4/07
	// I haven't had access to Cxbx sources in a few months, great to be back :)
	//
	// Anyway, since standard Direct3D doesn't support the back fill mode
	// operation, this function will be ignored.  Things like this make me
	// think even more that an OpenGL port wouldn't hurt since OpenGL supports
	// nearly all of the missing features that Direct3D lacks.  The Xbox's version
	// of Direct3D was specifically created to take advantage of certain NVIDIA
	// GPU registers and provide more OpenGL-like features IHMO.

	EmuSwapFS();	// Xbox FS

	return S_OK;
}

// ******************************************************************
// * func: EmuD3DDevice_KickOff (D3D::CDevice::KickOff)
// ******************************************************************
VOID WINAPI XTL::EmuD3DDevice_KickOff()
{
	
	EmuSwapFS();	// Win2K/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuD3DDevice_KickOff()\n", GetCurrentThreadId());

	// TODO: Anything (kick off and NOT wait for idle)?
	// NOTE: We should actually emulate IDirect3DDevice8_KickPushBuffer()
	// instead of this function.  When needed, use the breakpoint (int 3)
	// to determine what is calling this function if it's something other
	// than IDirect3DDevice8_KickPushBuffer() itself.

//	__asm int 3;

	EmuSwapFS();	// Xbox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetTexture2
// ******************************************************************
XTL::X_D3DResource* WINAPI XTL::EmuIDirect3DDevice8_GetTexture2(DWORD Stage)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetTexture2\n"
           "(\n"
           "   Stage               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Stage);
	
	// Get the active texture from this stage
	X_D3DResource* pRet = EmuD3DActiveTexture[Stage];

	EmuSwapFS();	// Xbox FS

	return pRet;
}

// ******************************************************************
// * func: EmuD3DDevice_SetStateVB (D3D::CDevice::SetStateVB)
// ******************************************************************
VOID WINAPI XTL::EmuD3DDevice_SetStateVB( ULONG Unknown1 )
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuD3DDevice_SetStateVB\n"
			"(\n"
			"   Unknown1          : 0x%.08X\n"
			")\n",
			GetCurrentThreadId(), Unknown1);

	// TODO: Anything?
	__asm int 3;

	EmuSwapFS();
}

// ******************************************************************
// * func: EmuD3DDevice_SetStateUP (D3D::CDevice::SetStateUP)
// ******************************************************************
VOID WINAPI XTL::EmuD3DDevice_SetStateUP()
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuD3DDevice_SetStateUP()\n", GetCurrentThreadId());

	// TODO: Anything?
	__asm int 3;

	EmuSwapFS();
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetStipple
// ******************************************************************
void WINAPI XTL::EmuIDirect3DDevice8_SetStipple( DWORD* pPattern )
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetStipple\n"
			"(\n"
			"   pPattern          : 0x%.08X\n"
			")\n",
			GetCurrentThreadId(), pPattern);

	// We need an OpenGL port... badly

	EmuSwapFS();
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetSwapCallback
// ******************************************************************
void WINAPI XTL::EmuIDirect3DDevice8_SetSwapCallback
(
	D3DSWAPCALLBACK		pCallback
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetSwapCallback\n"
           "(\n"
           "   pCallback           : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pCallback);

	DbgPrintf("pCallback: = 0x%.08X\n", pCallback);

    g_pSwapCallback = pCallback;

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_PersistDisplay
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_PersistDisplay()
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_PersistDisplay()\n", GetCurrentThreadId());

	HRESULT hRet = S_OK;

	// TODO: If this functionality is ever really needed, an idea for 
	// implementation would be to save a copy of the backbuffer's contents
	// and free the memory after the next call to D3DDevice::Present().
	// This temporary data could also be made available to the Xbox game
	// through AvGetSavedDataAddress() since D3DDevice::GetPersistedDisplay2
	// just contains a call to that kernel function.  So far, Unreal Champ-
	// ionship is the only game that uses this functionality that I know of.
	// Other Unreal Engine 2.x games might as well.

#ifndef D3D9
	IDirect3DSurface8* pBackSurface = NULL;
	if( SUCCEEDED( g_pD3DDevice->GetBackBuffer( 0, D3DBACKBUFFER_TYPE_MONO, &pBackSurface ) ) )
#else
	IDirect3DSurface9* pBackSurface = NULL;
	if( SUCCEEDED( g_pD3DDevice->GetBackBuffer( 0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackSurface ) ) )
#endif
	{
		//D3DXSaveSurfaceToFile( "persisted_surface.bmp", D3DXIFF_BMP, pBackSurface, NULL, NULL );
		pBackSurface->Release();

		DbgPrintf("Persisted display surface saved to persisted_surface.bmp\n");
	}
	else
	{
		EmuWarning("(Temporarily) Not persisting display. Blueshogun can fix this.");
	}

	if(!g_pD3DDevice)
	{
		EmuWarning("Direct3D device not initialized!");
		hRet =  E_FAIL;
	}
	/*else
	{
		IDirect3DSurface8* pBackBuffer = NULL;
		D3DLOCKED_RECT LockedRect;
		D3DSURFACE_DESC BackBufferDesc;

		g_pD3DDevice->GetBackBuffer( 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer );
		
		pBackBuffer->GetDesc( &BackBufferDesc );

		DWORD dwBytesPerPixel = ( BackBufferDesc.Format == D3DFMT_X8R8G8B8 || BackBufferDesc.Format == D3DFMT_A8R8G8B8 ) ? 4 : 2;
		FILE* fp = fopen( "PersistedSurface.bin", "wb" );
		if(fp)
		{
			void* ptr = malloc( BackBufferDesc.Width * BackBufferDesc.Height * dwBytesPerPixel );

			if( SUCCEEDED( pBackBuffer->LockRect( &LockedRect, NULL, 0 ) ) )
			{
				CopyMemory( ptr, LockedRect.pBits, BackBufferDesc.Width * BackBufferDesc.Height * dwBytesPerPixel );
				
				fwrite( ptr, BackBufferDesc.Width * BackBufferDesc.Height * dwBytesPerPixel, 1, fp );

				pBackBuffer->UnlockRect();
			}
	
			fclose(fp);
		}
	}*/

	EmuSwapFS();	// Xbox FS

	return hRet;
}


// ******************************************************************
// * func: EmuIDirect3DDevice8_Unknown1
// ******************************************************************
void WINAPI XTL::EmuIDirect3DDevice8_Unknown1()
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_Unknown1()\n", GetCurrentThreadId());

	// TODO: Find out what this actually is.
	// This function was only found in Run Like Hell (5233) @ 0x11FCD0.
	// So far, this function hasn't been found in any other XDKs.  Since
	// the only major thing going on inside of it is a call to the kernel
	// function AvSendTVEncoderOption, we can probably ignore it.

	EmuSwapFS();	// Xbox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_PrimeVertexCache
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_PrimeVertexCache
(
	UINT  VertexCount,
	WORD *pIndexData
)
{
	EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_PrimeVertexCache\n"
           "(\n"
           "   VertexCount           : 0x%.08X\n"
		   "   pIndexData            : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), VertexCount, pIndexData);

	// TODO: Implement
	EmuWarning("PrimeVertexCache is not supported!");

	EmuSwapFS();	// Win2k/XP FS

	return S_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_SampleAlpha
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_SampleAlpha
(
	DWORD dwSampleAlpha
)
{
	EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_SampleAlpha\n"
           "(\n"
           "   dwSampleAlpha         : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), dwSampleAlpha);

	// TODO: Implement?

	EmuWarning("SampleAlpha not supported!");

	EmuSwapFS();	// Xbox FS

	return S_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_Deferred
// ******************************************************************
VOID __fastcall XTL::EmuIDirect3DDevice8_SetRenderState_Deferred
(
	DWORD State,
	DWORD Value
)
{
	EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_Deferred\n"
           "(\n"
           "   State               : 0x%.08X\n"
           "   Value               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), State, Value);

	// TODO: HACK: Technically, this function doesn't need to be emulated.
	// The location of EmuD3DDeferredRenderState for 3911 isn't correct and at
	// the time of writing, I don't understand how to fix it.  Until then, 
	// I'm going to implement this in a reckless manner.  When the offset for
	// EmuD3DDeferredRenderState is fixed for 3911, this function should be
	// obsolete!

	if( State > 81 && State < 116 )
		EmuD3DDeferredRenderState[State-82] = Value;
	else
		CxbxKrnlCleanup("Unknown Deferred RenderState! (%d)\n", State);

	/*
	XDK 3911 Deferred RenderState values
	D3DRS_FOGENABLE                 = 82,   // TRUE to enable fog blending 
    D3DRS_FOGTABLEMODE              = 83,   // D3DFOGMODE 
    D3DRS_FOGSTART                  = 84,   // float fog start (for both vertex and pixel fog) 
    D3DRS_FOGEND                    = 85,   // float fog end      
    D3DRS_FOGDENSITY                = 86,   // float fog density  
    D3DRS_RANGEFOGENABLE            = 87,   // TRUE to enable range-based fog 
    D3DRS_WRAP0                     = 88,   // D3DWRAP* flags (D3DWRAP_U, D3DWRAPCOORD_0, etc.) for 1st texture coord.
    D3DRS_WRAP1                     = 89,   // D3DWRAP* flags (D3DWRAP_U, D3DWRAPCOORD_0, etc.) for 2nd texture coord. 
    D3DRS_WRAP2                     = 90,   // D3DWRAP* flags (D3DWRAP_U, D3DWRAPCOORD_0, etc.) for 3rd texture coord. 
    D3DRS_WRAP3                     = 91,   // D3DWRAP* flags (D3DWRAP_U, D3DWRAPCOORD_0, etc.) for 4th texture coord. 
    D3DRS_LIGHTING                  = 92,   // TRUE to enable lighting
    D3DRS_SPECULARENABLE            = 93,   // TRUE to enable specular 
    D3DRS_LOCALVIEWER               = 94,   // TRUE to enable camera-relative specular highlights
    D3DRS_COLORVERTEX               = 95,   // TRUE to enable per-vertex color
    D3DRS_BACKSPECULARMATERIALSOURCE= 96,   // D3DMATERIALCOLORSOURCE (Xbox extension)
    D3DRS_BACKDIFFUSEMATERIALSOURCE = 97,   // D3DMATERIALCOLORSOURCE (Xbox extension)
    D3DRS_BACKAMBIENTMATERIALSOURCE = 98,   // D3DMATERIALCOLORSOURCE (Xbox extension)
    D3DRS_BACKEMISSIVEMATERIALSOURCE= 99,   // D3DMATERIALCOLORSOURCE (Xbox extension)
    D3DRS_SPECULARMATERIALSOURCE    = 100,  // D3DMATERIALCOLORSOURCE 
    D3DRS_DIFFUSEMATERIALSOURCE     = 101,  // D3DMATERIALCOLORSOURCE 
    D3DRS_AMBIENTMATERIALSOURCE     = 102,  // D3DMATERIALCOLORSOURCE 
    D3DRS_EMISSIVEMATERIALSOURCE    = 103,  // D3DMATERIALCOLORSOURCE 
    D3DRS_BACKAMBIENT               = 104,  // D3DCOLOR (Xbox extension)
    D3DRS_AMBIENT                   = 105,  // D3DCOLOR 
    D3DRS_POINTSIZE                 = 106,  // float point size 
    D3DRS_POINTSIZE_MIN             = 107,  // float point size min threshold 
    D3DRS_POINTSPRITEENABLE         = 108,  // TRUE to enable point sprites
    D3DRS_POINTSCALEENABLE          = 109,  // TRUE to enable point size scaling
    D3DRS_POINTSCALE_A              = 110,  // float point attenuation A value 
    D3DRS_POINTSCALE_B              = 111,  // float point attenuation B value 
    D3DRS_POINTSCALE_C              = 112,  // float point attenuation C value 
    D3DRS_POINTSIZE_MAX             = 113,  // float point size max threshold 
    D3DRS_PATCHEDGESTYLE            = 114,  // D3DPATCHEDGESTYLE
    D3DRS_PATCHSEGMENTS             = 115,  // DWORD number of segments per edge when drawing patches
	*/

	EmuSwapFS();	// Xbox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_DeleteStateBlock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_DeleteStateBlock
(
	DWORD Token
)
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_DeleteStateBlock\n"
		   "(\n"
		   "   Token               : 0x%.08X\n"
		   ");\n",
		   GetCurrentThreadId(), Token);

#ifndef D3D9
	HRESULT hRet = g_pD3DDevice->DeleteStateBlock(Token);
#else
    // TODO: Implement D3D9 equivalent
    // CreateStateBlock()
    // d3dDevice->DeleteStateBlock(stateblockHandle) == stateblock->Release()
    HRESULT hRet = E_FAIL; //g_pD3DDevice->DeleteStateBlock(Token);
#endif

	EmuSwapFS();	// Xbox FS

	return hRet;
}


// ******************************************************************
// * func: EmuIDirect3DDevice8_SetModelView
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetModelView
(
	CONST D3DMATRIX *pModelView, 
	CONST D3DMATRIX *pInverseModelView, 
	CONST D3DMATRIX *pComposite
)
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetModelView\n"
		   "(\n"
		   "   pModelView           : 0x%.08X\n"
		   "   pInverseModelView    : 0x%.08X\n"
		   "   pComposite           : 0x%.08X\n"
		   ");\n",
		   GetCurrentThreadId(), pModelView, pInverseModelView, pComposite);

	// TODO: Implement
//	CxbxKrnlCleanup("SetModelView not yet implemented (should be easy fix, tell blueshogun)");

	EmuSwapFS();	// Xbox FS

	return S_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_FlushVertexCache
// ******************************************************************
void WINAPI XTL::EmuIDirect3DDevice8_FlushVertexCache()
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_FlushVertexCache();\n", GetCurrentThreadId());

	EmuSwapFS();	// Xbox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_BeginPushBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_BeginPushBuffer
(
	X_D3DPushBuffer *pPushBuffer
)
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_BeginPushBuffer\n"
		   "(\n" 
		   "   pPushBuffer          : 0x%.08X\n"
		   ");\n", GetCurrentThreadId(), pPushBuffer);

	// TODO: Implement. Easier said than done with Direct3D, but OpenGL
	// can emulate this functionality rather easily.
//	CxbxKrnlCleanup("BeginPushBuffer is not yet implemented!\n"
	//				"This is going to be a difficult fix for Direct3D but NOT OpenGL!");
	EmuWarning("BeginPushBuffer is not yet implemented!");

	EmuSwapFS();	// Xbox FS

	return S_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_EndPushBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_EndPushBuffer()
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_EndPushBuffer();\n", GetCurrentThreadId());

	EmuSwapFS();	// Xbox FS

	return S_OK;
}

// ******************************************************************
// * func: EmuXMETAL_StartPush
// ******************************************************************
void WINAPI XTL::EmuXMETAL_StartPush(void* Unknown)
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuXMETAL_StartPush\n"
		   "(\n" 
		   "   Unknown           : 0x%.08X\n"
		   ");\n", GetCurrentThreadId(), Unknown);

	// This function is too low level to actually emulate
	// Only use for debugging.
	__asm int 3;

	EmuSwapFS();	// Xbox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetModelView
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetModelView(D3DXMATRIX* pModelView)
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetModelView\n"
		   "(\n" 
		   "   pModelView        : 0x%.08X\n"
		   ");\n", GetCurrentThreadId(), pModelView);

	D3DXMATRIX mtxWorld, mtxView;

	// I hope this is right
	g_pD3DDevice->GetTransform( D3DTS_WORLD, &mtxWorld );
	g_pD3DDevice->GetTransform( D3DTS_VIEW, &mtxView );

	*pModelView = mtxWorld * mtxView;

	EmuSwapFS();	// Xbox FS

	return S_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetBackMaterial
// ******************************************************************
#ifndef D3D9
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetBackMaterial(D3DMATERIAL8* pMaterial)
#else
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetBackMaterial(D3DMATERIAL9* pMaterial)
#endif
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetBackMaterial\n"
		   "(\n" 
		   "   pMaterial         : 0x%.08X\n"
		   ");\n", GetCurrentThreadId(), pMaterial);

	EmuWarning("SetBackMaterial is not supported!");

	EmuSwapFS();	// Xbox FS

	return S_OK;
}

// ******************************************************************
// * func: EmuIDirect3D8_GetAdapterIdentifier
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3D8_GetAdapterIdentifier
(
	UINT					Adapter,
	DWORD					Flags,
#ifndef D3D9
	D3DADAPTER_IDENTIFIER8* pIdentifier
#else
    D3DADAPTER_IDENTIFIER9* pIdentifier
#endif
)
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3D8_GetAdapterIdentifier\n"
			"(\n"
			"   Adapter          : 0x%.08X\n"
			"   Flags            : 0x%.08X\n"
			"   pIdentifier      : 0x%.08X (0x%.08X)\n"
			");\n", GetCurrentThreadId(), Adapter, Flags, pIdentifier, pIdentifier);

	// TODO: Fill the Intentifier structure with the content of what an Xbox would return.
	// It might not matter for now, but just in case.

	// NOTE: Games do not crash when this function is not intercepted (at least not so far)
	// so it's recommended to add this function to every XDK you possibly can as it will
	// save you much hassle (at least it did for Max Payne).

	HRESULT hRet = g_pD3D->GetAdapterIdentifier( Adapter, Flags, pIdentifier );
	if(FAILED(hRet))
		EmuWarning("GetAdapterIdentifier failed!");

	EmuSwapFS();	// Xbox FS

	return hRet;
}

// ******************************************************************
// * func: D3D::MakeRequestedSpace
// ******************************************************************
HRESULT WINAPI XTL::EmuD3D_MakeRequestedSpace( DWORD Unknown1, DWORD Unknown2 )
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuD3D_MakeRequestedSpace\n"
			"(\n"
			"   Unknown1         : 0x%.08X\n"
			"   Unknown2         : 0x%.08X\n"
			");\n", GetCurrentThreadId(), Unknown1, Unknown2);

	// NOTE: This function is not meant to me emulated.  Just use it to find out
	// the function that is calling it, and emulate that instead!!!  If necessary,
	// create an XRef...

	__asm int 3;
	CxbxKrnlCleanup("D3D::MakeRequestedSpace not implemented (tell blueshogun)");

	EmuSwapFS();	// Xbox FS

	return S_OK;
}

// ******************************************************************
// * func: D3DDevice_MakeSpace
// ******************************************************************
void WINAPI XTL::EmuD3DDevice_MakeSpace()
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf( "EmuD3D8 (0x%X): EmuD3DDevice_MakeSpace();\n", GetCurrentThreadId());

	// NOTE: Like the above function, this should not be emulated.  The intended
	// usage is the same as above.

	__asm int 3;
	CxbxKrnlCleanup("D3DDevice::MakeSpace not implemented (tell blueshogun)");

	EmuSwapFS();	// Xbox FS
}

// ******************************************************************
// * func: D3D::SetCommonDebugRegisters
// ******************************************************************
void WINAPI XTL::EmuD3D_SetCommonDebugRegisters()
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf( "EmuD3D8 (0x%X): EmuD3D_SetCommonDebugRegisters();\n", GetCurrentThreadId());

	// NOTE: I added this because I was too lazy to deal with emulating certain render
	// states that use it.  

	EmuSwapFS();	// Xbox FS
}

// ******************************************************************
// * func: D3D::BlockOnTime
// ******************************************************************
void WINAPI XTL::EmuD3D_BlockOnTime( DWORD Unknown1, int Unknown2 )
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuD3D_BlockOnTime\n"
			"(\n"
			"   Unknown1         : 0x%.08X\n"
			"   Unknown2         : 0x%.08X\n"
			");\n", GetCurrentThreadId(), Unknown1, Unknown2);

	// NOTE: This function is not meant to me emulated.  Just use it to find out
	// the function that is calling it, and emulate that instead!!!  If necessary,
	// create an XRef...

//	__asm int 3;
	CxbxKrnlCleanup("D3D::BlockOnTime not implemented (tell blueshogun)");

	EmuSwapFS();	// Xbox FS
}

// ******************************************************************
// * func: D3D::BlockOnResource
// ******************************************************************
void WINAPI XTL::EmuD3D_BlockOnResource( X_D3DResource* pResource )
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuD3D_BlockOnResource\n"
			"(\n"
			"   pResource         : 0x%.08X\n"
			");\n", GetCurrentThreadId(), pResource);

	// TODO: Implement
	// NOTE: Azurik appears to call this directly from numerous points

	EmuSwapFS();	// Xbox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetPushBufferOffset
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetPushBufferOffset
(
	DWORD *pOffset
)
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetPushBufferOffset\n"
			"(\n"
			"   pOffset            : 0x%.08X\n"
			");\n", GetCurrentThreadId(), pOffset);

	// TODO: Implement
	*pOffset = 0;

	EmuSwapFS();	// Xbox FS

	return S_OK;
}

// ******************************************************************
// * func: EmuIDirect3DCubeTexture8_GetCubeMapSurface
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DCubeTexture8_GetCubeMapSurface
(
	X_D3DCubeTexture*	pThis,
	D3DCUBEMAP_FACES	FaceType,
	UINT				Level,
	X_D3DSurface**		ppCubeMapSurface
)
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DCubeTexture8_GetCubeMapSurface\n"
			"(\n"
			"   pThis              : 0x%.08X\n"
			"   FaceType           : 0x%.08X\n"
			"   Level              : 0x%.08X\n"
			"   ppCubeMapSurface   : 0x%.08X (0x%.08X)\n"
			");\n", GetCurrentThreadId(), pThis, FaceType, Level, ppCubeMapSurface, *ppCubeMapSurface);

	HRESULT hRet;

	// Create a new surface
	*ppCubeMapSurface = new X_D3DSurface;

	hRet = pThis->EmuCubeTexture8->GetCubeMapSurface( FaceType, Level, &(*ppCubeMapSurface)->EmuSurface8 );

	EmuSwapFS();	// Xbox FS

	return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DCubeTexture8_GetCubeMapSurface2
// ******************************************************************
XTL::X_D3DSurface* WINAPI XTL::EmuIDirect3DCubeTexture8_GetCubeMapSurface2
(
	X_D3DCubeTexture*	pThis,
	D3DCUBEMAP_FACES	FaceType,
	UINT				Level
)
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DCubeTexture8_GetCubeMapSurface2\n"
			"(\n"
			"   pThis              : 0x%.08X\n"
			"   FaceType           : 0x%.08X\n"
			"   Level              : 0x%.08X\n"
			");\n", GetCurrentThreadId(), pThis, FaceType, Level);

	HRESULT hRet;

	// Create a new surface
	X_D3DSurface* pCubeMapSurface = new X_D3DSurface;

	hRet = pThis->EmuCubeTexture8->GetCubeMapSurface( FaceType, Level, &pCubeMapSurface->EmuSurface8 );

	EmuSwapFS();	// Xbox FS

	return pCubeMapSurface;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetPixelShader
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetPixelShader
(
	DWORD  Name,
	DWORD* pHandle
)
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetPixelShader\n"
			"(\n"
			"   Name               : 0x%.08X\n"
			"   pHandle            : 0x%.08X\n"
			");\n", GetCurrentThreadId(), Name, pHandle);

	// TODO: This implementation is very wrong, but better than nothing.
	*pHandle = g_dwCurrentPixelShader;

	EmuSwapFS();	// Xbox FS

	return S_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetPersistedSurface
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetPersistedSurface(X_D3DSurface **ppSurface)
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetPersistedSurface\n"
			"(\n"
			"   ppSurface          : 0x%.08X\n"
			");\n", GetCurrentThreadId(), ppSurface);

	// Attempt to load the persisted surface from persisted_surface.bmp

	*ppSurface = new X_D3DSurface;

#ifndef D3D9
	HRESULT hr = g_pD3DDevice->CreateImageSurface( 640, 480, D3DFMT_X8R8G8B8, &(*ppSurface)->EmuSurface8 );
#else
	HRESULT hr = g_pD3DDevice->CreateOffscreenPlainSurface( 640, 480, D3DFMT_X8R8G8B8, D3DPOOL_SCRATCH /*D3DPOOL_MANAGED*/, &(*ppSurface)->EmuSurface8, NULL );
#endif
	if( SUCCEEDED( hr ) )
	{
		hr = D3DXLoadSurfaceFromFileA( (*ppSurface)->EmuSurface8, NULL, NULL, "persisted_surface.bmp",
			NULL, D3DX_DEFAULT, 0, NULL );
		if( SUCCEEDED( hr ) )
		{
			DbgPrintf( "Successfully loaded persisted_surface.bmp\n" );
		}
		else
		{
			EmuWarning( "Could not load persisted_surface.bmp!\n" );
		}
	}
	else
	{
		EmuWarning( "Could not create temporary surface!" );
	}
	
	EmuSwapFS();	// Xbox FS

	return S_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetPersistedSurface2
// ******************************************************************
XTL::X_D3DSurface* WINAPI XTL::EmuIDirect3DDevice8_GetPersistedSurface2()
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetPersistedSurface2()\n", GetCurrentThreadId());

	// Attempt to load the persisted surface from persisted_surface.bmp

	X_D3DSurface* pSurface = new X_D3DSurface;

#ifndef D3D9
	HRESULT hr = g_pD3DDevice->CreateImageSurface( 640, 480, D3DFMT_X8R8G8B8, &pSurface->EmuSurface8 );
#else
    HRESULT hr = g_pD3DDevice->CreateOffscreenPlainSurface( 640, 480, D3DFMT_X8R8G8B8, D3DPOOL_SCRATCH /*D3DPOOL_MANAGED*/, &pSurface->EmuSurface8, NULL );
#endif
	if( SUCCEEDED( hr ) )
	{
		hr = D3DXLoadSurfaceFromFileA( pSurface->EmuSurface8, NULL, NULL, "persisted_surface.bmp",
			NULL, D3DX_DEFAULT, 0, NULL );
		if( SUCCEEDED( hr ) )
		{
			DbgPrintf( "Successfully loaded persisted_surface.bmp\n" );
		}
		else
		{
			EmuWarning( "Could not load persisted_surface.bmp!\n" );
		}
	}
	else
	{
		EmuWarning( "Could not create temporary surface!" );
	}
	
	EmuSwapFS();	// Xbox FS

	return pSurface;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderTargetFast
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetRenderTargetFast
(
    X_D3DSurface	*pRenderTarget,
    X_D3DSurface	*pNewZStencil,
    DWORD			Flags
)
{
	EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderTarget\n"
           "(\n"
           "   pRenderTarget       : 0x%.08X (0x%.08X)\n"
           "   pNewZStencil        : 0x%.08X (0x%.08X)\n"
		   "   Flags               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pRenderTarget, (pRenderTarget != 0) ? pRenderTarget->EmuSurface8 : 0, pNewZStencil,
           (pNewZStencil != 0) ? pNewZStencil->EmuSurface8 : 0, Flags);

	// Redirect to the standard version.
	EmuSwapFS();
	HRESULT hr = EmuIDirect3DDevice8_SetRenderTarget(pRenderTarget, pNewZStencil);
	EmuSwapFS();

	EmuSwapFS();

	return hr;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetScissors
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetScissors
(
	DWORD	*pCount, 
	BOOL	*pExclusive, 
	D3DRECT *pRects
)
{
	EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetScissors\n"
           "(\n"
           "   pCount              : 0x%.08X\n"
           "   pExclusive          : 0x%.08X\n"
           "   pRects              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pCount, pExclusive, pRects);

    // TODO: Save a copy of each scissor rect in case this function is called
	// in conjunction with D3DDevice::SetScissors. So far, only Outrun2 uses
	// this function. For now, just return the values within the current
	// viewport.

#ifndef D3D9
	D3DVIEWPORT8 vp;
#else
    D3DVIEWPORT9 vp;
#endif

	g_pD3DDevice->GetViewport( &vp );

	pRects->x1 = pRects->y1 = 0;
	pRects->x2 = vp.Width;
	pRects->y2 = vp.Height;

	pExclusive[0] = FALSE;

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetBackMaterial
// ******************************************************************
#ifndef D3D9
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetBackMaterial(D3DMATERIAL8* pMaterial)
#else
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetBackMaterial(D3DMATERIAL9* pMaterial)
#endif
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetBackMaterial\n"
		   "(\n" 
		   "   pMaterial         : 0x%.08X\n"
		   ");\n", GetCurrentThreadId(), pMaterial);

	EmuWarning("GetBackMaterial is not supported!");

	// TODO: HACK: This is wrong, but better than nothing, right?

	if( pMaterial )
		g_pD3DDevice->GetMaterial( pMaterial );

	EmuSwapFS();	// Xbox FS

	return S_OK;
}