#pragma once
#include <windows.h>
#include <string>

namespace sat::ui::typography {
enum class Role { body, title, heading, note };
enum class Ink { primary, secondary };
struct Style { int sizeDip; int weight; int lineDip; Ink ink; };
constexpr Style style(Role role) noexcept {
    switch(role){
    case Role::title:return {20,FW_SEMIBOLD,28,Ink::primary};
    case Role::heading:return {16,FW_SEMIBOLD,24,Ink::primary};
    case Role::note:return {13,FW_NORMAL,20,Ink::secondary};
    default:return {14,FW_NORMAL,20,Ink::primary};
    }
}
inline int CALLBACK foundFamily(const LOGFONTW*,const TEXTMETRICW*,DWORD,LPARAM result){*reinterpret_cast<bool*>(result)=true;return 0;}
inline const std::wstring& family(){
    static const std::wstring chosen=[] {
        HDC dc=GetDC(nullptr);
        for(const auto* name:{L"Microsoft YaHei UI",L"Microsoft YaHei",L"Noto Sans CJK SC"}){
            LOGFONTW query{};query.lfCharSet=DEFAULT_CHARSET;lstrcpynW(query.lfFaceName,name,LF_FACESIZE);bool found=false;
            if(dc)EnumFontFamiliesExW(dc,&query,foundFamily,reinterpret_cast<LPARAM>(&found),0);
            if(found){ReleaseDC(nullptr,dc);return std::wstring(name);}
        }
        if(dc)ReleaseDC(nullptr,dc);
        NONCLIENTMETRICSW metrics{sizeof(metrics)};
        if(SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0))return std::wstring(metrics.lfMessageFont.lfFaceName);
        return std::wstring(L"Segoe UI");
    }();return chosen;
}
inline HFONT create(Role role,UINT dpi){
    const auto spec=style(role);
    // One chosen family for every peer; native GDI/RichEdit font linking handles
    // missing CJK/emoji glyphs without replacing text, caret or IME rendering.
    return CreateFontW(-MulDiv(spec.sizeDip,static_cast<int>(dpi),96),0,0,0,spec.weight,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,family().c_str());
}
inline COLORREF color(Ink ink,COLORREF primary,COLORREF secondary) noexcept {return ink==Ink::secondary?secondary:primary;}
}
