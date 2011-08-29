/**
 * Copyright (C) 2010 Jorge Jimenez (jorge@iryoku.com). All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    1. Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are 
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the copyright holders.
 */


#include "DXUT.h"
#include "DXUTgui.h"
#include "DXUTsettingsDlg.h"
#include "SDKmisc.h"

#include <sstream>
#include <fstream>
#include <iomanip>
#include <cmath>

#include "Timer.h"
#include "RenderTarget.h"
#include "Copy.h"
#include "SMAA.h"

using namespace std;


const int HUD_WIDTH = 140;

CDXUTDialogResourceManager dialogResourceManager;
CD3DSettingsDlg settingsDialog;
CDXUTDialog hud;

Timer *timer = NULL;
ID3DX10Font *font = NULL;
ID3DX10Sprite *sprite = NULL;
CDXUTTextHelper *txtHelper = NULL;

SMAA *smaa = NULL;
DepthStencil *depthStencil = NULL;
RenderTarget *depthBufferRenderTarget = NULL;
BackbufferRenderTarget *backbufferRenderTarget = NULL;
RenderTarget *finalRenderTargetSRGB = NULL;
RenderTarget *finalRenderTarget = NULL;

ID3D10ShaderResourceView *testColorSRV = NULL;
ID3D10ShaderResourceView *testDepthSRV = NULL;

bool showHud = true;

bool benchmark = false;
fstream benchmarkFile;

struct {
    float threshold;
    int distance;
    wstring src;
    wstring dst;
} commandlineOptions = {0.1f, 8, L"", L""};


#define IDC_TOGGLEFULLSCREEN      1
#define IDC_CHANGEDEVICE          2
#define IDC_LOADIMAGE             3
#define IDC_DETECTIONMODE         4
#define IDC_VIEWMODE              5
#define IDC_INPUT                 6
#define IDC_ANTIALIASING          7
#define IDC_PROFILE               8
#define IDC_MAXSEARCHSTEPS_LABEL  9
#define IDC_MAXSEARCHSTEPS       10
#define IDC_THRESHOLD_LABEL      11
#define IDC_THRESHOLD            12


float round(float n) {
    return floor(n + 0.5f);
}


bool CALLBACK isDeviceAcceptable(UINT adapter, UINT output, D3D10_DRIVER_TYPE deviceType, DXGI_FORMAT format, bool windowed, void *context) {
    return true;
}


void loadImage() {
    HRESULT hr;

    D3DX10_IMAGE_LOAD_INFO loadInfo = D3DX10_IMAGE_LOAD_INFO();
    loadInfo.MipLevels = 1;
    loadInfo.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    loadInfo.Filter = D3DX10_FILTER_POINT | D3DX10_FILTER_SRGB_IN;

    WCHAR *text = hud.GetComboBox(IDC_INPUT)->GetSelectedItem()->strText;
    if (int(hud.GetComboBox(IDC_INPUT)->GetSelectedData()) != -1) { // ... then, we have to search for it in the executable resources
        SAFE_RELEASE(testColorSRV);
        SAFE_RELEASE(testDepthSRV);

        // Load color
        V(D3DX10CreateShaderResourceViewFromResource(DXUTGetD3D10Device(), GetModuleHandle(NULL), text, &loadInfo, NULL, &testColorSRV, NULL));

        // Try to load depth
        loadInfo.Format = DXGI_FORMAT_R32_FLOAT;
        loadInfo.Filter = D3DX10_FILTER_POINT;

        wstring path = wstring(text).substr(0, wstring(text).find_last_of('.')) + L".dds";
        if (FindResource(GetModuleHandle(NULL), path.c_str(), RT_RCDATA) != NULL)
            V(D3DX10CreateShaderResourceViewFromResource(DXUTGetD3D10Device(), GetModuleHandle(NULL), path.c_str(), &loadInfo, NULL, &testDepthSRV, NULL));
    } else { // ... search for it in the file system
        ID3D10ShaderResourceView *colorSRV= NULL;
        if (FAILED(D3DX10CreateShaderResourceViewFromFile(DXUTGetD3D10Device(), text, &loadInfo, NULL, &colorSRV, NULL))) {
            MessageBox(NULL, L"Unable to open selected file", L"ERROR", MB_OK | MB_SETFOREGROUND | MB_TOPMOST);
            hud.GetComboBox(IDC_INPUT)->RemoveItem(hud.GetComboBox(IDC_INPUT)->FindItem(text));
            if (commandlineOptions.src != L"") {
                exit(1);
            } else {
                loadImage(); // Fallback to previous image
            }
            return;
        } else {
            SAFE_RELEASE(testColorSRV);
            SAFE_RELEASE(testDepthSRV);

            testColorSRV = colorSRV;

            ID3D10ShaderResourceView *depthSRV;
            wstring path = wstring(text).substr(0, wstring(text).find_last_of('.')) + L".dds";
            if (SUCCEEDED(D3DX10CreateShaderResourceViewFromFile(DXUTGetD3D10Device(), path.c_str(), &loadInfo, NULL, &depthSRV, NULL)))
                testDepthSRV = depthSRV;
        }
    }

    int selectedIndex = hud.GetComboBox(IDC_DETECTIONMODE)->GetSelectedIndex();
    hud.GetComboBox(IDC_DETECTIONMODE)->RemoveAllItems();
    hud.GetComboBox(IDC_DETECTIONMODE)->AddItem(L"Luma edge det.", (LPVOID) 0);
    hud.GetComboBox(IDC_DETECTIONMODE)->AddItem(L"Color edge det.", (LPVOID) 1);
    if (testDepthSRV != NULL)
        hud.GetComboBox(IDC_DETECTIONMODE)->AddItem(L"Depth edge det.", (LPVOID) 2);
    hud.GetComboBox(IDC_DETECTIONMODE)->SetSelectedByIndex(selectedIndex);
}


