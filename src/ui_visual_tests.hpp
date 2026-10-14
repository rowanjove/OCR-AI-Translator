// Included inside namespace sat, in the test executable only.
// These are actual renderer/EDIT tests; they do not emulate monitor DPI or IME.
inline void testFrameTypography(){
    auto require=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    openSettings();openHelp();
    require(fontOwner(uiItem(g->settings,settingModel))==g->settings&&fontOwner(uiItem(g->help,settingIntro))==g->help,"child HWND resolved to the wrong font cache");
    const auto previous=g->previewTheme;
    size_t controls=0;
    auto checkFont=[&](HWND child,HFONT expected){
        wchar_t cls[64]{};GetClassNameW(child,cls,64);
        if(_wcsicmp(cls,MSFTEDIT_CLASS)==0){CHARFORMAT2W format{};format.cbSize=sizeof(format);SendMessageW(child,EM_GETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&format));require(_wcsicmp(format.szFaceName,ui::typography::family().c_str())==0,"RichEdit default face escaped typography");}
        else if(_wcsicmp(cls,L"STATIC")==0||_wcsicmp(cls,L"BUTTON")==0||_wcsicmp(cls,L"EDIT")==0||_wcsicmp(cls,L"COMBOBOX")==0){
            const auto actual=reinterpret_cast<HFONT>(SendMessageW(child,WM_GETFONT,0,0));LOGFONTW a{},b{};
            require(actual&&GetObjectW(actual,sizeof(a),&a)&&GetObjectW(expected,sizeof(b),&b),"native control is using an implicit system font");
            require(a.lfHeight==b.lfHeight&&a.lfWeight==b.lfWeight&&_wcsicmp(a.lfFaceName,b.lfFaceName)==0,"font role differs between initial creation and refresh");++controls;
        }
    };
    for(HWND root:{g->window,g->settings,g->help}){
        // Check initial assignments and the production refresh/rebind/delete path.
        for(int pass=0;pass<2;++pass){
            struct Context { HWND root;decltype(checkFont)* check; } context{root,&checkFont};
            EnumChildWindows(root,[](HWND child,LPARAM value)->BOOL{auto* c=reinterpret_cast<Context*>(value);(*c->check)(child,roleFont(fontSetFor(c->root),textRole(c->root,GetDlgCtrlID(child))));return TRUE;},reinterpret_cast<LPARAM>(&context));
            fontSetFor(root).dpi=0;refreshFonts(root);
            if(root==g->help){
                HWND text=uiItem(root,settingIntro);CHARRANGE selection{};SendMessageW(text,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));CHARRANGE first{0,2};SendMessageW(text,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&first));CHARFORMAT2W format{};format.cbSize=sizeof(format);SendMessageW(text,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
                require((format.dwEffects&CFE_BOLD)&&format.yHeight==ui::typography::style(ui::typography::Role::heading).sizeDip*15,"help heading lost typography during font rebuild");SendMessageW(text,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
            }
        }
        const auto style=GetWindowLongPtrW(root,GWL_STYLE),ex=GetWindowLongPtrW(root,GWL_EXSTYLE);
        require((style&WS_THICKFRAME)!=0&&(style&(WS_BORDER|WS_DLGFRAME))==0&&(ex&(WS_EX_CLIENTEDGE|WS_EX_DLGMODALFRAME|WS_EX_STATICEDGE))==0,"custom frame lost sizing or acquired an extra border");
        for(Theme theme:{Theme::dark,Theme::light}){
            g->previewTheme=theme;setTheme();
            for(int i=0;i<25;++i){SendMessageW(root,WM_NCACTIVATE,FALSE,0);SendMessageW(root,WM_NCPAINT,1,0);SendMessageW(root,WM_NCACTIVATE,TRUE,0);SendMessageW(root,WM_NCPAINT,1,0);}
            BOOL enabled=TRUE;const HRESULT hr=DwmGetWindowAttribute(root,DWMWA_NCRENDERING_ENABLED,&enabled,sizeof(enabled));require(FAILED(hr)||!enabled,"DWM regained nonclient frame ownership");
        }
        MONITORINFO monitor{sizeof(monitor)};require(GetMonitorInfoW(MonitorFromWindow(root,MONITOR_DEFAULTTONEAREST),&monitor)!=FALSE,"work area unavailable");MINMAXINFO limits{};SendMessageW(root,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));
        require(limits.ptMaxSize.x==monitor.rcWork.right-monitor.rcWork.left&&limits.ptMaxSize.y==monitor.rcWork.bottom-monitor.rcWork.top,"maximized client frame can cover the taskbar");
    }
    LOGFONTW tip{};require(GetObjectW(reinterpret_cast<HFONT>(SendMessageW(g->tooltip,WM_GETFONT,0,0)),sizeof(tip),&tip)!=0&&_wcsicmp(tip.lfFaceName,ui::typography::family().c_str())==0,"tooltip retained its default font");
    for(UINT dpi:{96u,120u,144u,192u})for(auto role:{ui::typography::Role::body,ui::typography::Role::title,ui::typography::Role::heading,ui::typography::Role::note}){
        HFONT font=ui::typography::create(role,dpi);LOGFONTW info{};require(font&&GetObjectW(font,sizeof(info),&info)!=0,"scaled font creation failed");const auto spec=ui::typography::style(role);require(info.lfHeight==-MulDiv(spec.sizeDip,static_cast<int>(dpi),96)&&info.lfWeight==spec.weight,"scaled typography metrics drifted");DeleteObject(font);
    }
    DestroyWindow(g->help);DestroyWindow(g->settings);g->previewTheme=previous;setTheme();
    std::ofstream out(g->config.folder/L"frame-typography-checks.json",std::ios::binary);out<<"{\"native_font_assignments\":"<<controls<<",\"frame_windows\":3,\"themes\":2,\"nc_activation_cycles_per_window_theme\":25,\"font_factory_dpi\":[96,120,144,192],\"child_font_owner\":true,\"font_rebuild\":true,\"work_area_limits\":true,\"physical_dpi_and_mouse\":\"UNVERIFIED\"}";
}
inline void testVisualHardening(){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    openSettings();HWND settings=g->settings;
    SendMessageW(settings,WM_COMMAND,MAKEWPARAM(settingTabApi,BN_CLICKED),0);
    for(int id:{settingProfileName,settingBaseUrl,settingModel,settingApiKey,settingTimeout,settingTokens}){
        HWND edit=uiItem(settings,id);RECT wr{},cr{};POINT origin{};GetWindowRect(edit,&wr);GetClientRect(edit,&cr);ClientToScreen(edit,&origin);
        const int top=origin.y-wr.top,bottom=wr.bottom-origin.y-cr.bottom;
        require(top>=2&&std::abs(top-bottom)<=1,"single-line native EDIT is not centered inside its window");
        const auto margins=SendMessageW(edit,EM_GETMARGINS,0,0);require(LOWORD(margins)==ui::scale(8,static_cast<int>(uiDpi(edit)))&&HIWORD(margins)==LOWORD(margins),"single-line EDIT padding drifted between initial styling and font refresh");
        HDC dc=GetDC(edit);const auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(edit,WM_GETFONT,0,0)));TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,old);ReleaseDC(edit,dc);
        require(cr.bottom>=metrics.tmHeight,"native EDIT clipped its font line or caret");
        const auto initial=getText(edit);const std::wstring sample=id==settingTimeout||id==settingTokens?L"1234":L"中文 English 🌏";
        SetWindowTextW(edit,sample.c_str());SendMessageW(edit,EM_SETSEL,0,static_cast<LPARAM>(-1));DWORD start=0,end=0;SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));require(start==0&&end==sample.size(),"native EDIT lost Unicode selection");
        SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"56"));require(getText(edit)==L"56","native EDIT replacement failed");
        if(id==settingApiKey){
            // Password undo is a Windows security behavior; compare with an
            // unstyled native password EDIT rather than assuming normal undo.
            HWND native=CreateWindowExW(0,L"EDIT",sample.c_str(),WS_CHILD|ES_PASSWORD|ES_AUTOHSCROLL,0,0,200,36,settings,nullptr,g->instance,nullptr);require(native!=nullptr,"native password comparison control unavailable");
            SendMessageW(native,EM_SETSEL,0,-1);SendMessageW(native,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"56"));SendMessageW(native,EM_UNDO,0,0);SendMessageW(edit,EM_UNDO,0,0);const bool parity=getText(native)==getText(edit);DestroyWindow(native);require(parity,"password EDIT changed native undo behavior");
        }else{require(SendMessageW(edit,EM_CANUNDO,0,0)!=0,"ordinary native EDIT lost its undo buffer");SendMessageW(edit,EM_UNDO,0,0);if(getText(edit)!=sample)throw std::runtime_error("native EDIT undo failed: control "+std::to_string(id));}
        SetWindowTextW(edit,initial.c_str());
    }
    HWND button=uiItem(settings,settingCancel);SendMessageW(button,BM_SETSTYLE,BS_DEFPUSHBUTTON,TRUE);require((GetWindowLongPtrW(button,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW,"dialog default state restored a system rectangle");
    DestroyWindow(settings);
    const auto previous=g->previewTheme;
    constexpr int ids[]={117,103,104,105,106,107,108,111,118,119,120,121,122,109,123,124,125,126};
    double paintMs=0;size_t images=0,aaCases=0;DWORD gdiBefore=0,gdiAfter=0;
    for(Theme theme:{Theme::dark,Theme::light})for(int dpi:{96,120,144,192}){
        g->previewTheme=theme;setTheme();
        const int cell=ui::scale(48,dpi),width=cell*static_cast<int>(std::size(ids)),height=cell*5;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr;HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);require(dc&&bitmap&&pixels,"visual atlas DIB allocation failed");const auto old=SelectObject(dc,bitmap);RECT all{0,0,width,height};FillRect(dc,&all,g->backgroundBrush);
        for(int row=0;row<5;++row)for(size_t col=0;col<std::size(ids);++col){
            HWND h=uiItem(g->window,ids[col]);const bool enabled=IsWindowEnabled(h)!=FALSE;EnableWindow(h,row!=4);RemovePropW(h,L"SAT.Hot");if(row==1)SetPropW(h,L"SAT.Hot",reinterpret_cast<HANDLE>(1));
            SendMessageW(h,WM_CHANGEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS),0);
            const int left=static_cast<int>(col)*cell,top=row*cell;
            const int inset=ui::scale(6,dpi);
            DRAWITEMSTRUCT item{ODT_BUTTON,static_cast<UINT>(ids[col]),0,ODA_DRAWENTIRE,static_cast<UINT>(row==2?ODS_SELECTED:row==3?ODS_FOCUS:row==4?ODS_DISABLED:0),h,dc,{left+inset,top+inset,left+cell-inset,top+cell-inset},0};
            drawActionButton(item,dpi);RemovePropW(h,L"SAT.Hot");EnableWindow(h,enabled);
            // Require fractional edge coverage in every normal icon at every DPI.
            if(row==0){GdiFlush();bool antialiased=false;size_t ink=0;const auto* data=static_cast<const DWORD*>(pixels);
                auto rgb=[](COLORREF color)->DWORD{return (GetRValue(color)<<16)|(GetGValue(color)<<8)|GetBValue(color);};
                const bool pane=ids[col]==103||ids[col]==104||ids[col]==105;const COLORREF surface=ids[col]==111?(theme==Theme::dark?RGB(145,57,61):RGB(190,53,59)):pane?g->paneBackgroundColor:g->backgroundColor;
                const DWORD foreground=rgb(ids[col]==111?RGB(255,255,255):g->foregroundColor),background=rgb(surface);const int extent=ui::scale(10,dpi),cx=left+cell/2,cy=top+cell/2;
                for(int y=cy-extent;y<cy+extent;++y)for(int x=cx-extent;x<cx+extent;++x){const DWORD value=data[y*width+x]&0xffffff;if(value!=background){++ink;if(value!=foreground)antialiased=true;}}
                require(ink>=3&&antialiased,"icon renderer lost visible ink or fractional antialias coverage");++aaCases;
            }
        }
        GdiFlush();BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+width*height*4;
        const auto name=std::wstring(L"icons-")+(theme==Theme::dark?L"dark-":L"light-")+std::to_wstring(dpi)+L".bmp";
        std::ofstream out(g->config.folder/name,std::ios::binary);out.write(reinterpret_cast<const char*>(&file),sizeof(file));out.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));out.write(static_cast<const char*>(pixels),static_cast<std::streamsize>(width)*height*4);require(out.good(),"visual atlas write failed");++images;
        if(dpi==96&&theme==Theme::light){
            DRAWITEMSTRUCT item{ODT_BUTTON,109,0,ODA_DRAWENTIRE,0,uiItem(g->window,109),dc,{6,6,42,42},0};drawActionButton(item,dpi);GdiFlush();std::vector<DWORD> normal(static_cast<size_t>(cell)*cell);auto* data=static_cast<DWORD*>(pixels);for(int y=0;y<cell;++y)std::copy_n(data+y*width,cell,normal.data()+y*cell);
            gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);const auto start=InputClock::now();
            for(int i=0;i<1000;++i){SendMessageW(item.hwndItem,WM_MOUSEMOVE,0,0);drawActionButton(item,dpi);SendMessageW(item.hwndItem,WM_MOUSELEAVE,0,0);item.itemState=ODS_SELECTED;drawActionButton(item,dpi);item.itemState=ODS_FOCUS;drawActionButton(item,dpi);item.itemState=0;drawActionButton(item,dpi);}
            paintMs=std::chrono::duration<double,std::milli>(InputClock::now()-start).count();GdiFlush();gdiAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
            for(int y=0;y<cell;++y)require(std::equal(normal.data()+y*cell,normal.data()+(y+1)*cell,data+y*width),"hover/pressed/focus repaint left stale pixels");require(gdiAfter==gdiBefore,"repeated Direct2D button paint leaked GDI objects");
        }
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    }
    g->previewTheme=previous;setTheme();
    std::ofstream evidence(g->config.folder/L"ui-visual-checks.json",std::ios::binary);
    evidence<<"{\"native_edit_checks\":6,\"antialiased_icon_cases\":"<<aaCases<<",\"state_atlases\":"<<images<<",\"paint_cycles\":1000,\"draw_calls\":4000,\"paint_ms\":"<<paintMs<<",\"gdi_before\":"<<gdiBefore<<",\"gdi_after\":"<<gdiAfter<<",\"normal_pixels_restored\":true,\"dpi\":\"offscreen actual Direct2D rasterization, not monitor acceptance\",\"human_ime\":\"UNVERIFIED\"}";
}
