#include "ui_layout.hpp"
#include <iostream>
#include <stdexcept>
using namespace sat::ui;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool intersects(const Rect& a,const Rect& b){return a.x<b.x+b.width&&a.x+a.width>b.x&&a.y<b.y+b.height&&a.y+a.height>b.y;}
int main(){try{
    int cases=0;
    for(int dpi:{96,120,144,192})for(int width:{358,420,558})for(int comboHeight:{34,36,40}){
        const int w=scale(width,dpi),h=scale(174,dpi);const auto prompt=promptFrame(w,h,dpi,scale(comboHeight,dpi));
        const Rect controls[]={prompt.both,prompt.result,prompt.source,prompt.history,prompt.input,prompt.status,prompt.execute,prompt.cancel};
        for(size_t i=0;i<std::size(controls);++i){const auto& r=controls[i];check(r.x>=0&&r.y>=0&&r.x+r.width<=w&&r.y+r.height<=h,"prompt control escapes its client");for(size_t j=0;j<i;++j)check(!intersects(r,controls[j]),"prompt controls overlap");}
        check(prompt.input.y-prompt.history.y-prompt.history.height>=scale(10,dpi),"prompt history/input spacing is too small");
    }
    const auto defaultSettingsFrame=settingsFrame(scale(850,96),scale(650,96),96);
    const auto defaultGeneralPage=settingsPage(0,scale(850,96)-scale(18,96),defaultSettingsFrame.viewport.height,96,0);
    check(defaultGeneralPage.content<=defaultSettingsFrame.viewport.height,"default settings page requires unnecessary scrolling");
    const auto defaultResultPage=settingsPage(2,scale(850,96),defaultSettingsFrame.viewport.height,96,0);
    check(defaultResultPage.content<=defaultSettingsFrame.viewport.height,"default result settings clipped toolbar-order controls or needed a tiny scrollbar");
    for(int dpi:{96,120,144,192})for(int width:{520,620,720,850})for(int height:{360,420,520,650}){
        const int w=scale(width,dpi),h=scale(height,dpi);const auto frame=settingsFrame(w,h,dpi);
        check(frame.viewport.y==frame.header.height&&frame.viewport.y+frame.viewport.height==frame.footer.y,"frame regions overlap");
        for(int tab=0;tab<5;++tab){int previous=-1;for(int scroll=0;scroll<=2200;scroll+=20){
            const auto page=settingsPage(tab,w-scale(18,dpi),frame.viewport.height,dpi,scroll),repeat=settingsPage(tab,w-scale(18,dpi),frame.viewport.height,dpi,scroll);
            check(page.scroll>=previous&&page.scroll<=std::max(0,page.content-frame.viewport.height),"scroll monotonicity/bounds failed");previous=page.scroll;
            check(page.items.size()==repeat.items.size(),"layout not deterministic");
            for(size_t i=0;i<page.items.size();++i){const auto& a=page.items[i];check(a.rect==repeat.items[i].rect,"layout not idempotent");check(a.rect.x>=0&&a.rect.x+a.rect.width<=w,"settings field exceeds width");
                for(size_t j=0;j<i;++j)check(!intersects(a.rect,page.items[j].rect),"settings controls overlap");}
            ++cases;
        }}
        for(bool editing:{false,true}){const auto main=mainFrame(w,h,dpi,editing);check(main.sourceSurface.x>=0&&main.sourceSurface.x+main.sourceSurface.width<=w&&main.sourceSurface.y+main.sourceSurface.height<main.toolbarY,"source surface escapes content area");check(main.source.x>=main.sourceSurface.x&&main.source.x+main.source.width<=main.sourceSurface.x+main.sourceSurface.width&&main.source.y>=main.sourceSurface.y&&main.source.y+main.source.height<=main.sourceSurface.y+main.sourceSurface.height,"source edit is outside its surface");check(main.resultSurface.x>=0&&main.resultSurface.x+main.resultSurface.width<=w,"result surface escapes content area");check(editing||(!intersects(main.sourceSurface,main.resultSurface)&&!intersects(main.source,main.result)),"main panes overlap");check(editing|| (main.result.x>=main.resultSurface.x&&main.result.x+main.result.width<=main.resultSurface.x+main.resultSurface.width&&main.result.y>=main.resultSurface.y&&main.result.y+main.result.height<=main.resultSurface.y+main.resultSurface.height),"result edit is outside its surface");check(main.toolbarY+scale(40,dpi)<main.statusY,"toolbar/status overlap");}
        const auto row=languageRow(w,dpi);const Rect fields[]={row.source,row.arrow,row.target,row.translate};
        for(size_t i=0;i<std::size(fields);++i){check(fields[i].x>=0&&fields[i].x+fields[i].width<=w,"language/Translate row exceeds width");for(size_t j=0;j<i;++j)check(!intersects(fields[i],fields[j]),"language/Translate fields overlap");}
    }
    std::cout<<"ui_layout: PASS "<<cases<<" geometry/scroll cases at 100/125/150/200 percent (simulated, not monitor acceptance)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