void resizeWindow() {
    ID3D10Texture2D *texture2D;
    testColorSRV->GetResource(reinterpret_cast<ID3D10Resource **>(&texture2D));
    D3D10_TEXTURE2D_DESC desc;
    texture2D->GetDesc(&desc);
    texture2D->Release();

    RECT rect = {0, 0, desc.Width, desc.Height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    SetWindowPos(DXUTGetHWNDDeviceWindowed(), 0, 0, 0,  (rect.right - rect.left), (rect.bottom - rect.top), SWP_NOZORDER | SWP_NOMOVE);
}


HRESULT CALLBACK onCreateDevice(ID3D10Device *device, const DXGI_SURFACE_DESC *desc, void *context) {
    HRESULT hr;
    V_RETURN(dialogResourceManager.OnD3D10CreateDevice(device));
    V_RETURN(settingsDialog.OnD3D10CreateDevice(device));

    V_RETURN(D3DX10CreateFont(device, 15, 0, FW_BOLD, 1, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                              L"Arial", &font));
    V_RETURN(D3DX10CreateSprite(device, 512, &sprite));
    txtHelper = new CDXUTTextHelper(NULL, NULL, font, sprite, 15);

    timer = new Timer(device);
    timer->setEnabled(hud.GetCheckBox(IDC_PROFILE)->GetChecked());
    timer->setRepetitionsCount(100);

    Copy::init(device);

    loadImage();

    return S_OK;
}


void CALLBACK onDestroyDevice(void *context) {
    dialogResourceManager.OnD3D10DestroyDevice();
    settingsDialog.OnD3D10DestroyDevice();
    DXUTGetGlobalResourceCache().OnDestroyDevice();

    SAFE_RELEASE(font);
    SAFE_RELEASE(sprite);
    SAFE_DELETE(txtHelper);

    SAFE_DELETE(timer);

    Copy::release();
    
    SAFE_RELEASE(testColorSRV);
    SAFE_RELEASE(testDepthSRV);
}


void initSMAA() {
    int min, max;
    float scale;

    CDXUTSlider *slider = hud.GetSlider(IDC_MAXSEARCHSTEPS);
    slider->GetRange(min, max);
    scale = float(slider->GetValue()) / (max - min);
    smaa->setMaxSearchSteps(int(round(scale * 98.0f)));

    slider = hud.GetSlider(IDC_THRESHOLD);
    slider->GetRange(min, max);
    scale = float(slider->GetValue()) / (max - min);
    smaa->setThreshold(scale * 0.5f);
}


HRESULT CALLBACK onResizedSwapChain(ID3D10Device *device, IDXGISwapChain *swapChain, const DXGI_SURFACE_DESC *desc, void *context) {
    HRESULT hr;
    V_RETURN(dialogResourceManager.OnD3D10ResizedSwapChain(device, desc));
    V_RETURN(settingsDialog.OnD3D10ResizedSwapChain(device, desc));

    hud.SetLocation(desc->Width - (45 + HUD_WIDTH), 0);

    smaa = new SMAA(device, desc->Width, desc->Height);
    initSMAA();
    depthStencil = new DepthStencil(device, desc->Width, desc->Height,  DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
    depthBufferRenderTarget = new RenderTarget(device, desc->Width, desc->Height, DXGI_FORMAT_R32_FLOAT);
    backbufferRenderTarget = new BackbufferRenderTarget(device, DXUTGetDXGISwapChain());
    finalRenderTargetSRGB = new RenderTarget(device, desc->Width, desc->Height, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    finalRenderTarget = new RenderTarget(device, *finalRenderTargetSRGB, DXGI_FORMAT_R8G8B8A8_UNORM);

    return S_OK;
}


void CALLBACK onReleasingSwapChain(void *context) {
    dialogResourceManager.OnD3D10ReleasingSwapChain();

    SAFE_DELETE(smaa);
    SAFE_DELETE(depthStencil);
    SAFE_DELETE(depthBufferRenderTarget);
    SAFE_DELETE(backbufferRenderTarget);
    SAFE_DELETE(finalRenderTargetSRGB);
    SAFE_DELETE(finalRenderTarget);
}


void drawHud(ID3D10Device *device, float elapsedTime) {
    if (showHud) {
        hud.OnRender(elapsedTime);

        txtHelper->Begin();

        txtHelper->SetInsertionPos(2, 0);
        txtHelper->SetForegroundColor(D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f));
        txtHelper->DrawTextLine(DXUTGetFrameStats(DXUTIsVsyncEnabled()));
        txtHelper->DrawTextLine(DXUTGetDeviceStats());
        txtHelper->DrawTextLine(L"Press 'tab' to toogle the HUD, 'a' and 'd' to quickly cycle through the images");

        if (timer->isEnabled()) {
            wstringstream s;
            s << setprecision(2) << std::fixed;
            s << *timer;
            txtHelper->DrawTextLine(s.str().c_str());
        }

        txtHelper->End();
    }
}


void saveBackbuffer(ID3D10Device *device) {
    HRESULT hr;
    RenderTarget *renderTarget = new RenderTarget(device, backbufferRenderTarget->getWidth(), backbufferRenderTarget->getHeight(), DXGI_FORMAT_R8G8B8A8_UNORM, NoMSAA(), false);
    device->CopyResource(*renderTarget, *backbufferRenderTarget);
    V(D3DX10SaveTextureToFile(*renderTarget, D3DX10_IFF_PNG, commandlineOptions.dst.c_str()));
    SAFE_DELETE(renderTarget);
}


void doBenchmark() {
    benchmarkFile << timer->getSection(L"SMAA") << endl;

    int next = hud.GetComboBox(IDC_INPUT)->GetSelectedIndex() + 1;
    int n = hud.GetComboBox(IDC_INPUT)->GetNumItems();
    hud.GetComboBox(IDC_INPUT)->SetSelectedByIndex(next < n? next : n);
    loadImage();

    timer->reset();

    if (next == n) {
        benchmark = false;
        benchmarkFile.close();
    }
}


void drawTextures(ID3D10Device *device) {
    switch (int(hud.GetComboBox(IDC_VIEWMODE)->GetSelectedData())) {
        case 1:
            Copy::go(*smaa->getEdgeRenderTarget(), *backbufferRenderTarget);
            break;
        case 2:
            Copy::go(*smaa->getBlendRenderTarget(), *backbufferRenderTarget);
            break;
        default:
            break;
    }
}


void CALLBACK onFrameRender(ID3D10Device *device, double time, float elapsedTime, void *context) {
    if (settingsDialog.IsActive()) {
        settingsDialog.OnRender(elapsedTime);
        return;
    }

    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    device->ClearRenderTargetView(*backbufferRenderTarget, clearColor);
    device->ClearDepthStencilView(*depthStencil, D3D10_CLEAR_STENCIL, 1.0, 0);

    // This is our simulated main render-to-backbuffer pass.
    D3D10_VIEWPORT viewport = Utils::viewportFromView(testColorSRV);
    Copy::go(testColorSRV, *finalRenderTargetSRGB, &viewport);
    Copy::go(testDepthSRV, *depthBufferRenderTarget, &viewport);

    // Run SMAA
    if (hud.GetCheckBox(IDC_ANTIALIASING)->GetChecked()) {
        SMAA::Input input = SMAA::Input(int(hud.GetComboBox(IDC_DETECTIONMODE)->GetSelectedData()));
        int n = hud.GetCheckBox(IDC_PROFILE)->GetChecked()? timer->getRepetitionsCount() : 1;

        timer->start();
        for (int i = 0; i < n; i++) { // This loop is just for profiling.
            switch (input) {
                case SMAA::INPUT_LUMA:
                case SMAA::INPUT_COLOR:
                    smaa->go(*finalRenderTarget, *finalRenderTargetSRGB, *backbufferRenderTarget, *depthStencil, input);
                    break;
                case SMAA::INPUT_DEPTH:
                    smaa->go(*depthBufferRenderTarget, *finalRenderTargetSRGB, *backbufferRenderTarget, *depthStencil, input);
                    break;
            }
        }
        timer->clock(L"SMAA");
    } else {
        Copy::go(*finalRenderTargetSRGB, *backbufferRenderTarget);
    }

    if (commandlineOptions.dst != L"") {
        saveBackbuffer(device);
        exit(0);
    }

    if (benchmark)
        doBenchmark();

    DXUT_BeginPerfEvent(DXUT_PERFEVENTCOLOR, L"HUD / Stats"); // These events are to help PIX identify what the code is doing
    drawTextures(device);
    drawHud(device, elapsedTime);
    DXUT_EndPerfEvent();
}


LRESULT CALLBACK msgProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, bool *finished, void *context) {
    *finished = dialogResourceManager.MsgProc(hwnd, msg, wparam, lparam);
    if (*finished)
        return 0;

    if (settingsDialog.IsActive()) {
        settingsDialog.MsgProc(hwnd, msg, wparam, lparam);
        return 0;
    }

    *finished = hud.MsgProc(hwnd, msg, wparam, lparam);
    if (*finished)
        return 0;

    return 0;
}


void CALLBACK keyboardProc(UINT nchar, bool keyDown, bool altDown, void *context) {
    if (keyDown)
    switch (nchar) {
        case VK_TAB: {
            if (keyDown)
                showHud = !showHud;
            break;
        }
        case 'A': {
            if (keyDown) {
                int previous = hud.GetComboBox(IDC_INPUT)->GetSelectedIndex() - 1;
                hud.GetComboBox(IDC_INPUT)->SetSelectedByIndex(previous > 0? previous : 0);
                loadImage();
            }
            break;
        }          
        case 'D': {
            if (keyDown) {
                int next = hud.GetComboBox(IDC_INPUT)->GetSelectedIndex() + 1;
                int n = hud.GetComboBox(IDC_INPUT)->GetNumItems();
                hud.GetComboBox(IDC_INPUT)->SetSelectedByIndex(next < n? next : n);
                loadImage();
            }
            break;
        }
        case 'X':
            hud.GetCheckBox(IDC_PROFILE)->SetChecked(!hud.GetCheckBox(IDC_PROFILE)->GetChecked());
            timer->setEnabled(hud.GetCheckBox(IDC_PROFILE)->GetChecked());
            break;
        case 'Z':
            hud.GetCheckBox(IDC_ANTIALIASING)->SetChecked(!hud.GetCheckBox(IDC_ANTIALIASING)->GetChecked());
            break;
        case 'P':
            hud.GetCheckBox(IDC_PROFILE)->SetChecked(true);
            timer->setEnabled(true);
            benchmark = true;
            benchmarkFile.open("Benchmark.txt",  ios_base::out);
            break;
    }
}


void setAdapter(DXUTDeviceSettings *settings) {
    HRESULT hr;

    // Look for 'NVIDIA PerfHUD' adapter. If it is present, override default settings.
    IDXGIFactory *factory;
    V(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**) &factory));

    // Search for a PerfHUD adapter.
    IDXGIAdapter *adapter = NULL;
    int i = 0;
    while (factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND) {
        if (adapter) {
            DXGI_ADAPTER_DESC desc;
            if (SUCCEEDED(adapter->GetDesc(&desc))) {
                const bool isPerfHUD = wcscmp(desc.Description, L"NVIDIA PerfHUD") == 0;

                if(isPerfHUD) {
                    // IMPORTANT: we modified DXUT for this to work. Search for <PERFHUD_FIX> in DXUT.cpp
                    settings->d3d10.AdapterOrdinal = i;
                    settings->d3d10.DriverType = D3D10_DRIVER_TYPE_REFERENCE;
                    break;
                }
            }
        }
        i++;
    }
}


