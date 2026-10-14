#pragma once
#include <windows.h>
#include <d2d1.h>
#include <wrl/client.h>
#include <algorithm>
#include <map>

namespace sat::ui {
using Microsoft::WRL::ComPtr;
// Shared UI-thread target; normalized paths are created once and retained.
class VectorPainter {
    ComPtr<ID2D1Factory> factory_;
    ComPtr<ID2D1DCRenderTarget> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<ID2D1StrokeStyle> stroke_;
    std::map<int,ComPtr<ID2D1PathGeometry>> icons_;
    bool initialize(){
        if(!factory_){
            if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory_.GetAddressOf())))return false;
            auto props=D2D1::StrokeStyleProperties();
            props.startCap=props.endCap=props.dashCap=D2D1_CAP_STYLE_ROUND;props.lineJoin=D2D1_LINE_JOIN_ROUND;
            if(FAILED(factory_->CreateStrokeStyle(props,nullptr,0,stroke_.GetAddressOf()))){factory_.Reset();return false;}
        }
        if(!target_){
            const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE));
            if(FAILED(factory_->CreateDCRenderTarget(&props,target_.GetAddressOf())))return false;
            if(FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(0),brush_.ReleaseAndGetAddressOf()))){target_.Reset();return false;}
        }
        return true;
    }
