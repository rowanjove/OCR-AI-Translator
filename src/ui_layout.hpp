#pragma once
#include <algorithm>
#include <vector>

namespace sat::ui {
struct Rect { int x{},y{},width{},height{}; bool operator==(const Rect&) const = default; };
inline int scale(int dip,int dpi) noexcept { return (dip*dpi+48)/96; }
struct SettingsFrame { Rect header,viewport,footer; };
inline SettingsFrame settingsFrame(int width,int height,int dpi) noexcept {
    const int header=std::min(height,scale(104,dpi));
    const int footer=std::min(std::max(0,height-header),scale(70,dpi));
    return {{0,0,width,header},{0,header,width,std::max(0,height-header-footer)},{0,height-footer,width,footer}};
}
inline int clampScroll(int requested,int content,int viewport) noexcept {return std::clamp(requested,0,std::max(0,content-viewport));}
struct Placement {int id{};Rect rect;};
struct SettingsPage {std::vector<Placement> items;int content{},scroll{};};
inline SettingsPage settingsPage(int tab,int width,int height,int dpi,int requestedDip){
    SettingsPage page;const int margin=scale(24,dpi),full=std::max(0,width-margin*2);int y=scale(24,dpi);
    auto put=[&](int id,int x,int top,int w,int h){page.items.push_back({id,{x,top,std::max(0,w),h}});};
    auto heading=[&](int id){put(id,margin,y,full,scale(28,dpi));y+=scale(44,dpi);};
    auto check=[&](int id){put(id+1000,margin,y,std::max(0,full-scale(54,dpi)),scale(24,dpi));put(id+2000,margin,y+scale(25,dpi),std::max(0,full-scale(54,dpi)),scale(20,dpi));put(id,margin+full-scale(40,dpi),y+scale(6,dpi),scale(40,dpi),scale(30,dpi));y+=scale(48,dpi);};
    auto row=[&](int id,int label){put(label,margin,y,full,scale(24,dpi));put(id+2000,margin,y+scale(25,dpi),full,scale(18,dpi));put(id,margin,y+scale(47,dpi),full,scale(36,dpi));y+=scale(88,dpi);};
    switch(tab){
    case 0:{heading(319);check(307);check(308);check(324);const int gap=scale(16,dpi),half=(full-gap)/2;
        put(400,margin,y,half,scale(24,dpi));put(2300,margin,y+scale(25,dpi),half,scale(18,dpi));put(300,margin,y+scale(47,dpi),half,scale(36,dpi));
        put(401,margin+half+gap,y,half,scale(24,dpi));put(2301,margin+half+gap,y+scale(25,dpi),half,scale(18,dpi));put(301,margin+half+gap,y+scale(47,dpi),half,scale(36,dpi));y+=scale(88,dpi);
        row(345,415);row(343,413);page.items.back().rect.width=std::max(0,full-scale(96,dpi));put(354,margin+full-scale(88,dpi),y-scale(41,dpi),scale(88,dpi),scale(36,dpi));break;}
    case 1:heading(320);check(309);check(310);check(311);break;
    case 2:
        heading(321);for(int id:{312,313,325,326,327,328})check(id);row(340,412);
        page.items.back().rect.width=std::max(0,full-scale(180,dpi));
        put(341,margin+full-scale(172,dpi),y-scale(41,dpi),scale(80,dpi),scale(36,dpi));
        put(342,margin+full-scale(82,dpi),y-scale(41,dpi),scale(82,dpi),scale(36,dpi));break;
    case 3:
        heading(322);row(329,407);page.items.back().rect.width=std::max(0,full-scale(164,dpi));put(332,margin+full-scale(156,dpi),y-scale(41,dpi),scale(72,dpi),scale(36,dpi));put(333,margin+full-scale(76,dpi),y-scale(41,dpi),scale(76,dpi),scale(36,dpi));
        row(335,410);row(302,402);row(303,403);row(344,414);row(304,404);check(330);check(331);check(336);
        if(full>=scale(650,dpi)){
            const int gap=scale(16,dpi),half=(full-gap)/2;
            for(int col=0;col<2;++col){const int x=margin+col*(half+gap),id=305+col;put(405+col,x,y,half,scale(24,dpi));put(id+2000,x,y+scale(25,dpi),half,scale(18,dpi));put(id,x,y+scale(47,dpi),half,scale(36,dpi));}y+=scale(88,dpi);
        }else{row(305,405);row(306,406);}
        put(317,margin,y,scale(148,dpi),scale(36,dpi));y+=scale(60,dpi);break;
    case 4:heading(323);row(338,411);check(337);check(339);break;
    default:break;
    }
    page.content=y;page.scroll=clampScroll(scale(std::max(0,requestedDip),dpi),page.content,height);
    for(auto& item:page.items)item.rect.y-=page.scroll;
    return page;
}
struct MainFrame { Rect sourceSurface,resultSurface,source,result;int captionY{},toolbarY{},statusY{}; };
struct PromptFrame {Rect both,result,source,history,input,status,execute,cancel;};
inline PromptFrame promptFrame(int width,int height,int dpi,int closedComboHeight=0) noexcept {
    const int margin=scale(8,dpi),gap=scale(10,dpi),row=std::max(scale(36,dpi),closedComboHeight),scopeHeight=scale(26,dpi),scopeY=margin+(row-scopeHeight)/2;
    const int historyX=margin+scale(236,dpi),inputY=margin+row+gap,buttonHeight=scale(30,dpi),buttonY=height-margin-buttonHeight;
    return {{margin,scopeY,scale(84,dpi),scopeHeight},
            {margin+scale(88,dpi),scopeY,scale(68,dpi),scopeHeight},
            {margin+scale(160,dpi),scopeY,scale(68,dpi),scopeHeight},
            {historyX,margin,std::max(0,width-margin-historyX),row},
            {margin,inputY,std::max(0,width-margin*2),std::max(0,buttonY-gap-inputY)},
            {margin,buttonY,std::max(0,width-margin*2-scale(150,dpi)),buttonHeight},
            {width-margin-scale(142,dpi),buttonY,scale(68,dpi),buttonHeight},
            {width-margin-scale(68,dpi),buttonY,scale(68,dpi),buttonHeight}};
}
struct LanguageRow {Rect source,arrow,target,translate;};
inline LanguageRow languageRow(int width,int dpi) noexcept {
    const int margin=scale(24,dpi),targetX=margin+scale(124,dpi),gap=scale(10,dpi),button=scale(76,dpi);
    const int targetWidth=std::max(0,std::min(scale(174,dpi),width-margin-targetX-gap-button));
    return {{margin,scale(64,dpi),scale(92,dpi),scale(30,dpi)},
            {margin+scale(94,dpi),scale(64,dpi),scale(22,dpi),scale(30,dpi)},
            {targetX,scale(60,dpi),targetWidth,scale(34,dpi)},
            {targetX+targetWidth+gap,scale(60,dpi),button,scale(34,dpi)}};
}
inline MainFrame mainFrame(int width,int height,int dpi,bool editing) noexcept {
    const int margin=scale(24,dpi),gap=editing?0:scale(16,dpi),bottom=std::max(scale(138,dpi),height-scale(84,dpi));
    const int available=std::max(0,width-2*margin-gap),left=editing?available:available/2;
    const int caption=scale(110,dpi),surfaceTop=caption-scale(12,dpi),surfaceBottom=bottom-scale(4,dpi),inset=scale(14,dpi),textTop=caption+scale(36,dpi);
    const Rect sourceSurface{margin,surfaceTop,left,std::max(0,surfaceBottom-surfaceTop)};
    const Rect resultSurface{margin+left+gap,surfaceTop,editing?0:available-left,sourceSurface.height};
    const Rect source{sourceSurface.x+inset,textTop,std::max(0,sourceSurface.width-inset*2),std::max(0,surfaceBottom-textTop-scale(8,dpi))};
    const Rect result{resultSurface.x+inset,textTop,std::max(0,resultSurface.width-inset*2),editing?0:source.height};
    return {sourceSurface,resultSurface,source,result,caption,bottom+scale(16,dpi),height-scale(24,dpi)};
}
}
