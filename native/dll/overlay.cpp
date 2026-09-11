#include "overlay.hpp"

#include "log.hpp"
#include "messages.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>

#include <array>
#include <format>

namespace oyr::overlay {
namespace {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

// IDXGISwapChain vtable slots: IUnknown (3), IDXGIObject (4), IDXGIDeviceSubObject (1), then Present.
constexpr size_t kPresentSlot = 8;
constexpr size_t kResizeBuffersSlot = 13;

PresentFn g_present = nullptr;
ResizeBuffersFn g_resize_buffers = nullptr;

// Only touched on the render thread.
IDXGISwapChain* g_swap_chain = nullptr;  // the first one that presented with messages; the game has one
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
ID3D11RenderTargetView* g_target = nullptr;
ImFont* g_font = nullptr;
bool g_failed = false;

void fail(std::string_view what, HRESULT result) {
    log::info("overlay disabled: {} failed ({:#x})", what, static_cast<uint32_t>(result));
    g_failed = true;
}

bool start(IDXGISwapChain* swap_chain) {
    if (HRESULT result = swap_chain->GetDevice(IID_PPV_ARGS(&g_device)); FAILED(result)) {
        fail("GetDevice", result);
        return false;
    }
    g_device->GetImmediateContext(&g_context);
    g_swap_chain = swap_chain;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    wchar_t windows[MAX_PATH];
    UINT length = GetWindowsDirectoryW(windows, MAX_PATH);
    std::wstring wide = std::wstring(windows, length) + L"\\Fonts\\segoeuib.ttf";
    // ImGui takes UTF-8 paths.
    std::string path(WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, path.data(), static_cast<int>(path.size()), nullptr, nullptr);
    if (GetFileAttributesW(wide.c_str()) != INVALID_FILE_ATTRIBUTES) {
        g_font = io.Fonts->AddFontFromFileTTF(path.c_str(), 32.0f);
    }
    if (!g_font) {
        g_font = io.Fonts->AddFontDefault();
    }
    if (!ImGui_ImplDX11_Init(g_device, g_context)) {
        fail("ImGui_ImplDX11_Init", E_FAIL);
        return false;
    }
    log::info("overlay started");
    return true;
}

void draw(IDXGISwapChain* swap_chain) {
    std::vector<messages::Shown> shown = messages::shown();
    if (shown.empty() || g_failed) {
        return;
    }
    if (!g_swap_chain && !start(swap_chain)) {
        return;
    }
    if (swap_chain != g_swap_chain) {
        return;
    }
    if (!g_target) {
        ID3D11Texture2D* back_buffer = nullptr;
        if (HRESULT result = swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)); FAILED(result)) {
            fail("GetBuffer", result);
            return;
        }
        HRESULT result = g_device->CreateRenderTargetView(back_buffer, nullptr, &g_target);
        back_buffer->Release();
        if (FAILED(result)) {
            fail("CreateRenderTargetView", result);
            return;
        }
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    swap_chain->GetDesc(&desc);
    const auto width = static_cast<float>(desc.BufferDesc.Width);
    const auto height = static_cast<float>(desc.BufferDesc.Height);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    io.DeltaTime = 1.0f / 60.0f;

    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    ImDrawList* list = ImGui::GetForegroundDrawList();
    const float size = height * 0.035f;
    const float wrap = width * 0.7f;
    const float outline = size * 0.06f;
    float y = height * 0.06f;
    for (const messages::Shown& message : shown) {
        const char* text = message.text.c_str();
        ImVec2 extent = g_font->CalcTextSizeA(size, FLT_MAX, wrap, text);
        ImVec2 at((width - extent.x) / 2, y);
        ImU32 shadow = IM_COL32(20, 12, 4, static_cast<int>(220 * message.alpha));
        for (ImVec2 offset : {ImVec2(-outline, 0), ImVec2(outline, 0), ImVec2(0, -outline), ImVec2(0, outline)}) {
            list->AddText(g_font, size, ImVec2(at.x + offset.x, at.y + offset.y), shadow, text, nullptr, wrap);
        }
        list->AddText(g_font, size, at, IM_COL32(255, 248, 230, static_cast<int>(255 * message.alpha)), text, nullptr, wrap);
        y += extent.y + size * 0.35f;
    }
    ImGui::Render();

    // Draw on the back buffer, then give the game back the render targets it had.
    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets{};
    ID3D11DepthStencilView* depth = nullptr;
    g_context->OMGetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), &depth);
    g_context->OMSetRenderTargets(1, &g_target, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_context->OMSetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), depth);
    for (ID3D11RenderTargetView* target : targets) {
        if (target) {
            target->Release();
        }
    }
    if (depth) {
        depth->Release();
    }
}

HRESULT STDMETHODCALLTYPE present(IDXGISwapChain* swap_chain, UINT sync_interval, UINT flags) {
    if (!(flags & DXGI_PRESENT_TEST)) {
        draw(swap_chain);
    }
    return g_present(swap_chain, sync_interval, flags);
}

HRESULT STDMETHODCALLTYPE resize_buffers(IDXGISwapChain* swap_chain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) {
    // The game cannot resize while a view of its back buffer is alive.
    if (swap_chain == g_swap_chain && g_target) {
        g_target->Release();
        g_target = nullptr;
    }
    return g_resize_buffers(swap_chain, count, width, height, format, flags);
}

template <class Fn>
void patch_slot(void** vtable, size_t slot, Fn hook, Fn& original) {
    DWORD protection = 0;
    VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &protection);
    original = reinterpret_cast<Fn>(vtable[slot]);
    vtable[slot] = reinterpret_cast<void*>(hook);
    VirtualProtect(&vtable[slot], sizeof(void*), protection, &protection);
}

}  // namespace

std::string install() {
    // A throwaway swap chain on a hidden window, only to find the vtable.
    WNDCLASSEXW window_class{sizeof(window_class)};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"open_yoku_rando_overlay_probe";
    RegisterClassExW(&window_class);
    HWND window = CreateWindowExW(0, window_class.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 16, 16, nullptr, nullptr,
                                  window_class.hInstance, nullptr);
    if (!window) {
        return std::format("CreateWindowExW failed ({})", GetLastError());
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 1;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    HRESULT result = E_FAIL;
    for (D3D_DRIVER_TYPE driver : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        result = D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc, &swap_chain,
                                               &device, nullptr, &context);
        if (SUCCEEDED(result)) {
            break;
        }
    }
    std::string error;
    if (SUCCEEDED(result)) {
        void** vtable = *reinterpret_cast<void***>(swap_chain);
        patch_slot(vtable, kPresentSlot, &present, g_present);
        patch_slot(vtable, kResizeBuffersSlot, &resize_buffers, g_resize_buffers);
        swap_chain->Release();
        context->Release();
        device->Release();
    } else {
        error = std::format("D3D11CreateDeviceAndSwapChain failed ({:#x})", static_cast<uint32_t>(result));
    }
    DestroyWindow(window);
    UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
    return error;
}

}  // namespace oyr::overlay
