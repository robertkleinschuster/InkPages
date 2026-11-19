// cursorprobe: where and what the system really draws as the mouse pointer, from DXGI Desktop Duplication
// (IDXGIOutputDuplication: PointerPosition + GetFramePointerShape) - screenshots (CopyFromScreen) leave the
// cursor out and GetCursorInfo only says which cursor is set.
//
// Usage: cursorprobe.exe <outdir> <seconds>
//   logs every pointer change to <outdir>\cursorprobe.log; whenever a file <outdir>\snap-<name>.req appears it
//   writes <outdir>\snap-<name>.bmp = the desktop with the pointer drawn where DWM draws it, and deletes the .req.

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

static FILE* g_log;
static void Log(const char* fmt, ...)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a;
    va_start(a, fmt);
    std::vfprintf(g_log, fmt, a);
    va_end(a);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}

struct Pointer
{
    bool visible = false;
    POINT pos{};
    DXGI_OUTDUPL_POINTER_SHAPE_INFO info{};
    std::vector<BYTE> shape;
};

// Blends the pointer shape into a BGRA frame (types as documented for DXGI_OUTDUPL_POINTER_SHAPE_TYPE).
static void DrawPointer(BYTE* frame, UINT pitch, UINT w, UINT h, Pointer const& p)
{
    if (!p.visible || p.shape.empty()) return;
    UINT sw = p.info.Width;
    UINT sh = p.info.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME ? p.info.Height / 2 : p.info.Height;
    for (UINT y = 0; y < sh; ++y)
        for (UINT x = 0; x < sw; ++x)
        {
            int fx = p.pos.x + int(x), fy = p.pos.y + int(y);
            if (fx < 0 || fy < 0 || fx >= int(w) || fy >= int(h)) continue;
            BYTE* d = frame + fy * pitch + fx * 4;
            if (p.info.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME)
            {
                BYTE andBit = (p.shape[y * p.info.Pitch + x / 8] >> (7 - x % 8)) & 1;
                BYTE xorBit = (p.shape[(y + sh) * p.info.Pitch + x / 8] >> (7 - x % 8)) & 1;
                for (int c = 0; c < 3; ++c) d[c] = BYTE((andBit ? d[c] : 0) ^ (xorBit ? 0xFF : 0));
            }
            else
            {
                BYTE const* s = &p.shape[y * p.info.Pitch + x * 4];
                if (p.info.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR)
                {
                    UINT a = s[3];
                    for (int c = 0; c < 3; ++c) d[c] = BYTE((s[c] * a + d[c] * (255 - a)) / 255);
                }
                else // masked color: alpha 0 = replace, 0xFF = xor
                {
                    if (s[3] == 0) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
                    else { d[0] ^= s[0]; d[1] ^= s[1]; d[2] ^= s[2]; }
                }
            }
        }
}

