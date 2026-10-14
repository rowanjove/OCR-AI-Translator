#pragma once
#include <windows.h>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace sat::json {
struct Value {
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value, std::less<>>;
    using Data = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;
    Data data = nullptr;
    Value() = default;
    Value(std::nullptr_t) : data(nullptr) {}
    Value(bool v) : data(v) {}
    Value(double v) : data(v) {}
    Value(std::string v) : data(std::move(v)) {}
    Value(const char* v) : data(std::string(v)) {}
    Value(Array v) : data(std::move(v)) {}
    Value(Object v) : data(std::move(v)) {}
    bool isNull() const { return std::holds_alternative<std::nullptr_t>(data); }
    const Object* object() const { return std::get_if<Object>(&data); }
    const Array* array() const { return std::get_if<Array>(&data); }
    const std::string* string() const { return std::get_if<std::string>(&data); }
    const double* number() const { return std::get_if<double>(&data); }
    const bool* boolean() const { return std::get_if<bool>(&data); }
    const Value* get(std::string_view k) const {
        const auto* o = object(); if (!o) return nullptr;
        const auto it = o->find(k); return it == o->end() ? nullptr : &it->second;
    }
};

class Parser {
    std::string_view in;
    size_t p = 0, nodes = 0;
    static constexpr size_t maxNodes = 100000;
    void ws() { while (p < in.size() && (in[p]==' ' || in[p]=='\t' || in[p]=='\r' || in[p]=='\n')) ++p; }
    [[noreturn]] void fail() const { throw std::runtime_error("invalid JSON at byte " + std::to_string(p)); }
    bool take(char c) { ws(); if (p < in.size() && in[p] == c) { ++p; return true; } return false; }
    uint32_t hex4() {
        if (p + 4 > in.size()) fail(); uint32_t v = 0;
        for (int i=0; i<4; ++i) { const char c=in[p++]; v <<= 4;
            if(c>='0'&&c<='9') v|=c-'0'; else if(c>='a'&&c<='f') v|=c-'a'+10; else if(c>='A'&&c<='F') v|=c-'A'+10; else fail(); }
        return v;
    }
    static void appendUtf8(std::string& s, uint32_t cp) {
        if(cp<=0x7f) s.push_back(static_cast<char>(cp));
        else if(cp<=0x7ff) { s.push_back(static_cast<char>(0xc0|(cp>>6))); s.push_back(static_cast<char>(0x80|(cp&63))); }
        else if(cp<=0xffff) { s.push_back(static_cast<char>(0xe0|(cp>>12))); s.push_back(static_cast<char>(0x80|((cp>>6)&63))); s.push_back(static_cast<char>(0x80|(cp&63))); }
        else { s.push_back(static_cast<char>(0xf0|(cp>>18))); s.push_back(static_cast<char>(0x80|((cp>>12)&63))); s.push_back(static_cast<char>(0x80|((cp>>6)&63))); s.push_back(static_cast<char>(0x80|(cp&63))); }
    }
    static bool validUtf8(std::string_view s) {
        int32_t i=0; const int32_t n=static_cast<int32_t>(s.size());
        while(i<n) { const auto c=static_cast<uint8_t>(s[i++]); if(c<0x80) continue;
            uint32_t cp; int k; if(c>=0xc2&&c<=0xdf){cp=c&31;k=1;} else if(c>=0xe0&&c<=0xef){cp=c&15;k=2;} else if(c>=0xf0&&c<=0xf4){cp=c&7;k=3;} else return false;
            if(i+k>n) return false; for(int j=0;j<k;++j){const auto b=static_cast<uint8_t>(s[i++]); if((b&0xc0)!=0x80)return false; cp=(cp<<6)|(b&63);}
            if((k==1&&cp<0x80)||(k==2&&cp<0x800)||(k==3&&cp<0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)) return false;
        } return true;
    }
    std::string str() {
        ws(); if(p>=in.size()||in[p++]!='"') fail(); std::string out;
        while(p<in.size()) { const unsigned char c=static_cast<unsigned char>(in[p++]);
            if(c=='"') { if(!validUtf8(out)) fail(); return out; }
            if(c<0x20) fail();
            if(c!='\\') { out.push_back(static_cast<char>(c)); continue; }
            if(p>=in.size()) fail(); const char e=in[p++];
            switch(e) {
            case '"': out+='"'; break; case '\\': out+='\\'; break; case '/': out+='/'; break;
            case 'b': out+='\b'; break; case 'f': out+='\f'; break; case 'n': out+='\n'; break; case 'r': out+='\r'; break; case 't': out+='\t'; break;
            case 'u': { uint32_t cp=hex4(); if(cp>=0xd800&&cp<=0xdbff) { if(p+2>in.size()||in[p++]!='\\'||in[p++]!='u') fail(); const uint32_t low=hex4(); if(low<0xdc00||low>0xdfff) fail(); cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00); }
                else if(cp>=0xdc00&&cp<=0xdfff) fail(); appendUtf8(out,cp); break; }
            default: fail();
            }
            if(out.size()>1024*1024) fail();
        } fail();
    }
    Value value(unsigned depth) {
        ws(); if(depth>32||++nodes>maxNodes||p>=in.size()) fail();
        if(in[p]=='"') return str();
        if(in[p]=='{') { ++p; Value::Object o; if(take('}'))return o;
            while(true){ auto k=str(); if(!take(':'))fail(); auto v=value(depth+1); if(!o.emplace(std::move(k),std::move(v)).second)fail(); if(take('}'))return o; if(!take(','))fail(); }
        }
        if(in[p]=='[') { ++p; Value::Array a; if(take(']'))return a;
            while(true){ a.push_back(value(depth+1)); if(take(']'))return a; if(!take(','))fail(); }
        }
        if(in.substr(p,4)=="true"){p+=4;return true;} if(in.substr(p,5)=="false"){p+=5;return false;} if(in.substr(p,4)=="null"){p+=4;return nullptr;}
        const size_t start=p; if(in[p]=='-')++p; if(p>=in.size())fail(); if(in[p]=='0')++p; else {if(in[p]<'1'||in[p]>'9')fail();while(p<in.size()&&in[p]>='0'&&in[p]<='9')++p;}
        if(p<in.size()&&in[p]=='.'){++p;const auto d=p;while(p<in.size()&&in[p]>='0'&&in[p]<='9')++p;if(p==d)fail();}
        if(p<in.size()&&(in[p]=='e'||in[p]=='E')){++p;if(p<in.size()&&(in[p]=='+'||in[p]=='-'))++p;const auto d=p;while(p<in.size()&&in[p]>='0'&&in[p]<='9')++p;if(p==d)fail();}
        double d{}; const auto [end,ec]=std::from_chars(in.data()+start,in.data()+p,d); if(ec!=std::errc{}||end!=in.data()+p||!std::isfinite(d))fail(); return d;
    }