bool CALLBACK modifyDeviceSettings(DXUTDeviceSettings *settings, void *context) {
    setAdapter(settings);

    settingsDialog.GetDialogControl()->GetComboBox(DXUTSETTINGSDLG_D3D10_MULTISAMPLE_COUNT)->SetEnabled(false);
    settingsDialog.GetDialogControl()->GetComboBox(DXUTSETTINGSDLG_D3D10_MULTISAMPLE_QUALITY)->SetEnabled(false);
    settingsDialog.GetDialogControl()->GetStatic(DXUTSETTINGSDLG_D3D10_MULTISAMPLE_COUNT_LABEL)->SetEnabled(false);
    settingsDialog.GetDialogControl()->GetStatic(DXUTSETTINGSDLG_D3D10_MULTISAMPLE_QUALITY_LABEL)->SetEnabled(false);
    settings->d3d10.AutoCreateDepthStencil = false;
    settings->d3d10.sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
    return true;
}


void buildInputComboBox() {
    hud.GetComboBox(IDC_INPUT)->RemoveAllItems();

    for (int i = 0; i < 7; i++) {
        wstringstream s;
        s.fill('0');
        s << L"Unigine" << setw(2) << i + 1 << ".png";
        hud.GetComboBox(IDC_INPUT)->AddItem(s.str().c_str(), NULL);
    }

    WIN32_FIND_DATA data;
    HANDLE handle = FindFirstFile(L"Images\\*.png", &data);
    if (handle != INVALID_HANDLE_VALUE){
        do {
            hud.GetComboBox(IDC_INPUT)->AddItem((L"Images\\" + wstring(data.cFileName)).c_str(), (LPVOID) -1);
        } while(FindNextFile(handle, &data));
        FindClose(handle);
    }
}