static void WriteBmp(std::wstring const& path, BYTE const* frame, UINT pitch, UINT w, UINT h)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") || !f) { Log("cannot write snapshot"); return; }
    BITMAPINFOHEADER bi{sizeof(bi), LONG(w), -LONG(h), 1, 32, BI_RGB};
    BITMAPFILEHEADER fh{0x4D42, DWORD(sizeof(fh) + sizeof(bi) + w * h * 4), 0, 0, DWORD(sizeof(fh) + sizeof(bi))};
    std::fwrite(&fh, sizeof(fh), 1, f);
    std::fwrite(&bi, sizeof(bi), 1, f);
    for (UINT y = 0; y < h; ++y) std::fwrite(frame + y * pitch, w * 4, 1, f);
    std::fclose(f);
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::wstring dir = argv[1];
    int seconds = _wtoi(argv[2]);
    _wfopen_s(&g_log, (dir + L"\\cursorprobe.log").c_str(), L"wb");
    if (!g_log) return 3;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
    Log("D3D11CreateDevice hr=0x%08X", hr);
    if (FAILED(hr)) return 4;
    ComPtr<IDXGIDevice> dxgiDevice;
    device.As(&dxgiDevice);
    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);
    ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(0, &output);
    Log("EnumOutputs hr=0x%08X", hr);
    if (FAILED(hr)) return 5;
    ComPtr<IDXGIOutput1> output1;
    output.As(&output1);
    ComPtr<IDXGIOutputDuplication> dup;
    DXGI_OUTDUPL_DESC desc{};
    // (Re)creates the duplication: DXGI_ERROR_ACCESS_LOST (desktop switch, mode change) invalidates it.
    auto duplicate = [&]() -> bool {
        dup.Reset();
        HRESULT dhr = output1->DuplicateOutput(device.Get(), &dup);
        Log("DuplicateOutput hr=0x%08X", dhr);
        if (FAILED(dhr)) return false;
        dup->GetDesc(&desc);
        Log("desktop %ux%u", desc.ModeDesc.Width, desc.ModeDesc.Height);
        return true;
    };
    if (!duplicate()) return 6;

    Pointer p;
    std::string last;
    ComPtr<ID3D11Texture2D> staging;
    ULONGLONG end = GetTickCount64() + ULONGLONG(seconds) * 1000;
    while (GetTickCount64() < end)
    {
        DXGI_OUTDUPL_FRAME_INFO fi{};
        ComPtr<IDXGIResource> resource;
        hr = dup->AcquireNextFrame(50, &fi, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) { /* no change: still check for snapshot requests below */ }
        else if (FAILED(hr))
        {
            Log("AcquireNextFrame hr=0x%08X", hr);
            while (!duplicate() && GetTickCount64() < end) Sleep(200);
            if (!dup) break;
            continue;
        }

        if (SUCCEEDED(hr))
        {
            if (fi.LastMouseUpdateTime.QuadPart)
            {
                p.visible = fi.PointerPosition.Visible != FALSE;
                p.pos = fi.PointerPosition.Position;
            }
            if (fi.PointerShapeBufferSize)
            {
                p.shape.resize(fi.PointerShapeBufferSize);
                UINT needed = 0;
                HRESULT shr = dup->GetFramePointerShape(UINT(p.shape.size()), p.shape.data(), &needed, &p.info);
                if (FAILED(shr)) Log("GetFramePointerShape hr=0x%08X", shr);
                // a fingerprint of the shape: type, size, hotspot, color in the middle
                UINT cx = p.info.Width / 2, cy = (p.info.Type == 1 ? p.info.Height / 4 : p.info.Height / 2);
                DWORD mid = p.info.Type == 1 ? 0 : *reinterpret_cast<DWORD const*>(&p.shape[cy * p.info.Pitch + cx * 4]);
                Log("shape: type=%u %ux%u hotspot=%d,%d center=0x%08X", p.info.Type, p.info.Width, p.info.Height,
                    p.info.HotSpot.x, p.info.HotSpot.y, mid);
            }
            char line[128];
            std::snprintf(line, sizeof(line), "pointer: %s at %ld,%ld", p.visible ? "visible" : "hidden", p.pos.x, p.pos.y);
            if (last != line) { Log("%s", line); last = line; }
            if (fi.LastPresentTime.QuadPart && resource) // keep the latest desktop image
            {
                ComPtr<ID3D11Texture2D> tex;
                resource.As(&tex);
                if (!staging)
                {
                    D3D11_TEXTURE2D_DESC td;
                    tex->GetDesc(&td);
                    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
                    hr = device->CreateTexture2D(&td, nullptr, &staging);
                    if (FAILED(hr)) Log("CreateTexture2D hr=0x%08X", hr);
                }
                if (staging) context->CopyResource(staging.Get(), tex.Get());
            }
            dup->ReleaseFrame();
        }

        WIN32_FIND_DATAW fd;
        HANDLE find = FindFirstFileW((dir + L"\\snap-*.req").c_str(), &fd);
        if (find != INVALID_HANDLE_VALUE)
        {
            FindClose(find);
            std::wstring req = dir + L"\\" + fd.cFileName;
            std::wstring name = std::wstring(fd.cFileName).substr(0, wcslen(fd.cFileName) - 4);
            if (staging)
            {
                D3D11_MAPPED_SUBRESOURCE m;
                if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m)))
                {
                    std::vector<BYTE> frame(static_cast<BYTE*>(m.pData), static_cast<BYTE*>(m.pData) + m.RowPitch * desc.ModeDesc.Height);
                    context->Unmap(staging.Get(), 0);
                    DrawPointer(frame.data(), m.RowPitch, desc.ModeDesc.Width, desc.ModeDesc.Height, p);
                    WriteBmp(dir + L"\\" + name + L".bmp", frame.data(), m.RowPitch, desc.ModeDesc.Width, desc.ModeDesc.Height);
                    Log("snapshot %ls: pointer %s at %ld,%ld", name.c_str(), p.visible ? "visible" : "hidden", p.pos.x, p.pos.y);
                    DeleteFileW(req.c_str());
                }
            }
        }
    }
    Log("end");
    return 0;
}