public:
    bool begin(HDC dc,const RECT& rect,int dpi){
        if(!initialize()||rect.right<=rect.left||rect.bottom<=rect.top)return false;
        target_->SetDpi(static_cast<float>(dpi),static_cast<float>(dpi));
        if(FAILED(target_->BindDC(dc,&rect)))return false;
        target_->BeginDraw();target_->SetTransform(D2D1::Matrix3x2F::Identity());
        target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);return true;
    }
    void color(COLORREF c){brush_->SetColor(D2D1::ColorF(GetRValue(c)/255.f,GetGValue(c)/255.f,GetBValue(c)/255.f));}
    void clear(COLORREF c){target_->Clear(D2D1::ColorF(GetRValue(c)/255.f,GetGValue(c)/255.f,GetBValue(c)/255.f));}
    void round(float x,float y,float w,float h,float radius,COLORREF fill,COLORREF border,float weight=1.f){
        const auto r=D2D1::RoundedRect(D2D1::RectF(x,y,x+w,y+h),radius,radius);
        color(fill);target_->FillRoundedRectangle(r,brush_.Get());
        if(weight>0){color(border);target_->DrawRoundedRectangle(r,brush_.Get(),weight,stroke_.Get());}
    }
    void ring(float x,float y,float w,float h,float radius,COLORREF c,float weight=1.5f){
        color(c);target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x,y,x+w,y+h),radius,radius),brush_.Get(),weight,stroke_.Get());
    }
    void dot(float x,float y,float radius,COLORREF c){color(c);target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x,y),radius,radius),brush_.Get());}
    bool end(){const HRESULT hr=target_->EndDraw();if(FAILED(hr)){brush_.Reset();target_.Reset();}return SUCCEEDED(hr);}
    bool icon(int id,float width,float height,COLORREF c){
        if(id==353)id=125;if(id==352)id=1000;
        if(id==104||id==126)id=103;if(id==125)id=107;
        auto it=icons_.find(id);
        if(it==icons_.end()){
            ComPtr<ID2D1PathGeometry> path;ComPtr<ID2D1GeometrySink> sink;
            if(FAILED(factory_->CreatePathGeometry(path.GetAddressOf()))||FAILED(path->Open(sink.GetAddressOf())))return false;
            auto start=[&](float x,float y){sink->BeginFigure(D2D1::Point2F(x,y),D2D1_FIGURE_BEGIN_HOLLOW);};
            auto to=[&](float x,float y){sink->AddLine(D2D1::Point2F(x,y));};
            auto curve=[&](float a,float b,float d,float e,float x,float y){sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(a,b),D2D1::Point2F(d,e),D2D1::Point2F(x,y)));};
            auto finish=[&](bool close=false){sink->EndFigure(close?D2D1_FIGURE_END_CLOSED:D2D1_FIGURE_END_OPEN);};
            auto line=[&](float x,float y,float a,float b){start(x,y);to(a,b);finish();};
            auto box=[&](float x,float y,float r,float b,float radius){start(x+radius,y);to(r-radius,y);curve(r,y,r,y,r,y+radius);to(r,b-radius);curve(r,b,r,b,r-radius,b);to(x+radius,b);curve(x,b,x,b,x,b-radius);to(x,y+radius);curve(x,y,x,y,x+radius,y);finish(true);};
            auto circle=[&](float x,float y,float r){constexpr float k=.55228475f;start(x+r,y);curve(x+r,y+r*k,x+r*k,y+r,x,y+r);curve(x-r*k,y+r,x-r,y+r*k,x-r,y);curve(x-r,y-r*k,x-r*k,y-r,x,y-r);curve(x+r*k,y-r,x+r,y-r*k,x+r,y);finish(true);};
            bool known=true;
            switch(id){
            case 103:box(3.5f,8,15.5f,21,2);start(8.5f,6);to(8.5f,5);curve(8.5f,3,8.5f,3,10.5f,3);to(18.5f,3);curve(20.5f,3,20.5f,3,20.5f,5);to(20.5f,14);curve(20.5f,16,20.5f,16,18.5f,16);to(18,16);finish();break;
            case 105:box(5,10,19,21,2);start(9,10);to(9,7);curve(9,1,18,1,18,7);finish();break;
            case 106:start(20,9);curve(18,2,8,1,4,9);curve(0,18,12,24,19,17);finish();start(4,4);to(4,10);to(10,10);finish();break;
            case 108:start(19,8);curve(16,3,9,2,5,7);curve(1,12,4,20,11,21);curve(15,22,19,19,20,15);finish();start(19,3);to(19,8);to(14,8);finish();break;
            case 107:line(6,6,18,18);line(18,6,6,18);break;
            case 111:box(6,6,18,18,2);break;
            case 117:start(4,6);to(8,6);to(9.5f,3.5f);to(14.5f,3.5f);to(16,6);to(20,6);curve(21,6,21,7,21,8);to(21,19);curve(21,20,20,21,19,21);to(5,21);curve(4,21,3,20,3,19);to(3,8);curve(3,7,3,6,4,6);finish(true);circle(12,13,4);break;
            case 118:start(4,20);to(7,20);to(19,8);to(16,5);to(4,17);finish(true);line(13,8,16,11);line(6,3,6,7);line(4,5,8,5);line(19,14,19,18);line(17,16,21,16);break;
            case 119:start(4,19);curve(7,17,8,14,9,10);curve(10,14,11,17,14,19);curve(11,20,7,20,4,19);finish(true);line(17,3,17,9);line(14,6,20,6);line(19,14,19,18);line(17,16,21,16);break;
            case 120:start(4,20);to(6,14);to(17,3);curve(18,2,22,6,21,7);to(10,18);finish(true);line(14,6,18,10);line(6,14,10,18);break;
            case 121:circle(12,12,9);start(12,6);to(12,12);to(17,15);finish();break;
            case 122:circle(5,12,1);circle(12,12,1);circle(19,12,1);break;
            case 109:start(9,3);to(15,3);to(16,6);to(19,7);to(21,10);to(19,12);to(21,14);to(19,17);to(16,18);to(15,21);to(9,21);to(8,18);to(5,17);to(3,14);to(5,12);to(3,10);to(5,7);to(8,6);finish(true);circle(12,12,3.5f);break;
            case 123:start(10,3);to(20,13);to(16,14);to(13,19);to(5,11);to(10,8);finish(true);line(9,15,3,21);break;
            case 124:line(5,12,19,12);break;
            case 1000:circle(12,12,9);start(9,8);curve(9,4,17,5,15,10);curve(14,12,12,11,12,14);finish();circle(12,17,.4f);break;
            case 1001:start(8,10);to(12,14);to(16,10);finish();break;
            default:known=false;break;
            }
            if(FAILED(sink->Close())||!known)return false;
            it=icons_.emplace(id,std::move(path)).first;
        }
        constexpr float size=20.f,ratio=size/24.f;
        target_->SetTransform(D2D1::Matrix3x2F::Scale(ratio,ratio)*D2D1::Matrix3x2F::Translation((width-size)/2.f,(height-size)/2.f));
        color(c);target_->DrawGeometry(it->second.Get(),brush_.Get(),1.8f,stroke_.Get());
        target_->SetTransform(D2D1::Matrix3x2F::Identity());return true;
    }
};
inline VectorPainter& vectorPainter(){static thread_local VectorPainter painter;return painter;}
inline bool drawIcon(HDC dc,const RECT& rect,int id,COLORREF color,int dpi,COLORREF background){
    auto& p=vectorPainter();if(!p.begin(dc,rect,dpi))return false;
    p.clear(background);
    const bool known=p.icon(id,(rect.right-rect.left)*96.f/dpi,(rect.bottom-rect.top)*96.f/dpi,color);
    return p.end()&&known;
}
}
