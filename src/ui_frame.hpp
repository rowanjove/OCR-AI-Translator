#pragma once
#include <windows.h>
#include <dwmapi.h>

namespace sat::ui {
inline HRESULT useClientFrame(HWND window) noexcept {
    // These three windows paint their whole rectangular client area. Keep
    // WS_THICKFRAME for native sizing/snap, but give DWM no second frame to draw.
    const DWMNCRENDERINGPOLICY policy=DWMNCRP_DISABLED;
    return DwmSetWindowAttribute(window,DWMWA_NCRENDERING_POLICY,&policy,sizeof(policy));
}
inline LRESULT activateClientFrame(HWND window,WPARAM active) noexcept {
    // Preserve DefWindowProc's activation bookkeeping, suppress only its paint.
    return DefWindowProcW(window,WM_NCACTIVATE,active,-1);
}
}