void CALLBACK onGUIEvent(UINT event, int id, CDXUTControl *control, void *context) {
    switch (id) {
        case IDC_TOGGLEFULLSCREEN:
            DXUTToggleFullScreen();
            break;
        case IDC_CHANGEDEVICE:
            settingsDialog.SetActive(!settingsDialog.IsActive());
            break;
        case IDC_LOADIMAGE: {
            if (!DXUTIsWindowed()) 
                DXUTToggleFullScreen();

            OPENFILENAME params;
            ZeroMemory(&params, sizeof(OPENFILENAME));
            
            const int maxLength = 512;
            WCHAR file[maxLength]; 
            file[0] = 0;

            params.lStructSize   = sizeof(OPENFILENAME);
            params.hwndOwner     = DXUTGetHWND();
            params.lpstrFilter   = L"Image Files (*.bmp, *.jpg, *.png)\0*.bmp;*.jpg;*.png\0\0";
            params.nFilterIndex  = 1;
            params.lpstrFile     = file;
            params.nMaxFile      = maxLength;
            params.lpstrTitle    = L"Open File";
            params.lpstrInitialDir = file;
            params.nMaxFileTitle = sizeof(params.lpstrTitle);
            params.Flags         = OFN_FILEMUSTEXIST;
            params.dwReserved    = OFN_EXPLORER;

            if (GetOpenFileName(&params)) {
                buildInputComboBox();
                hud.GetComboBox(IDC_INPUT)->AddItem(file, (LPVOID) -1);
                hud.GetComboBox(IDC_INPUT)->SetSelectedByData((LPVOID) -1);

                loadImage();
                resizeWindow();
                onReleasingSwapChain(NULL);
                onResizedSwapChain(DXUTGetD3D10Device(), DXUTGetDXGISwapChain(), DXUTGetDXGIBackBufferSurfaceDesc(), NULL);
            }
        }
        case IDC_VIEWMODE:
            if (event == EVENT_COMBOBOX_SELECTION_CHANGED) {
                if (int(hud.GetComboBox(IDC_VIEWMODE)->GetSelectedData()) > 0) {
                    hud.GetCheckBox(IDC_ANTIALIASING)->SetChecked(true);
                }
            }
            break;
        case IDC_INPUT:
            if (event == EVENT_COMBOBOX_SELECTION_CHANGED) {
                timer->reset();            
                loadImage();
                resizeWindow();
                onReleasingSwapChain(NULL);
                onResizedSwapChain(DXUTGetD3D10Device(), DXUTGetDXGISwapChain(), DXUTGetDXGIBackBufferSurfaceDesc(), NULL);
            }
            break;
        case IDC_ANTIALIASING:
            if (event == EVENT_CHECKBOX_CHANGED) {
                timer->reset();
                hud.GetComboBox(IDC_VIEWMODE)->SetSelectedByIndex(0);
            }
            break;
        case IDC_PROFILE:
            if (event == EVENT_CHECKBOX_CHANGED) {
                timer->reset();
                timer->setEnabled(hud.GetCheckBox(IDC_PROFILE)->GetChecked());
            }
            break;
        case IDC_MAXSEARCHSTEPS:
            if (event == EVENT_SLIDER_VALUE_CHANGED) {
                CDXUTSlider *slider = (CDXUTSlider *) control;
                int min, max;
                slider->GetRange(min, max);

                float scale = float(slider->GetValue()) / (max - min);
                smaa->setMaxSearchSteps(int(round(scale * 98.0f)));
            
                wstringstream s;
                s << L"Max Search Steps: " << int(round(scale * 98.0f));
                hud.GetStatic(IDC_MAXSEARCHSTEPS_LABEL)->SetText(s.str().c_str());
            }
            break;
        case IDC_THRESHOLD:
            if (event == EVENT_SLIDER_VALUE_CHANGED) {
                CDXUTSlider *slider = (CDXUTSlider *) control;
                int min, max;
                slider->GetRange(min, max);

                float scale = float(slider->GetValue()) / (max - min);
                smaa->setThreshold(scale * 0.5f);
            
                wstringstream s;
                s << L"Threshold: " << scale * 0.5f;
                hud.GetStatic(IDC_THRESHOLD_LABEL)->SetText(s.str().c_str());
            }
            break;
        default:
            break;
    }
}


