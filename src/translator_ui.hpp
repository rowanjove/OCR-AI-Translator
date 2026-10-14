#pragma once

namespace sat {
inline unsigned resolveUiTheme(unsigned requested,bool systemDark,bool highContrast) noexcept {
    if(highContrast)return 0; // Native system colors take precedence for accessibility.
    return requested==0?(systemDark?2u:1u):requested;
}
inline bool deferImeKey(unsigned message,unsigned key,bool composing) noexcept {
    return composing&&(message==0x0100||message==0x0101||message==0x0102)&&
        (key==0x0d||key==0x1b||key==0x0a);
}
enum class MainAccelerator { none, retranslate, submitEdit, paste, copyResult };

inline MainAccelerator mapMainAccelerator(unsigned key,bool control,bool shift,bool editing,bool mainWindowFocused) noexcept {
    if(!control||!mainWindowFocused)return MainAccelerator::none;
    if(key=='R'&&!shift&&!editing)return MainAccelerator::retranslate;
    if(key==0x0d&&!shift&&editing)return MainAccelerator::submitEdit;
    if(key=='V'&&!shift&&!editing)return MainAccelerator::paste;
    if(key=='C'&&shift)return MainAccelerator::copyResult;
    return MainAccelerator::none;
}
}