public:
    explicit Parser(std::string_view s):in(s) { if(s.size()>2*1024*1024) throw std::runtime_error("JSON exceeds size limit"); }
    Value parse() { auto v=value(0); ws(); if(p!=in.size())fail(); return v; }
};
inline std::string escape(std::string_view s) {
    static constexpr char hex[]="0123456789abcdef"; std::string o="\"";
    for(unsigned char c:s){switch(c){case '"':o+="\\\"";break;case '\\':o+="\\\\";break;case '\b':o+="\\b";break;case '\f':o+="\\f";break;case '\n':o+="\\n";break;case '\r':o+="\\r";break;case '\t':o+="\\t";break;default:if(c<0x20){o+="\\u00";o+=hex[c>>4];o+=hex[c&15];}else o+=static_cast<char>(c);}}
    return o+'"';
}
inline std::string stringify(const Value& v) {
    if(v.isNull())return "null"; if(const auto*b=v.boolean())return *b?"true":"false";
    if(const auto*n=v.number()){char b[64];const auto [p,e]=std::to_chars(b,b+sizeof(b),*n);if(e!=std::errc{})throw std::runtime_error("JSON number format failed");return {b,p};}
    if(const auto*s=v.string())return escape(*s);
    if(const auto*a=v.array()){std::string o="[";for(size_t i=0;i<a->size();++i){if(i)o+=',';o+=stringify((*a)[i]);}return o+']';}
    const auto&o=std::get<Value::Object>(v.data);std::string r="{";bool first=true;for(const auto&[k,x]:o){if(!first)r+=',';first=false;r+=escape(k)+":"+stringify(x);}return r+"}";
}
inline std::string encode(std::wstring_view w) {
    if(w.empty())return {}; const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),static_cast<int>(w.size()),nullptr,0,nullptr,nullptr);
    if(!n)throw std::runtime_error("invalid UTF-16");std::string s(static_cast<size_t>(n),'\0');if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),static_cast<int>(w.size()),s.data(),n,nullptr,nullptr))throw std::runtime_error("UTF-8 conversion failed");return s;
}
inline std::wstring decode(std::string_view s) {
    if(s.empty())return {}; if(s.size()>static_cast<size_t>(INT_MAX))throw std::runtime_error("UTF-8 input exceeds limit");
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(!n)throw std::runtime_error("input is not valid UTF-8");
    std::wstring w(static_cast<size_t>(n),L'\0');if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),w.data(),n))throw std::runtime_error("UTF-8 conversion failed");return w;
}
}