void initApp() {
    settingsDialog.Init(&dialogResourceManager);
    hud.Init(&dialogResourceManager);
    hud.SetCallback(onGUIEvent);

    int iY = 10;

    hud.AddButton(IDC_TOGGLEFULLSCREEN, L"Toggle full screen", 35, iY, HUD_WIDTH, 22);
    hud.AddButton(IDC_CHANGEDEVICE, L"Change device", 35, iY += 24, HUD_WIDTH, 22, VK_F2);
    hud.AddButton(IDC_LOADIMAGE, L"Load image", 35, iY += 24, HUD_WIDTH, 22);
    
    hud.AddComboBox(IDC_DETECTIONMODE, 35, iY += 24, HUD_WIDTH, 22, 0, false);
    hud.GetComboBox(IDC_DETECTIONMODE)->AddItem(L"Luma edge det.", (LPVOID) 0);
    hud.GetComboBox(IDC_DETECTIONMODE)->AddItem(L"Color edge det.", (LPVOID) 1);
    hud.GetComboBox(IDC_DETECTIONMODE)->AddItem(L"Depth edge det.", (LPVOID) 2);

    hud.AddComboBox(IDC_VIEWMODE, 35, iY += 24, HUD_WIDTH, 22, 0, false);
    hud.GetComboBox(IDC_VIEWMODE)->AddItem(L"View image", (LPVOID) 0);
    hud.GetComboBox(IDC_VIEWMODE)->AddItem(L"View edges", (LPVOID) 1);
    hud.GetComboBox(IDC_VIEWMODE)->AddItem(L"View weights", (LPVOID) 2);

    hud.AddComboBox(IDC_INPUT, 35, iY += 24, HUD_WIDTH, 22, 0, false);
    buildInputComboBox();
    if (commandlineOptions.src != L"") {
        hud.GetComboBox(IDC_INPUT)->AddItem(commandlineOptions.src.c_str(), (LPVOID) -1);
        hud.GetComboBox(IDC_INPUT)->SetSelectedByData((LPVOID) -1);
    }

    hud.AddCheckBox(IDC_ANTIALIASING, L"SMAA Anti-Aliasing", 35, iY += 24, HUD_WIDTH, 22, true);
    hud.AddCheckBox(IDC_PROFILE, L"Profile", 35, iY += 24, HUD_WIDTH, 22, false);

    wstringstream s;
    s << L"Max Search Steps: " << commandlineOptions.distance;
    hud.AddStatic(IDC_MAXSEARCHSTEPS_LABEL, s.str().c_str(), 35, iY += 24, HUD_WIDTH, 22);
    hud.AddSlider(IDC_MAXSEARCHSTEPS, 35, iY += 24, HUD_WIDTH, 22, 0, 100, int(100.0f * commandlineOptions.distance / 98.0f));

    s = wstringstream();
    s << L"Threshold: " << commandlineOptions.threshold;
    hud.AddStatic(IDC_THRESHOLD_LABEL, s.str().c_str(), 35, iY += 24, HUD_WIDTH, 22);
    hud.AddSlider(IDC_THRESHOLD, 35, iY += 24, HUD_WIDTH, 22, 0, 100, int(100.0f * commandlineOptions.threshold / 0.5f));
}


