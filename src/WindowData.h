#pragma once

#include "inkpages/Log.h"

namespace inkpages::detail
{
    inline bool SetWindowData(HWND window, void* owner) noexcept
    {
        SetLastError(ERROR_SUCCESS);
        if (SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner))) return true;
        DWORD error = GetLastError();
        if (error == ERROR_SUCCESS) return true;
        try
        {
            LogHr("SetWindowLongPtr (window owner)", HRESULT_FROM_WIN32(error));
        }
        catch (...)
        {
            OutputDebugStringW(L"SetWindowLongPtr failed while setting the window owner.\n");
        }
        SetLastError(error);
        return false;
    }
}