void parseCommandLine() {
    // Cheap, hackish, but simple command-line parsing:
    //   Demo.exe <distance:int> <threshold:float> <file.png in:str> <file.png out:str>
    wstringstream s(GetCommandLineW());
    wstring executable;
    s >> executable;
    s >> commandlineOptions.distance;
    commandlineOptions.distance = max(min(commandlineOptions.distance, 16), 0);
    if (!s) return;
    s >> commandlineOptions.threshold;
    commandlineOptions.threshold = max(min(commandlineOptions.threshold, 0.5f), 0.0f);
    if (!s) return;
    s >> commandlineOptions.src;
    if (!s) return;
    s >> commandlineOptions.dst;
    if (!s) return;
}


INT WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // Enable run-time memory check for debug builds.
    #if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    #endif

    parseCommandLine();

    DXUTSetCallbackD3D10DeviceAcceptable(isDeviceAcceptable);
    DXUTSetCallbackD3D10DeviceCreated(onCreateDevice);
    DXUTSetCallbackD3D10DeviceDestroyed(onDestroyDevice);
    DXUTSetCallbackD3D10SwapChainResized(onResizedSwapChain);
    DXUTSetCallbackD3D10SwapChainReleasing(onReleasingSwapChain);
    DXUTSetCallbackD3D10FrameRender(onFrameRender);

    DXUTSetCallbackMsgProc(msgProc);
    DXUTSetCallbackKeyboard(keyboardProc);
    DXUTSetCallbackDeviceChanging(modifyDeviceSettings);

    initApp();

    if (FAILED(DXUTInit(true, true, L"-forcevsync:0")))
        return -1;

    DXUTSetCursorSettings(true, true);
    if (FAILED(DXUTCreateWindow(L"SMAA: Subpixel Morphological Antialiasing")))
        return -1;

    if (FAILED(DXUTCreateDevice(true, 1280, 720)))
        return -1;

    resizeWindow();

    /**
     * A note to myself: we hacked DXUT to not show the window by default.
     * See <WINDOW_FIX> in DXUT.h
     */
    if (commandlineOptions.dst == L"")
        ShowWindow(DXUTGetHWND(), SW_SHOW);

    DXUTMainLoop();

    return DXUTGetExitCode();
}
