#include <winsock2.h>
#include <windows.h>
#include <richedit.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "test_sandbox.hpp"
#include "json.hpp"

namespace {
HWND testItem(HWND parent,int id){HWND item=GetDlgItem(parent,id);if(!item){HWND viewport=GetDlgItem(parent,350);if(viewport)item=GetDlgItem(viewport,id);}return item;}
LRESULT testMessage(HWND parent,int id,UINT m,WPARAM wp,LPARAM lp){return SendMessageW(testItem(parent,id),m,wp,lp);}
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string utf8(std::wstring_view value){if(value.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);std::string out(static_cast<size_t>(n),'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),n,nullptr,nullptr);return out;}
bool setRemoteText(HWND parent,int id,const std::wstring& value){HWND control=testItem(parent,id);DWORD_PTR result=0;return control&&SendMessageTimeoutW(control,WM_SETTEXT,0,reinterpret_cast<LPARAM>(value.c_str()),SMTO_ABORTIFHUNG|SMTO_BLOCK,1000,&result)!=0&&result!=0;}
class HeldHttpServer {
    struct WinsockRuntime { WinsockRuntime(){WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data)!=0)throw std::runtime_error("process mock WSAStartup failed");}~WinsockRuntime(){WSACleanup();} } winsock_;
public:
    HeldHttpServer(){
        listener_=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener_==INVALID_SOCKET)throw std::runtime_error("process mock socket failed");
        try{sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=0;
            if(bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof(address))==SOCKET_ERROR||listen(listener_,1)==SOCKET_ERROR)throw std::runtime_error("process mock bind/listen failed");
            int length=sizeof(address);if(getsockname(listener_,reinterpret_cast<sockaddr*>(&address),&length)==SOCKET_ERROR)throw std::runtime_error("process mock port query failed");port_=ntohs(address.sin_port);worker_=std::thread([this]{serve();});
        }catch(...){closesocket(listener_);listener_=INVALID_SOCKET;throw;}
    }
    HeldHttpServer(const HeldHttpServer&)=delete;HeldHttpServer&operator=(const HeldHttpServer&)=delete;
    ~HeldHttpServer(){release();SOCKET wake=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(wake!=INVALID_SOCKET){sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(port_);(void)connect(wake,reinterpret_cast<sockaddr*>(&address),sizeof(address));closesocket(wake);}if(listener_!=INVALID_SOCKET){shutdown(listener_,SD_BOTH);closesocket(listener_);}if(worker_.joinable())worker_.join();}
    std::wstring url()const{return L"http://127.0.0.1:"+std::to_wstring(port_)+L"/v1";}
    bool waitForRequest(DWORD timeoutMs){std::unique_lock lock(mutex_);return changed_.wait_for(lock,std::chrono::milliseconds(timeoutMs),[this]{return requestSeen_;});}
    std::string request(){std::lock_guard lock(mutex_);return request_;}
    void release()noexcept{{std::lock_guard lock(mutex_);released_=true;if(client_!=INVALID_SOCKET)shutdown(client_,SD_BOTH);}changed_.notify_all();}
private:
    void serve()noexcept{
        const SOCKET accepted=accept(listener_,nullptr,nullptr);if(accepted==INVALID_SOCKET)return;
        {std::lock_guard lock(mutex_);if(released_){closesocket(accepted);return;}client_=accepted;}
        std::string request;char buffer[2048]{};bool complete=false;
        while(!complete){const int received=recv(accepted,buffer,static_cast<int>(sizeof(buffer)),0);if(received<=0)break;request.append(buffer,static_cast<size_t>(received));complete=request.find("\r\n\r\n")!=std::string::npos;}
        if(complete){const auto headers=request.substr(0,request.find("\r\n\r\n"));size_t lengthAt=headers.find("Content-Length:");if(lengthAt==std::string::npos)lengthAt=headers.find("content-length:");if(lengthAt==std::string::npos){std::lock_guard lock(mutex_);released_=true;changed_.notify_all();}else{lengthAt=headers.find(':',lengthAt)+1;while(lengthAt<headers.size()&&headers[lengthAt]==' ')++lengthAt;size_t bodyLength=0;try{bodyLength=static_cast<size_t>(std::stoull(headers.substr(lengthAt)));}catch(...){bodyLength=0;}const size_t headerEnd=request.find("\r\n\r\n")+4;while(request.size()<headerEnd+bodyLength){const int received=recv(accepted,buffer,static_cast<int>(sizeof(buffer)),0);if(received<=0)break;request.append(buffer,static_cast<size_t>(received));}std::unique_lock lock(mutex_);request_=std::move(request);requestSeen_=true;changed_.notify_all();changed_.wait(lock,[this]{return released_;});}}
        {std::lock_guard lock(mutex_);if(client_==accepted)client_=INVALID_SOCKET;}
        shutdown(accepted,SD_BOTH);closesocket(accepted);
    }
    SOCKET listener_=INVALID_SOCKET,client_=INVALID_SOCKET;unsigned short port_{};std::thread worker_;std::mutex mutex_;std::condition_variable changed_;bool requestSeen_=false,released_=false;std::string request_;
};
struct SseReply { unsigned status; std::string body; };
class SseResponseServer {
    struct WinsockRuntime { WinsockRuntime(){if(WSAStartup(MAKEWORD(2,2),&data_)!=0)throw std::runtime_error("response mock WSAStartup failed");}~WinsockRuntime(){WSACleanup();}WSADATA data_{}; } winsock_;
public:
    explicit SseResponseServer(std::vector<SseReply> replies):replies_(std::move(replies)){
        listener_=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener_==INVALID_SOCKET)throw std::runtime_error("response mock socket failed");
        try{sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=0;if(bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof(address))==SOCKET_ERROR||listen(listener_,4)==SOCKET_ERROR)throw std::runtime_error("response mock bind/listen failed");int length=sizeof(address);if(getsockname(listener_,reinterpret_cast<sockaddr*>(&address),&length)==SOCKET_ERROR)throw std::runtime_error("response mock port query failed");port_=ntohs(address.sin_port);worker_=std::thread([this]{serve();});}catch(...){closesocket(listener_);listener_=INVALID_SOCKET;throw;}
    }
    SseResponseServer(const SseResponseServer&)=delete;SseResponseServer&operator=(const SseResponseServer&)=delete;
    ~SseResponseServer(){stopping_.store(true);const SOCKET client=client_.load();if(client!=INVALID_SOCKET)shutdown(client,SD_BOTH);if(listener_!=INVALID_SOCKET){SOCKET wake=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(wake!=INVALID_SOCKET){sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(port_);(void)connect(wake,reinterpret_cast<sockaddr*>(&address),sizeof(address));closesocket(wake);}shutdown(listener_,SD_BOTH);closesocket(listener_);listener_=INVALID_SOCKET;}if(worker_.joinable())worker_.join();}
    std::wstring url()const{return L"http://127.0.0.1:"+std::to_wstring(port_)+L"/v1";}
    bool waitForRequests(size_t count,DWORD timeoutMs=5000){std::unique_lock lock(mutex_);return changed_.wait_for(lock,std::chrono::milliseconds(timeoutMs),[&]{return requests_.size()>=count;});}
    std::vector<std::string> requests(){std::lock_guard lock(mutex_);return requests_;}
private:
    static bool sendAll(SOCKET socket,const char* data,size_t size){while(size){const int sent=send(socket,data,static_cast<int>(std::min<size_t>(size,INT_MAX)),0);if(sent<=0)return false;data+=sent;size-=static_cast<size_t>(sent);}return true;}
    static bool readRequest(SOCKET socket,std::string& request)noexcept{try{char buffer[4096];size_t headerEnd=std::string::npos;while(headerEnd==std::string::npos){const int received=recv(socket,buffer,sizeof(buffer),0);if(received<=0)return false;request.append(buffer,static_cast<size_t>(received));if(request.size()>1024*1024)return false;headerEnd=request.find("\r\n\r\n");}const auto headers=request.substr(0,headerEnd);size_t pos=headers.find("Content-Length:");if(pos==std::string::npos)pos=headers.find("content-length:");if(pos==std::string::npos)return false;pos=headers.find(':',pos)+1;while(pos<headers.size()&&headers[pos]==' ')++pos;const size_t length=static_cast<size_t>(std::stoull(headers.substr(pos)));if(length>1024*1024||headerEnd+4>1024*1024-length)return false;const size_t total=headerEnd+4+length;while(request.size()<total){const int received=recv(socket,buffer,sizeof(buffer),0);if(received<=0)return false;request.append(buffer,static_cast<size_t>(received));}request.resize(total);return true;}catch(...){return false;}}
    void serve()noexcept{for(const auto& reply:replies_){if(stopping_.load())return;SOCKET client=accept(listener_,nullptr,nullptr);if(client==INVALID_SOCKET)return;client_.store(client);if(stopping_.load()){shutdown(client,SD_BOTH);closesocket(client);client_.store(INVALID_SOCKET);return;}std::string request;if(!readRequest(client,request)){shutdown(client,SD_BOTH);closesocket(client);client_.store(INVALID_SOCKET);return;}{std::lock_guard lock(mutex_);requests_.push_back(std::move(request));changed_.notify_all();}const bool success=reply.status>=200&&reply.status<300;const std::string body=success?"data: {\"choices\":[{\"delta\":{\"content\":\""+reply.body+"\"}}]}\n\ndata: [DONE]\n\n":reply.body;const std::string reason=reply.status==200?"OK":reply.status==503?"Service Unavailable":"Error";const std::string contentType=success?"text/event-stream; charset=utf-8":"application/json; charset=utf-8";const std::string header="HTTP/1.1 "+std::to_string(reply.status)+" "+reason+"\r\nContent-Type: "+contentType+"\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n";bool sent=sendAll(client,header.data(),header.size());for(size_t at=0;sent&&at<body.size();at+=3)sent=sendAll(client,body.data()+at,std::min<size_t>(3,body.size()-at));shutdown(client,SD_BOTH);closesocket(client);client_.store(INVALID_SOCKET);}}
    SOCKET listener_=INVALID_SOCKET;std::atomic<SOCKET>client_{INVALID_SOCKET};std::atomic<bool>stopping_{false};unsigned short port_{};std::vector<SseReply>replies_;std::thread worker_;std::mutex mutex_;std::condition_variable changed_;std::vector<std::string>requests_;
};
struct WindowList{DWORD pid;std::wstring found;};
BOOL CALLBACK collectWindows(HWND window,LPARAM data){auto*list=reinterpret_cast<WindowList*>(data);DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==list->pid){wchar_t cls[128]{},title[256]{};GetClassNameW(window,cls,128);GetWindowTextW(window,title,256);list->found+=L" ["+std::wstring(cls)+L":"+title+L"]";}return TRUE;}
struct DialogTextList{std::wstring found;};
BOOL CALLBACK collectDialogText(HWND window,LPARAM data){auto*list=reinterpret_cast<DialogTextList*>(data);wchar_t cls[128]{},title[512]{};GetClassNameW(window,cls,128);DWORD_PTR length=0;if(SendMessageTimeoutW(window,WM_GETTEXTLENGTH,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,500,&length)&&length>0&&length<std::size(title))(void)SendMessageTimeoutW(window,WM_GETTEXT,std::size(title),reinterpret_cast<LPARAM>(title),SMTO_ABORTIFHUNG|SMTO_BLOCK,500,&length);if(title[0])list->found+=L" ["+std::wstring(cls)+L":"+title+L"]";return TRUE;}
struct ProcessDialog{DWORD pid;HWND found{};};
BOOL CALLBACK findProcessDialog(HWND window,LPARAM data){auto*search=reinterpret_cast<ProcessDialog*>(data);DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==search->pid){wchar_t cls[128]{};GetClassNameW(window,cls,128);if(std::wstring(cls)==L"#32770"){search->found=window;return FALSE;}}return TRUE;}
struct Child {
    PROCESS_INFORMATION pi{};
    HWND window{};
    Child()=default;Child(const Child&)=delete;Child&operator=(const Child&)=delete;
    Child(Child&& other)noexcept:pi(other.pi),window(other.window){other.pi={};other.window=nullptr;}
    Child&operator=(Child&& other)noexcept{if(this!=&other){if(pi.hProcess){if(WaitForSingleObject(pi.hProcess,0)==WAIT_TIMEOUT){if(window)SendMessageW(window,WM_CLOSE,0,0);if(WaitForSingleObject(pi.hProcess,3000)==WAIT_TIMEOUT)TerminateProcess(pi.hProcess,3);}CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}pi=other.pi;window=other.window;other.pi={};other.window=nullptr;}return *this;}
    ~Child(){if(pi.hProcess){if(WaitForSingleObject(pi.hProcess,0)==WAIT_TIMEOUT){if(window)SendMessageW(window,WM_CLOSE,0,0);if(WaitForSingleObject(pi.hProcess,3000)==WAIT_TIMEOUT)TerminateProcess(pi.hProcess,3);}CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}}
    void start(const std::wstring& exe,const std::wstring& args,int startupShow=-1){STARTUPINFOW si{sizeof(si)};if(startupShow>=0){si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=static_cast<WORD>(startupShow);}std::wstring command=L"\""+exe+L"\""+(args.empty()?L"":L" "+args);std::vector<wchar_t> buffer(command.begin(),command.end());buffer.push_back(L'\0');check(CreateProcessW(exe.c_str(),buffer.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi)!=FALSE,"Translator process start failed");}
    void startCaptured(const std::wstring& exe,const std::wstring& args,const std::filesystem::path& path){
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&attributes,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);check(file!=INVALID_HANDLE_VALUE,"diagnostic output capture failed");
        STARTUPINFOW info{sizeof(info)};info.dwFlags=STARTF_USESTDHANDLES;info.hStdOutput=file;info.hStdError=file;info.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
        std::wstring command=L"\""+exe+L"\" "+args;const BOOL started=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&info,&pi);CloseHandle(file);check(started!=FALSE,"diagnostic process start failed");
    }
    void waitIdle(){(void)WaitForInputIdle(pi.hProcess,5000);const ULONGLONG deadline=GetTickCount64()+5000;while(GetTickCount64()<deadline){window=FindWindowW(L"SnipasteAITranslator.Test.Main.v1",nullptr);if(window)return;if(WaitForSingleObject(pi.hProcess,0)==WAIT_OBJECT_0)break;Sleep(10);}DWORD code=0;GetExitCodeProcess(pi.hProcess,&code);WindowList windows{pi.dwProcessId,{}};EnumWindows(collectWindows,reinterpret_cast<LPARAM>(&windows));DialogTextList dialog{};EnumChildWindows(FindWindowW(L"#32770",L"OCR AI Translator [Test]"),collectDialogText,reinterpret_cast<LPARAM>(&dialog));throw std::runtime_error("test main window did not become ready; child exit="+std::to_string(code)+" windows="+utf8(windows.found)+" dialog="+utf8(dialog.found));}
    void expectStartupError(const std::wstring& caseName){HWND dialog=nullptr;const ULONGLONG deadline=GetTickCount64()+5000;while(GetTickCount64()<deadline){ProcessDialog search{pi.dwProcessId};EnumWindows(findProcessDialog,reinterpret_cast<LPARAM>(&search));if(search.found){dialog=search.found;break;}if(WaitForSingleObject(pi.hProcess,0)==WAIT_OBJECT_0)break;Sleep(10);}if(!dialog)throw std::runtime_error("expected startup error dialog was not shown for "+utf8(caseName));PostMessageW(dialog,WM_CLOSE,0,0);bool exited=waitExit();if(!exited){PostMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDOK,BN_CLICKED),0);exited=waitExit(2000);}const DWORD code=exitCode();if(!exited||code!=2)throw std::runtime_error("invalid startup input did not exit with code 2 for "+utf8(caseName)+"; exited="+std::to_string(exited)+" code="+std::to_string(code));}
    bool waitExit(DWORD timeout=5000){return WaitForSingleObject(pi.hProcess,timeout)==WAIT_OBJECT_0;}
    DWORD exitCode(){DWORD code=99;GetExitCodeProcess(pi.hProcess,&code);return code;}
};
std::wstring textOf(HWND control){const LRESULT n=SendMessageW(control,WM_GETTEXTLENGTH,0,0);if(n<0||n>1024*1024)throw std::runtime_error("remote RichEdit length is invalid");std::wstring text(static_cast<size_t>(n)+1,L'\0');const LRESULT got=SendMessageW(control,WM_GETTEXT,static_cast<WPARAM>(text.size()),reinterpret_cast<LPARAM>(text.data()));if(got<0)throw std::runtime_error("remote RichEdit read failed");text.resize(static_cast<size_t>(got));return text;}
void click(HWND window,int id){check(IsWindowEnabled(testItem(window,id))!=FALSE,"test attempted to click a disabled control");SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),0);}
RECT clientRect(HWND window,HWND child){RECT r{};check(GetWindowRect(child,&r)!=FALSE,"child rectangle unavailable");MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);return r;}
size_t childWindowCount(HWND parent){size_t count=0;for(HWND child=GetWindow(parent,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))++count;return count;}
int jsonIntField(const std::string& json,const char* key){const std::string needle=std::string("\"")+key+"\":";const size_t start=json.find(needle);check(start!=std::string::npos,"persisted window size field missing");size_t at=start+needle.size();while(at<json.size()&&(json[at]==' '||json[at]=='\t'))++at;size_t end=at;while(end<json.size()&&json[end]>='0'&&json[end]<='9')++end;check(end>at,"persisted window size is not an integer");return std::stoi(json.substr(at,end-at));}
Child launch(const std::wstring& exe,const std::wstring& args){Child c;c.start(exe,args);c.waitIdle();return c;}
constexpr UINT testReaperControl=WM_APP+15;
constexpr WPARAM testQueryRetiredTasks=1,testQueryTimerReapedTasks=2,testSuppressEventReaping=3;
LRESULT reaperQuery(HWND window,WPARAM query){return SendMessageW(window,testReaperControl,query,0);}
constexpr UINT testCaptureControl=WM_APP+17;
void injectCandidate(HWND window,const std::wstring& text,bool notify=true){
    std::wstring payload=(notify?L"SAT/1:test_candidate\n":L"SAT/1:test_candidate_silent\n")+text;payload.push_back(L'\0');
    COPYDATASTRUCT data{0x53415431,static_cast<DWORD>(payload.size()*sizeof(wchar_t)),payload.data()};
    check(SendMessageW(window,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&data))!=0,"candidate injection rejected");
}
template<class Predicate> void awaitState(Predicate predicate,const char* message){
    const auto deadline=GetTickCount64()+3000;while(!predicate()&&GetTickCount64()<deadline)Sleep(10);check(predicate(),message);
}
void optionalOcrPathSettingsTest(const std::wstring& exe,const std::filesystem::path& configPath){
    const auto configDirectory=configPath.parent_path();const auto missingPath=configDirectory/L"uninstalled-Snipaste.exe";
    check(!std::filesystem::exists(missingPath),"optional OCR fixture path unexpectedly exists");
    SseResponseServer server({{200,"plain text works without OCR"}});
    Child app=launch(exe,L"--mock-text \"Optional OCR fixture source\"");
    click(app.window,109);HWND settings=FindWindowW(L"SAT.Settings",nullptr);check(settings!=nullptr,"optional OCR settings did not open");
    check(textOf(testItem(settings,343))==LR"(C:\tools\Snipaste\Snipaste.exe)","new config UI lost its explicit Snipaste path default");
    check(testMessage(settings,324,BM_GETCHECK,0,0)==BST_UNCHECKED&&testMessage(settings,339,BM_GETCHECK,0,0)==BST_CHECKED,"new config UI did not use cautious translation/OCR defaults");
    check(setRemoteText(settings,343,missingPath.wstring()),"missing optional OCR path edit failed");click(settings,363);
    check(setRemoteText(settings,302,server.url())&&setRemoteText(settings,303,L"no-ocr-model")&&setRemoteText(settings,304,L"OPTIONAL_OCR_FIXTURE_KEY"),"provider fixture fields could not be entered without Snipaste");
    click(settings,314);check(!IsWindow(settings),"saving a Provider with a missing optional OCR EXE did not close settings");
    std::ifstream saved(configPath,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(saved)),{});check(saved.good()||saved.eof(),"optional OCR config could not be read after save");
    check(bytes.find("uninstalled-Snipaste.exe")!=std::string::npos&&bytes.find(utf8(server.url()))!=std::string::npos&&bytes.find("no-ocr-model")!=std::string::npos&&bytes.find("OPTIONAL_OCR_FIXTURE_KEY")==std::string::npos,"missing OCR path or Provider settings were not safely persisted");
    click(app.window,117);check(textOf(testItem(app.window,110)).find(L"EXE")!=std::wstring::npos&&!SendMessageW(app.window,testCaptureControl,5,0),"starting OCR with a missing EXE did not return a clear error and clean up");
    check(server.requests().empty(),"failed OCR startup issued a Provider request");
    click(app.window,129);check(server.waitForRequests(1),"manual text translation required a working OCR installation");
    awaitState([&]{return textOf(testItem(app.window,101))==L"plain text works without OCR";},"manual text translation failed with a missing OCR executable");
    const auto request=server.requests().front();check(request.find("Optional OCR fixture source")!=std::string::npos&&request.find("no-ocr-model")!=std::string::npos,"manual translation used the wrong saved Provider");
    SendMessageW(app.window,WM_CLOSE,0,0);check(app.waitExit()&&app.exitCode()==0,"optional OCR config test process did not close cleanly");app.window=nullptr;
    Child restarted=launch(exe,L"--mock-text \"Optional OCR fixture source\"");click(restarted.window,109);settings=FindWindowW(L"SAT.Settings",nullptr);check(settings&&textOf(testItem(settings,343))==missingPath.wstring(),"missing OCR path did not survive an application restart");
    click(settings,363);check(textOf(testItem(settings,302))==server.url()&&textOf(testItem(settings,303))==L"no-ocr-model","saved Provider did not reload after an application restart");
    SendMessageW(settings,WM_CLOSE,0,0);SendMessageW(restarted.window,WM_CLOSE,0,0);check(restarted.waitExit()&&restarted.exitCode()==0,"restarted optional OCR test process did not close cleanly");restarted.window=nullptr;
    std::cout<<"optional_ocr_config_save_reload_manual_translation: PASS (local HTTP fixture)\n";
}
int captureWindowTests(const std::wstring& exe){
    const auto folder=requireTestDirectory(L"capture-test-config");std::filesystem::create_directories(folder);
    struct FixtureCleanup{
        std::vector<DWORD> pids;
        ~FixtureCleanup(){for(DWORD pid:pids){HANDLE stop=OpenEventW(EVENT_MODIFY_STATE,FALSE,(L"Local\\SAT.SnipasteFixture."+std::to_wstring(pid)).c_str());if(stop){SetEvent(stop);CloseHandle(stop);}HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,pid);if(process){WaitForSingleObject(process,3000);CloseHandle(process);}}}
    } fixtures;
    const auto record=std::filesystem::path(exe).parent_path()/L"launch.json";
    auto fixturePid=[&]()->DWORD{try{std::ifstream file(record,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(file)),{});const auto value=sat::json::Parser(bytes).parse();return static_cast<DWORD>(*value.get("pid")->number());}catch(...){return 0;}};
    auto start=[&](int startupShow=SW_HIDE){const DWORD previous=fixturePid();Child app;app.start(exe,L"--snip",startupShow);app.waitIdle();awaitState([&]{return fixturePid()!=0&&fixturePid()!=previous;},"capture command fixture not launched");fixtures.pids.push_back(fixturePid());check(!IsWindowVisible(app.window),"cold capture was visible before OCR");return app;};
    auto panelFor=[&](HWND window){awaitState([]{return FindWindowW(L"SAT.OcrConfirm",nullptr)!=nullptr;},"candidate panel missing");SendMessageW(window,WM_NULL,0,0);HWND panel=FindWindowW(L"SAT.OcrConfirm",nullptr);check(panel&&IsWindowVisible(panel)&&IsWindowEnabled(panel)&&!IsWindowVisible(window),"candidate panel is not independently visible and operable");return panel;};
    auto close=[&](Child& app){SendMessageW(app.window,WM_CLOSE,0,0);check(app.waitExit()&&app.exitCode()==0,"capture window did not exit cleanly");app.window=nullptr;};
    SseResponseServer server({{200,"adopted automatic translation"},{200,"confirmed automatic translation"}});
    SetEnvironmentVariableW(L"SAT_CAPTURE_DIAGNOSTICS",L"1");
    SetEnvironmentVariableW(L"SAT_SHOW_TEST_UI",L"1");
    {Child setup=launch(exe,L"");click(setup.window,109);HWND settings=FindWindowW(L"SAT.Settings",nullptr);
        check(settings!=nullptr,"capture setup unavailable");
        check(setRemoteText(settings,343,(std::filesystem::path(exe).parent_path()/L"SnipasteLaunchFixture.exe").wstring()),"capture fixture path unavailable");
        check(setRemoteText(settings,302,server.url())&&setRemoteText(settings,303,L"capture-model")&&setRemoteText(settings,304,L"CAPTURE_PRIVATE_TEST_KEY"),"capture translation profile setup failed");
        testMessage(settings,324,BM_SETCHECK,BST_UNCHECKED,0);click(settings,314);check(!IsWindow(settings),"capture setup failed");
        SendMessageW(setup.window,WM_CLOSE,0,0);check(setup.waitExit(),"capture setup exit failed");setup.window=nullptr;}
    for(bool confirm:{false,true}){
        Child app=start();
        SendMessageW(app.window,testCaptureControl,1,confirm);
        SendMessageW(app.window,testCaptureControl,7,!confirm);
        const std::wstring text=confirm?L"CONFIRMED_PRIVATE_OCR":L"AUTO_ADOPT_PRIVATE_OCR";
        injectCandidate(app.window,text);
        if(confirm){
            awaitState([]{return FindWindowW(L"SAT.OcrConfirm",nullptr)!=nullptr;},"hidden capture never offered candidate");
            SendMessageW(app.window,WM_NULL,0,0);
            HWND panel=FindWindowW(L"SAT.OcrConfirm",nullptr);
            std::cout<<"candidate window visible="<<(IsWindowVisible(panel)!=FALSE)<<" enabled="<<(IsWindowEnabled(panel)!=FALSE)<<" main visible="<<(IsWindowVisible(app.window)!=FALSE)<<"\n";
            check(IsWindowVisible(panel)&&IsWindowEnabled(panel)&&!IsWindowVisible(app.window),"candidate confirmation was hidden or blocked");
            click(panel,603);
        }
        awaitState([&]{return IsWindowVisible(app.window)&&textOf(testItem(app.window,100))==text;},"adopted cold OCR did not reveal actual top-level window and source control");
        check(!SendMessageW(app.window,testCaptureControl,5,0),"adopted OCR leaked waiting state");
        if(!confirm){check(server.waitForRequests(1),"confirmation-off autoStart did not start WinHTTP request");awaitState([&]{return textOf(testItem(app.window,101))==L"adopted automatic translation";},"confirmation-off auto translation was not displayed");}
        check(server.requests().size()==1,"autoStart-off mode sent an additional request");
        check(IsWindowVisible(testItem(app.window,129)),"OCR arrival did not expose the top Translate action");
        std::cout<<(confirm?"cold_confirm":"cold_auto_adopt")<<": PASS (IsWindowVisible + source RichEdit)\n";
        Child repeat;repeat.start(exe,L"--snip");check(repeat.waitExit()&&app.waitExit(),"visible shortcut IPC did not exit");app.window=nullptr;
    }
    {
        Child app=start(SW_SHOWNORMAL);SendMessageW(app.window,testCaptureControl,1,TRUE);SendMessageW(app.window,testCaptureControl,7,TRUE);
        injectCandidate(app.window,L"CONFIRMED_NETWORK_PRIVATE_OCR");HWND panel=panelFor(app.window);check(server.requests().size()==1,"candidate sent before confirmation");click(panel,603);
        awaitState([&]{return IsWindowVisible(app.window)&&textOf(testItem(app.window,100))==L"CONFIRMED_NETWORK_PRIVATE_OCR";},"confirmed OCR failed to show source before auto translation");
        check(server.waitForRequests(2),"confirmed autoStart did not start WinHTTP request");awaitState([&]{return textOf(testItem(app.window,101))==L"confirmed automatic translation";},"confirmed translation result not displayed");
        const auto request=server.requests().back();check(request.find("CONFIRMED_NETWORK_PRIVATE_OCR")!=std::string::npos,"auto translation used wrong source session");
        SendMessageW(app.window,testCaptureControl,7,FALSE);SendMessageW(app.window,testCaptureControl,1,FALSE);
        const auto previousSourceId=SendMessageW(app.window,testCaptureControl,9,0);
        const DWORD previous=fixturePid();click(app.window,117);awaitState([&]{return fixturePid()!=0&&fixturePid()!=previous;},"same-text capture fixture missing");fixtures.pids.push_back(fixturePid());
        check(!IsWindowVisible(app.window),"same-text capture did not hide existing window");injectCandidate(app.window,L"CONFIRMED_NETWORK_PRIVATE_OCR");
        awaitState([&]{return IsWindowVisible(app.window)&&!SendMessageW(app.window,testCaptureControl,5,0);},"same-text OCR did not create and finish fresh input session");check(textOf(testItem(app.window,100))==L"CONFIRMED_NETWORK_PRIVATE_OCR"&&SendMessageW(app.window,testCaptureControl,9,0)>previousSourceId,"same-text OCR failed to establish a new input session");close(app);
        std::cout<<"confirmed_auto_translate_and_same_text: PASS (local HTTP/SSE, real GUI)\n";
    }
    {
        Child app=start();SendMessageW(app.window,testCaptureControl,1,TRUE);SendMessageW(app.window,testCaptureControl,4,0);injectCandidate(app.window,L"BUSY_PRIVATE_OCR");HWND panel=panelFor(app.window);
        click(panel,604);check(!FindWindowW(L"SAT.OcrConfirm",nullptr)&&!IsWindowVisible(app.window)&&SendMessageW(app.window,testCaptureControl,5,0),"ignore did not continue hidden wait");
        injectCandidate(app.window,L"OLD_PRIVATE_OCR");panel=panelFor(app.window);SendMessageW(app.window,testCaptureControl,3,0);click(panel,603);
        check(!IsWindowVisible(app.window)&&textOf(testItem(app.window,100)).empty(),"competing sequence accepted old candidate");
        panel=panelFor(app.window);click(panel,604);injectCandidate(app.window,L"FRESH_PRIVATE_OCR");panel=panelFor(app.window);click(panel,603);
        awaitState([&]{return IsWindowVisible(app.window)&&textOf(testItem(app.window,100))==L"FRESH_PRIVATE_OCR";},"fresh candidate did not wake hidden window after contention");close(app);
        std::cout<<"busy_retry_ignore_competition_fresh_wakeup: PASS\n";
    }
    for(bool confirm:{false,true}){
        Child app=start();SendMessageW(app.window,testCaptureControl,1,confirm);SendMessageW(app.window,testCaptureControl,7,FALSE);
        injectCandidate(app.window,L"SILENT_NOTIFICATION_PRIVATE_OCR",false);
        if(confirm)click(panelFor(app.window),603);
        awaitState([&]{return IsWindowVisible(app.window)&&textOf(testItem(app.window,100))==L"SILENT_NOTIFICATION_PRIVATE_OCR";},"sequence-only update failed to wake hidden window");
        close(app);
    }
    std::cout<<"missing_clipboard_notification_recovery: PASS (both candidate policies, actual GUI)\n";
    for(int scenario:{0,1,2,3,4,5,6}){
        Child app=start();SendMessageW(app.window,testCaptureControl,1,TRUE);
        if(scenario==0){Child repeat;repeat.start(exe,L"--snip");check(repeat.waitExit(),"waiting shortcut IPC failed");}
        if(scenario==1)SendMessageW(app.window,WM_KEYDOWN,VK_ESCAPE,0);
        if(scenario==2){SendMessageW(app.window,testCaptureControl,2,0);PostMessageW(app.window,WM_TIMER,0x5342,0);}
        if(scenario>=3){injectCandidate(app.window,L"CANCEL_PRIVATE_OCR");HWND panel=panelFor(app.window);
            if(scenario==3)click(panel,605);
            if(scenario==4){SendMessageW(app.window,testCaptureControl,2,0);PostMessageW(app.window,WM_TIMER,0x5342,0);}
            if(scenario==5){SendMessageW(app.window,testCaptureControl,6,0);click(panel,603);}
            if(scenario==6){Child repeat;repeat.start(exe,L"--snip");check(repeat.waitExit(),"confirmation shortcut IPC failed");}
        }
        check(app.waitExit()&&app.exitCode()==0,"cold cancel/timeout/stale session did not release hidden lifetime");app.window=nullptr;
    }
    {
        Child app=start();SendMessageW(app.window,testCaptureControl,1,FALSE);injectCandidate(app.window,L" \t ");SendMessageW(app.window,testCaptureControl,2,0);PostMessageW(app.window,WM_TIMER,0x5342,0);check(app.waitExit()&&app.exitCode()==0,"empty result did not time out without revealing blank window");app.window=nullptr;
    }
    std::ifstream log(folder/L"capture-lifecycle.log",std::ios::binary);const std::string diagnostics((std::istreambuf_iterator<char>(log)),{});
    check(!diagnostics.empty()&&diagnostics.find("stage=main_revealed")!=std::string::npos&&diagnostics.find("reason=session_mismatch")!=std::string::npos&&diagnostics.find("stage=clipboard_retry")!=std::string::npos&&diagnostics.find("stage=clipboard_sequence_recovery")!=std::string::npos,"capture diagnostics missing required transitions");
    check(diagnostics.find("PRIVATE")==std::string::npos&&diagnostics.find("capture-model")==std::string::npos,"diagnostics leaked OCR text or credentials");
    check(server.requests().size()==2,"cancel/stale/manual OCR sent an extra request");
    std::cout<<"cancel_timeout_stale_session_ipc_diagnostics: PASS\n";
    std::cout<<"capture_window_tests: PASS\n";return 0;
}
#include "stability_process_tests.hpp"
}
int wmain(int argc,wchar_t**argv){
    std::unique_ptr<HeldHttpServer> mock;Child primary;
    try{
        if(argc==3&&std::wstring(argv[2])==L"--capture-window-tests")return captureWindowTests(argv[1]);
        if(argc==3&&std::wstring(argv[2])==L"--stability-tests")return stabilityProcessTests(argv[1]);
        check(argc==2,"expected Translator.exe path");const std::wstring exe=argv[1];const wchar_t* configDirEnv=L"SAT_CONFIG_DIR";wchar_t configDir[32768]{};const DWORD dirLen=GetEnvironmentVariableW(configDirEnv,configDir,static_cast<DWORD>(std::size(configDir)));check(dirLen>0&&dirLen<std::size(configDir),"test config directory missing");const auto configDirectory=requireTestDirectory(L"process-test-config");std::error_code resetError;std::filesystem::remove_all(configDirectory,resetError);check(!resetError,"could not clear the isolated process-test config directory");std::filesystem::create_directories(configDirectory,resetError);check(!resetError,"could not recreate the isolated process-test config directory");const auto configPath=configDirectory/L"config.json";optionalOcrPathSettingsTest(exe,configPath);mock=std::make_unique<HeldHttpServer>();primary=launch(exe,L"--mock-text \"Initial 中文\"");HWND source=testItem(primary.window,100),result=testItem(primary.window,101);check(source&&result,"RichEdit controls missing");check(textOf(source)==L"Initial 中文","startup text was not shown");check((GetWindowLongPtrW(source,GWL_STYLE)&ES_READONLY)!=0,"source should start read-only");wchar_t settingsCaption[64]{};check(testItem(primary.window,109)!=nullptr&&GetWindowTextW(testItem(primary.window,109),settingsCaption,static_cast<int>(std::size(settingsCaption)))>0&&std::wstring(settingsCaption)==L"设置","settings toolbar button lost its accessible label");check(GetNextDlgTabItem(primary.window,source,FALSE)==result,"main-window tab sequence did not advance from source to result");
        {Child conflictingSmoke;conflictingSmoke.start(exe,L"--self-test-ui");check(conflictingSmoke.waitExit()&&conflictingSmoke.exitCode()==2,"UI smoke reused an existing window and falsely reported success");}
        RECT split{},expanded{};GetWindowRect(source,&split);MapWindowPoints(nullptr,primary.window,reinterpret_cast<POINT*>(&split),2);click(primary.window,105);check((GetWindowLongPtrW(source,GWL_STYLE)&ES_READONLY)==0,"unlock did not enable editing");check(!IsWindowVisible(result),"translation pane should hide during source editing");GetWindowRect(source,&expanded);MapWindowPoints(nullptr,primary.window,reinterpret_cast<POINT*>(&expanded),2);check(expanded.right-expanded.left>split.right-split.left+100,"source pane did not expand while editing");
        SetWindowTextW(source,L"Uncommitted draft");click(primary.window,107);check(textOf(source)==L"Initial 中文","cancel edit did not restore prior text");
        click(primary.window,105);SetWindowTextW(source,L"Changed draft");click(primary.window,106);check(textOf(source)==L"Initial 中文","restore OCR snapshot failed");click(primary.window,107);
        {RECT originalWindow{};GetWindowRect(primary.window,&originalWindow);const UINT dpi=GetDpiForWindow(primary.window);RECT smallOuter{0,0,MulDiv(620,static_cast<int>(dpi),96),MulDiv(460,static_cast<int>(dpi),96)};AdjustWindowRectExForDpi(&smallOuter,static_cast<DWORD>(GetWindowLongPtrW(primary.window,GWL_STYLE)),FALSE,static_cast<DWORD>(GetWindowLongPtrW(primary.window,GWL_EXSTYLE)),dpi);check(SetWindowPos(primary.window,nullptr,0,0,smallOuter.right-smallOuter.left,smallOuter.bottom-smallOuter.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"could not resize to the specified minimum width");auto checkToolbar=[&]{const int ids[]={103,104,105,106,107,108,109,111};RECT client{};GetClientRect(primary.window,&client);std::vector<RECT> rectangles;for(int id:ids){HWND item=testItem(primary.window,id);if((GetWindowLongPtrW(item,GWL_STYLE)&WS_VISIBLE)==0)continue;const RECT rectangle=clientRect(primary.window,item);check(rectangle.left>=0&&rectangle.right<=client.right&&rectangle.top>=0&&rectangle.bottom<=client.bottom,"narrow toolbar control escaped the client area");for(const auto& previous:rectangles){RECT overlap{};check(!IntersectRect(&overlap,&rectangle,&previous),"narrow toolbar buttons overlap");}rectangles.push_back(rectangle);}};checkToolbar();click(primary.window,105);checkToolbar();check(IsWindowEnabled(testItem(primary.window,108)),"minimum-width edit submit was disabled");click(primary.window,107);check(SetWindowPos(primary.window,nullptr,0,0,originalWindow.right-originalWindow.left,originalWindow.bottom-originalWindow.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"could not restore the original window size");}
        click(primary.window,109);HWND settings=FindWindowW(L"SAT.Settings",nullptr);check(settings!=nullptr,"settings window did not open");
        check(testItem(settings,343)&&testItem(settings,413),"Snipaste path setting/label missing");
        check(textOf(testItem(settings,343))==(configDirectory/L"uninstalled-Snipaste.exe").wstring(),"optional missing OCR path did not survive application restart");
        const auto snipasteFixture=std::filesystem::path(exe).parent_path()/L"SnipasteLaunchFixture.exe";
        check(setRemoteText(settings,343,snipasteFixture.wstring()),"explicit fixture path edit failed");
for(int id=319;id<=323;++id)check(testItem(settings,id)!=nullptr,"settings section heading missing");for(int id=400;id<=406;++id)check(testItem(settings,id)!=nullptr,"persistent settings row label missing");for(int id=1307;id<=1313;++id)check(testItem(settings,id)!=nullptr,"persistent checkbox label missing");for(int id=307;id<=313;++id)check(!textOf(testItem(settings,id)).empty()&&textOf(testItem(settings,id))==textOf(testItem(settings,id+1000)),"owner-drawn checkbox lost its native accessible name");check(testItem(settings,1324)!=nullptr&&testMessage(settings,324,BM_GETCHECK,0,0)==BST_UNCHECKED,"automatic translation was enabled by default in settings");check(testMessage(settings,339,BM_GETCHECK,0,0)==BST_CHECKED,"OCR candidate confirmation was disabled by default in settings");SendMessageW(settings,WM_COMMAND,MAKEWPARAM(1307,STN_CLICKED),reinterpret_cast<LPARAM>(testItem(settings,1307)));check(testMessage(settings,307,BM_GETCHECK,0,0)==BST_CHECKED,"clicking a settings checkbox label did not toggle its control");SendMessageW(settings,WM_COMMAND,MAKEWPARAM(1307,STN_CLICKED),reinterpret_cast<LPARAM>(testItem(settings,1307)));check(testMessage(settings,307,BM_GETCHECK,0,0)==BST_UNCHECKED,"clicking a settings checkbox label twice did not restore its state");const size_t settingsChildrenBeforeLayout=childWindowCount(settings);
        const bool configExistedBeforeInvalidSave=std::filesystem::exists(configPath);std::string configBeforeInvalidSave;if(configExistedBeforeInvalidSave){std::ifstream originalConfig(configPath,std::ios::binary);configBeforeInvalidSave.assign(std::istreambuf_iterator<char>(originalConfig),{});check(static_cast<bool>(originalConfig)||originalConfig.eof(),"could not snapshot settings before validation test");}
        check(SetWindowPos(settings,nullptr,0,0,680,470,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"settings resize failed");
        HWND viewport=testItem(settings,350);SCROLLINFO scroll{sizeof(scroll),SIF_ALL};check(GetScrollInfo(testItem(settings,355),SB_CTL,&scroll)!=FALSE&&scroll.nMax>=static_cast<int>(scroll.nPage),"short settings viewport did not scroll");
        click(settings,363);const RECT scrollBefore=clientRect(settings,testItem(settings,302));SendMessageW(settings,WM_VSCROLL,MAKEWPARAM(SB_BOTTOM,0),0);const RECT after=clientRect(settings,testItem(settings,302));
        check(after.top<scrollBefore.top,"settings body did not move with scrolling");check(childWindowCount(settings)==settingsChildrenBeforeLayout,"settings relayout duplicated HWNDs");
        const RECT view=clientRect(settings,viewport),save=clientRect(settings,testItem(settings,314));check(view.bottom<=save.top,"viewport overlaps fixed footer");
        check(GetParent(testItem(settings,302))==viewport&&GetParent(testItem(settings,314))==settings,"settings viewport ownership is broken");
        testMessage(settings,300,CB_SETCURSEL,2,0);testMessage(settings,324,BM_SETCHECK,BST_UNCHECKED,0);check(setRemoteText(settings,302,mock->url())&&setRemoteText(settings,303,L"mock-model")&&setRemoteText(settings,304,L"process-test-key")&&setRemoteText(settings,305,L"5")&&setRemoteText(settings,306,L"32"),"process test could not populate model settings controls");const std::wstring populatedBaseUrl=textOf(testItem(settings,302)),populatedModel=textOf(testItem(settings,303));if(populatedBaseUrl!=mock->url()||populatedModel!=L"mock-model")throw std::runtime_error("settings control readback mismatch: base="+utf8(populatedBaseUrl)+" model="+utf8(populatedModel));
        auto rejectMalformedSetting=[&](int id,const wchar_t* invalid,const wchar_t* restore,const char* label){check(setRemoteText(settings,id,invalid),"could not populate malformed numeric setting");click(settings,314);check(IsWindow(settings),label);check(!textOf(testItem(settings,316)).empty(),"invalid numeric setting did not show an error");const bool configExists=std::filesystem::exists(configPath);check(configExists==configExistedBeforeInvalidSave,"invalid numeric setting changed config-file existence");if(configExists){std::ifstream currentConfig(configPath,std::ios::binary);std::string currentBytes((std::istreambuf_iterator<char>(currentConfig)),{});check(currentBytes==configBeforeInvalidSave,"invalid numeric setting modified persisted config");}check(setRemoteText(settings,id,restore),"could not restore valid numeric setting after rejection");};
        rejectMalformedSetting(305,L"5junk",L"5","malformed timeout was accepted");rejectMalformedSetting(306,L"32junk",L"32","malformed token limit was accepted");click(settings,314);check(!IsWindow(settings),"settings did not close after save");check(testMessage(primary.window,102,CB_GETCURSEL,0,0)==2,"saved target language did not update main UI");
        std::ifstream config(configPath,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(config)),{});check(bytes.find("process-test-key")==std::string::npos,"API key leaked into config JSON");check(bytes.find(utf8(mock->url()))!=std::string::npos,"saved Base URL was missing from config");check(bytes.find("\"credential_ref\":\"\"")==std::string::npos,"API Key was not persisted as a DPAPI credential reference");config.close();
        {
            const int visibilitySettings[]={309,310,311,312,313};const int toolbarButtons[]={103,105,106,104,108};
            auto controlHasVisibleStyle=[&](int id){const HWND control=testItem(primary.window,id);return control&&(GetWindowLongPtrW(control,GWL_STYLE)&WS_VISIBLE)!=0;};
            click(primary.window,109);HWND visibility=FindWindowW(L"SAT.Settings",nullptr);check(visibility!=nullptr,"settings did not reopen for toolbar visibility test");
            for(int id:visibilitySettings)testMessage(visibility,id,BM_SETCHECK,BST_UNCHECKED,0);
            click(visibility,314);check(!IsWindow(visibility),"toolbar visibility settings did not save");
            for(int id:toolbarButtons)check(!controlHasVisibleStyle(id),"saved toolbar visibility preference did not hide its control");
            SendMessageW(primary.window,WM_COMMAND,MAKEWPARAM(105,BN_CLICKED),0);check(!controlHasVisibleStyle(106),"disabled restore preference exposed the restore control while editing");check(controlHasVisibleStyle(107),"cancel edit disappeared when optional toolbar controls were hidden");check(controlHasVisibleStyle(108)&&IsWindowEnabled(testItem(primary.window,108)),"edit submission was hidden or disabled by a toolbar preference");
            SendMessageW(primary.window,WM_COMMAND,MAKEWPARAM(107,BN_CLICKED),0);check(textOf(source)==L"Initial 中文","visibility test did not restore the original text after cancel edit");
            click(primary.window,109);visibility=FindWindowW(L"SAT.Settings",nullptr);check(visibility!=nullptr,"settings did not reopen to verify persisted toolbar visibility");
            for(int id:visibilitySettings)check(testMessage(visibility,id,BM_GETCHECK,0,0)==BST_UNCHECKED,"toolbar visibility preference was not restored in settings");
            for(int id:visibilitySettings)testMessage(visibility,id,BM_SETCHECK,BST_CHECKED,0);
            click(visibility,314);check(!IsWindow(visibility),"restored toolbar visibility settings did not save");
            for(int id:toolbarButtons)if(id!=106)check(controlHasVisibleStyle(id),"re-enabled toolbar preference did not restore its control");
            check(!controlHasVisibleStyle(106),"restore control should stay hidden outside edit mode");SendMessageW(primary.window,WM_COMMAND,MAKEWPARAM(105,BN_CLICKED),0);check(controlHasVisibleStyle(106),"re-enabled restore preference did not show while editing");SendMessageW(primary.window,WM_COMMAND,MAKEWPARAM(107,BN_CLICKED),0);check(!controlHasVisibleStyle(106),"restore control remained visible after leaving edit mode");
        }
        click(primary.window,105);const std::wstring editedText=L"  Edited while editing 中文  ";check(setRemoteText(primary.window,100,editedText),"could not set the active edit draft");testMessage(primary.window,102,CB_SETCURSEL,3,0);SendMessageW(primary.window,WM_COMMAND,MAKEWPARAM(102,CBN_SELCHANGE),reinterpret_cast<LPARAM>(testItem(primary.window,102)));check(!mock->waitForRequest(250),"changing target language during edit started an automatic API request");check(textOf(result).empty(),"changing target language during edit retained a stale translation");check(textOf(source)==editedText,"changing target language discarded the edit draft");
        check(SendMessageW(primary.window,testReaperControl,testSuppressEventReaping,1)!=0,"test window did not enable isolated timer-reaper observation");click(primary.window,108);check(mock->waitForRequest(5000),"committing an edited source did not send the live WinHTTP request");check(textOf(source)==editedText,"committing an edit stripped meaningful leading or trailing whitespace");const auto editRequest=mock->request();check(editRequest.find(utf8(editedText))!=std::string::npos&&editRequest.find("Translate the supplied text into ja.")!=std::string::npos,"committed edit request did not use the edited source and selected target language");click(primary.window,111);const ULONGLONG reaperDeadline=GetTickCount64()+5000;while(reaperQuery(primary.window,testQueryTimerReapedTasks)==0&&GetTickCount64()<reaperDeadline)Sleep(10);check(reaperQuery(primary.window,testQueryTimerReapedTasks)>0,"live WinHTTP task was not reaped by the window timer");check(reaperQuery(primary.window,testQueryRetiredTasks)==0,"timer-reaped task remained in the retired queue");check(SendMessageW(primary.window,testReaperControl,testSuppressEventReaping,0)!=0,"test window did not restore normal event reaping");mock->release();
        {Child second=launch(exe,L"");check(second.waitExit()&&second.exitCode()==0,"second empty launch was not forwarded cleanly");}check(textOf(source)==editedText,"empty second launch replaced the current text");
        {Child second=launch(exe,L"--mock-text \"Replacement 中文\"");check(second.waitExit()&&second.exitCode()==0,"second text launch was not forwarded cleanly");}SendMessageW(primary.window,WM_APP+14,0,0);check(textOf(source)==L"Replacement 中文","second instance text was not delivered");
        {
            SseResponseServer response({{200,utf8(L"Connection OK")},{200,utf8(L"Mock 英译")},{200,utf8(L"Mock 日译")},{200,utf8(L"Mock 英译")},{200,utf8(L"Mock 自动译")},{503,"{\"error\":\"MOCK_PRIVATE_ERROR_BODY\"}"},{200,utf8(L"Mock 重试译")}});click(primary.window,109);HWND responseSettings=FindWindowW(L"SAT.Settings",nullptr);check(responseSettings!=nullptr,"settings did not open for UI streaming-response test");
            testMessage(responseSettings,300,CB_SETCURSEL,2,0);testMessage(responseSettings,324,BM_SETCHECK,BST_UNCHECKED,0);check(setRemoteText(responseSettings,302,response.url()),"UI response test could not set the local SSE endpoint");click(responseSettings,317);check(response.waitForRequests(1),"settings connection test did not send a real WinHTTP request");const ULONGLONG connectionDeadline=GetTickCount64()+5000;while(textOf(testItem(responseSettings,316))!=L"连接成功，模型已返回译文。测试使用少量 API Token。"&&GetTickCount64()<connectionDeadline)Sleep(10);check(textOf(testItem(responseSettings,316))==L"连接成功，模型已返回译文。测试使用少量 API Token。","settings connection test did not show its success state");check(setRemoteText(responseSettings,303,L"edited-model"),"connection snapshot test could not change the model");check(textOf(testItem(responseSettings,316))==L"API 配置已更改，请重新测试连接。","editing API fields retained an obsolete connection success");check(setRemoteText(responseSettings,303,L"mock-model"),"connection snapshot model did not restore");click(responseSettings,314);check(!IsWindow(responseSettings),"UI response settings did not save");
            {Child second=launch(exe,L"--mock-text \"Auto-off 中文\"");check(second.waitExit()&&second.exitCode()==0,"second auto-off text launch was not forwarded cleanly");}check(textOf(source)==L"Auto-off 中文","auto-off text was not delivered");check(!response.waitForRequests(2,250),"disabled automatic translation still sent a request");
            click(primary.window,109);responseSettings=FindWindowW(L"SAT.Settings",nullptr);check(responseSettings!=nullptr&&testMessage(responseSettings,324,BM_GETCHECK,0,0)==BST_UNCHECKED,"disabled automatic translation preference was not restored");testMessage(responseSettings,324,BM_SETCHECK,BST_CHECKED,0);click(responseSettings,314);check(!IsWindow(responseSettings),"enabling automatic translation did not save");check(!response.waitForRequests(2,250),"saving the setting automatically submitted the existing source");click(primary.window,108);
            const std::wstring firstTranslation=L"Mock 英译",secondTranslation=L"Mock 日译";auto waitForResult=[&](const std::wstring& expected){const ULONGLONG deadline=GetTickCount64()+5000;while(GetTickCount64()<deadline){if(textOf(result)==expected&&textOf(testItem(primary.window,110))==L"翻译完成。")return true;Sleep(10);}return false;};
            check(response.waitForRequests(1),"UI translation did not send its first real WinHTTP request");check(waitForResult(firstTranslation),"first streamed translation did not reach the result pane");
            testMessage(primary.window,102,CB_SETCURSEL,3,0);SendMessageW(primary.window,WM_COMMAND,MAKEWPARAM(102,CBN_SELCHANGE),reinterpret_cast<LPARAM>(testItem(primary.window,102)));check(textOf(result)!=firstTranslation,"changing target language retained the prior translation");check(response.waitForRequests(2),"target-language change did not send a second WinHTTP request");check(waitForResult(secondTranslation),"second streamed translation did not replace the first result");
            click(primary.window,109);HWND languageSettings=FindWindowW(L"SAT.Settings",nullptr);check(languageSettings!=nullptr,"settings language regression did not open");testMessage(languageSettings,300,CB_SETCURSEL,2,0);click(languageSettings,314);check(textOf(result).empty(),"settings language change retained a stale completed translation");check(textOf(source)==L"Auto-off 中文","settings language change replaced the committed source");check(!response.waitForRequests(4,250),"settings language save automatically translated existing text");
            click(primary.window,109);languageSettings=FindWindowW(L"SAT.Settings",nullptr);testMessage(languageSettings,300,CB_SETCURSEL,3,0);click(languageSettings,314);
            const std::wstring editDraft=L"Draft changed during editing 中文";click(primary.window,105);check(setRemoteText(primary.window,100,editDraft),"could not set a post-translation edit draft");click(primary.window,109);HWND draftSettings=FindWindowW(L"SAT.Settings",nullptr);check(draftSettings!=nullptr,"editing settings language regression did not open");testMessage(draftSettings,300,CB_SETCURSEL,2,0);click(draftSettings,314);check(textOf(result).empty(),"editing settings language change retained a translation in the old language");check(textOf(source)==editDraft,"editing target-language change discarded the draft");check(!response.waitForRequests(4,250),"editing target-language change started an API request before explicit submission");click(primary.window,108);check(response.waitForRequests(4),"explicitly submitting the edited source did not start the fourth translation request");check(waitForResult(firstTranslation),"edited source translation did not reach the result pane");
            {Child whitespace=launch(exe,L"--mock-text \"   \"");check(whitespace.waitExit()&&whitespace.exitCode()==0,"whitespace input was not forwarded");}check(textOf(source)==L"   ","whitespace input snapshot was altered");check(!response.waitForRequests(5,250),"whitespace-only input caused a billed translation request");
            {Child second=launch(exe,L"--mock-text \"Auto-start 中文\"");check(second.waitExit()&&second.exitCode()==0,"second auto-start text launch was not forwarded cleanly");}const std::wstring autoText=L"Auto-start 中文",autoTranslation=L"Mock 自动译";check(response.waitForRequests(5),"new incoming text did not start automatic translation");check(waitForResult(autoTranslation),"automatic translation did not reach the result pane");
            click(primary.window,108);check(response.waitForRequests(6),"manual translation request did not start");const std::wstring serviceFailure=L"服务商暂时不可用（HTTP 503）。请稍后重试。";const ULONGLONG failureDeadline=GetTickCount64()+5000;while(textOf(testItem(primary.window,110))!=serviceFailure&&GetTickCount64()<failureDeadline)Sleep(10);const auto visibleFailure=textOf(testItem(primary.window,110)),failedResult=textOf(result);check(visibleFailure==serviceFailure,"HTTP 503 did not produce actionable retry guidance");check(visibleFailure.find(L"MOCK_PRIVATE_ERROR_BODY")==std::wstring::npos&&failedResult.find(L"MOCK_PRIVATE_ERROR_BODY")==std::wstring::npos,"provider error body leaked into the UI");check(failedResult==autoTranslation,"failed retry did not recover the completed translation");check(!response.waitForRequests(7,250)&&response.requests().size()==6,"HTTP 503 triggered an automatic retry");click(primary.window,108);check(response.waitForRequests(7),"retry button did not send a new request after HTTP 503");check(waitForResult(L"Mock 重试译"),"retry response did not reach the result pane");
            const auto requests=response.requests();check(requests.size()==7,"UI mock did not capture connection, translation, auto-start, failure, and retry requests");const std::string sourceBytes=utf8(L"Auto-off 中文"),editBytes=utf8(editDraft),autoBytes=utf8(autoText);check(requests[1].find(sourceBytes)!=std::string::npos&&requests[2].find(sourceBytes)!=std::string::npos&&requests[3].find(editBytes)!=std::string::npos&&requests[4].find(autoBytes)!=std::string::npos&&requests[5].find(autoBytes)!=std::string::npos&&requests[6].find(autoBytes)!=std::string::npos,"translation request did not use the source current at submission");const std::string englishTarget="Translate the supplied text into en.",japaneseTarget="Translate the supplied text into ja.";if(requests[1].find(englishTarget)==std::string::npos||requests[2].find(japaneseTarget)==std::string::npos||requests[3].find(englishTarget)==std::string::npos||requests[4].find(englishTarget)==std::string::npos){auto context=[](const std::string& request){const size_t at=request.find("Translate the supplied text into");return at==std::string::npos?std::string("<missing>"):request.substr(at,100);};throw std::runtime_error("UI requests did not use the selected languages; first="+context(requests[1])+" second="+context(requests[2])+" edit="+context(requests[3])+" auto="+context(requests[4]));}for(const auto& request:requests){check(request.find("\"stream\":true")!=std::string::npos,"UI connection/translation request did not use streaming responses");check(request.find("Authorization: Bearer process-test-key")!=std::string::npos,"UI request did not use the decrypted API credential");const size_t bodyAt=request.find("\r\n\r\n");check(bodyAt!=std::string::npos&&request.substr(bodyAt+4).find("process-test-key")==std::string::npos,"API credential leaked into the JSON request body");}
            click(primary.window,109);HWND autoSettings=FindWindowW(L"SAT.Settings",nullptr);check(autoSettings!=nullptr,"settings did not reopen to disable automatic translation for isolated later cases");testMessage(autoSettings,324,BM_SETCHECK,BST_UNCHECKED,0);click(autoSettings,314);check(!IsWindow(autoSettings),"automatic translation preference did not save when disabled");
        }
        {
            click(primary.window,109);HWND clearSettings=FindWindowW(L"SAT.Settings",nullptr);check(clearSettings!=nullptr,"settings did not open to clear the saved API credential");check(textOf(testItem(clearSettings,343))==snipasteFixture.wstring(),"Snipaste path was not retained after reopening settings");check(setRemoteText(clearSettings,304,L""),"could not clear the API key field");click(clearSettings,314);check(!IsWindow(clearSettings),"clearing the API key did not save");
            std::ifstream clearedConfig(configPath,std::ios::binary);std::string clearedBytes((std::istreambuf_iterator<char>(clearedConfig)),{});check(clearedBytes.find("\"credential_ref\":\"\"")!=std::string::npos,"cleared API key still has a persisted credential reference");clearedConfig.close();
            bool encryptedCredentialRemains=false;for(const auto& entry:std::filesystem::directory_iterator(configDir)){const auto name=entry.path().filename().wstring();if(name.rfind(L"secret-",0)==0&&entry.path().extension()==L".bin")encryptedCredentialRemains=true;}check(!encryptedCredentialRemains,"clearing the API key left an orphaned DPAPI credential file");
            click(primary.window,109);clearSettings=FindWindowW(L"SAT.Settings",nullptr);check(clearSettings!=nullptr,"settings did not reopen to restore the isolated test credential");check(setRemoteText(clearSettings,304,L"process-test-key"),"could not restore the isolated API key");click(clearSettings,314);if(IsWindow(clearSettings))throw std::runtime_error("restoring the isolated API key did not save: "+utf8(textOf(testItem(clearSettings,316))));
        }
        check(SetWindowPos(primary.window,nullptr,0,0,820,520,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"could not resize main window for client-size persistence test");RECT beforeClose{};GetClientRect(primary.window,&beforeClose);const UINT windowDpi=GetDpiForWindow(primary.window);const int expectedWidthDip=MulDiv(beforeClose.right-beforeClose.left,96,static_cast<int>(windowDpi)),expectedHeightDip=MulDiv(beforeClose.bottom-beforeClose.top,96,static_cast<int>(windowDpi));
        SendMessageW(primary.window,WM_CLOSE,0,0);check(primary.waitExit()&&primary.exitCode()==0,"last window close did not exit Translator");primary.window=nullptr;std::ifstream resizedConfig(std::filesystem::path(configDir)/L"config.json",std::ios::binary);std::string resizedBytes((std::istreambuf_iterator<char>(resizedConfig)),{});resizedConfig.close();const int persistedWidthDip=jsonIntField(resizedBytes,"width_dip"),persistedHeightDip=jsonIntField(resizedBytes,"height_dip");const std::string sizeError="window client size mismatch: expected "+std::to_string(expectedWidthDip)+"x"+std::to_string(expectedHeightDip)+" DIP, saved "+std::to_string(persistedWidthDip)+"x"+std::to_string(persistedHeightDip);check(persistedWidthDip==expectedWidthDip&&persistedHeightDip==expectedHeightDip,sizeError.c_str());
        {
            std::ifstream preservedConfig(configPath,std::ios::binary);const std::string original((std::istreambuf_iterator<char>(preservedConfig)),{});preservedConfig.close();std::vector<std::pair<std::filesystem::path,std::string>> credentials;for(const auto& entry:std::filesystem::directory_iterator(configDirectory))if(entry.path().extension()==L".bin"){std::ifstream file(entry.path(),std::ios::binary);credentials.emplace_back(entry.path(),std::string((std::istreambuf_iterator<char>(file)),{}));}check(!credentials.empty(),"damaged-config regression requires a real DPAPI credential");
            auto writeBytes=[](const std::filesystem::path& path,const std::string& value){std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(value.data(),static_cast<std::streamsize>(value.size()));check(static_cast<bool>(file),"could not write isolated recovery fixture");};auto readBytes=[](const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return std::string((std::istreambuf_iterator<char>(file)),{});};
            const std::string damagedCases[]={"{broken", "{\"schema_version\":2}","{\"schema_version\":1,\"provider\":{\"credential_ref\":42}}"};
            for(const auto& damaged:damagedCases){writeBytes(configPath,damaged);Child recovery=launch(exe,L"");check(!textOf(testItem(recovery.window,110)).empty(),"damaged configuration did not produce a visible warning");SendMessageW(recovery.window,WM_CLOSE,0,0);check(recovery.waitExit()&&recovery.exitCode()==0,"damaged configuration did not close cleanly");recovery.window=nullptr;check(readBytes(configPath)==damaged,"closing after a damaged load silently rewrote the original config");for(const auto& credential:credentials)check(readBytes(credential.first)==credential.second,"closing after a damaged load deleted or changed recoverable DPAPI data");}
            writeBytes(configPath,original);writeBytes(credentials.front().first,"invalid-DPAPI-fixture");{Child recovery=launch(exe,L"");SendMessageW(recovery.window,WM_CLOSE,0,0);check(recovery.waitExit()&&recovery.exitCode()==0,"DPAPI failure did not close cleanly");recovery.window=nullptr;}check(readBytes(configPath)==original&&readBytes(credentials.front().first)=="invalid-DPAPI-fixture","DPAPI load failure triggered destructive automatic save");writeBytes(credentials.front().first,credentials.front().second);
        }
        {
            HeldHttpServer blocked;Child reaper=launch(exe,L"--mock-text \"Timer failure source\"");click(reaper.window,109);HWND reaperSettings=FindWindowW(L"SAT.Settings",nullptr);check(reaperSettings!=nullptr,"timer failure settings did not open");check(setRemoteText(reaperSettings,302,blocked.url()),"timer failure endpoint was not set");click(reaperSettings,314);check(!IsWindow(reaperSettings),"timer failure setup did not save");check(SendMessageW(reaper.window,testReaperControl,4,1)!=0,"timer failure injection did not activate");click(reaper.window,108);check(blocked.waitForRequest(5000),"timer failure test did not start a real request");click(reaper.window,109);reaperSettings=FindWindowW(L"SAT.Settings",nullptr);const int oldLanguage=static_cast<int>(testMessage(reaperSettings,300,CB_GETCURSEL,0,0));testMessage(reaperSettings,300,CB_SETCURSEL,oldLanguage==3?2:3,0);click(reaperSettings,314);check(!IsWindow(reaperSettings),"in-flight language settings did not save");check(textOf(testItem(reaper.window,100))==L"Timer failure source"&&textOf(testItem(reaper.window,101)).empty(),"in-flight settings change lost source or retained stale output");check((GetWindowLongPtrW(testItem(reaper.window,111),GWL_STYLE)&WS_VISIBLE)==0,"in-flight settings language change did not cancel the old request");const ULONGLONG finishDeadline=GetTickCount64()+5000;while(reaperQuery(reaper.window,testQueryRetiredTasks)!=0&&GetTickCount64()<finishDeadline)Sleep(10);check(reaperQuery(reaper.window,testQueryRetiredTasks)==0,"finished worker was not reaped when SetTimer failed");click(reaper.window,109);reaperSettings=FindWindowW(L"SAT.Settings",nullptr);testMessage(reaperSettings,300,CB_SETCURSEL,oldLanguage,0);click(reaperSettings,314);SendMessageW(reaper.window,WM_CLOSE,0,0);check(reaper.waitExit()&&reaper.exitCode()==0,"timer failure scenario did not exit cleanly");reaper.window=nullptr;blocked.release();
        }
        {const auto limitPath=configDirectory/L"ipc-limit.txt";const std::string limitBytes(512*1024,'x');{std::ofstream file(limitPath,std::ios::binary);file.write(limitBytes.data(),static_cast<std::streamsize>(limitBytes.size()));check(static_cast<bool>(file),"could not create the bounded IPC fixture");}Child boundary=launch(exe,L"--mock-text \"Boundary source\"");{Child second=launch(exe,L"--text-file \""+limitPath.wstring()+L"\"");check(second.waitExit()&&second.exitCode()==0,"maximum-size source was rejected because IPC counted protocol overhead as text");}// IPC acknowledgement enqueues input; the UI applies it on its next
            // message turn. Observe the real content within the existing 5 s
            // deadline instead of racing that turn or adding a fixed delay.
            const std::wstring expected(512*1024,L'x');std::wstring received;const ULONGLONG inputDeadline=GetTickCount64()+5000;
            do{received=textOf(testItem(boundary.window,100));if(received==expected)break;Sleep(10);}while(GetTickCount64()<inputDeadline);
            check(received==expected,"maximum-size IPC source was truncated or lost");SendMessageW(boundary.window,WM_CLOSE,0,0);check(boundary.waitExit()&&boundary.exitCode()==0,"maximum-size source did not close cleanly");boundary.window=nullptr;}
        {Child owned=launch(exe,L"--settings");HWND ownedSettings=FindWindowW(L"SAT.Settings",nullptr);check(ownedSettings!=nullptr&&GetWindow(ownedSettings,GW_OWNER)==owned.window,"settings was not owned by the main window");SendMessageW(owned.window,WM_CLOSE,0,0);check(owned.waitExit()&&owned.exitCode()==0&&!IsWindow(ownedSettings),"closing the owner left a dead settings window or a residual process");owned.window=nullptr;}
        {Child blank=launch(exe,L"");HWND blankSource=testItem(blank.window,100),blankResult=testItem(blank.window,101);check(blankSource&&blankResult&&textOf(blankSource).empty()&&textOf(blankResult).empty(),"no-argument startup did not show an empty paste window");check(textOf(testItem(blank.window,110)).find(L"按 Ctrl+V 粘贴要翻译的文本。")!=std::wstring::npos&&textOf(testItem(blank.window,110)).find(L"?")!=std::wstring::npos,"empty paste window did not explain the primary input action");SendMessageW(blank.window,WM_CLOSE,0,0);check(blank.waitExit()&&blank.exitCode()==0,"empty no-argument window did not exit cleanly");blank.window=nullptr;}
        const std::filesystem::path inputPath=std::filesystem::path(configDir)/L"中文 输入 测试.txt";const std::wstring fileText=L"File path 中文 🌏";{std::ofstream file(inputPath,std::ios::binary|std::ios::trunc);std::string fileBytes="\xef\xbb\xbf";fileBytes+=utf8(fileText);file.write(fileBytes.data(),static_cast<std::streamsize>(fileBytes.size()));check(static_cast<bool>(file),"Unicode-path fixture could not be written");}
        {Child fileInput=launch(exe,L"--text-file \""+inputPath.wstring()+L"\"");HWND fileSource=testItem(fileInput.window,100);check(fileSource&&textOf(fileSource)==fileText,"UTF-8 text file at a Chinese/space path was not loaded");SendMessageW(fileInput.window,WM_CLOSE,0,0);check(fileInput.waitExit()&&fileInput.exitCode()==0,"text-file Translator did not exit cleanly");fileInput.window=nullptr;}
        const auto missingPath=std::filesystem::path(inputPath.wstring()+L".missing"),emptyPath=std::filesystem::path(configDir)/L"空 输入.txt",oversizedPath=std::filesystem::path(configDir)/L"超限 输入.txt";
        {Child missing;missing.start(exe,L"--text-file \""+missingPath.wstring()+L"\"");missing.expectStartupError(L"missing text file");}
        {std::ofstream empty(emptyPath,std::ios::binary|std::ios::trunc);check(static_cast<bool>(empty),"empty text-file fixture could not be created");empty.close();Child invalid;invalid.start(exe,L"--text-file \""+emptyPath.wstring()+L"\"");invalid.expectStartupError(L"empty text file");}
        {std::ofstream oversized(oversizedPath,std::ios::binary|std::ios::trunc);std::vector<char> oversizedBytes(1024*1024+1,'x');oversized.write(oversizedBytes.data(),static_cast<std::streamsize>(oversizedBytes.size()));check(static_cast<bool>(oversized),"oversized text-file fixture could not be written");oversized.close();Child invalid;invalid.start(exe,L"--text-file \""+oversizedPath.wstring()+L"\"");invalid.expectStartupError(L"oversized text file");}
        {Child invalid;invalid.start(exe,L"--unknown-option");invalid.expectStartupError(L"unknown command line option");}
        {Child diagnostic;diagnostic.start(exe,L"--diagnose");HWND dialog=nullptr;const ULONGLONG deadline=GetTickCount64()+5000;while(GetTickCount64()<deadline){ProcessDialog search{diagnostic.pi.dwProcessId};EnumWindows(findProcessDialog,reinterpret_cast<LPARAM>(&search));if(search.found){dialog=search.found;break;}if(WaitForSingleObject(diagnostic.pi.hProcess,0)==WAIT_OBJECT_0)break;Sleep(10);}check(dialog!=nullptr,"--diagnose did not show its local diagnostic report");wchar_t title[256]{};GetWindowTextW(dialog,title,static_cast<int>(std::size(title)));check(std::wstring(title).find(L"诊断")!=std::wstring::npos,"--diagnose report title was missing");PostMessageW(dialog,WM_CLOSE,0,0);bool exited=diagnostic.waitExit(3000);if(!exited){PostMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDOK,BN_CLICKED),0);exited=diagnostic.waitExit(2000);}const DWORD diagnosticExit=diagnostic.exitCode();if(!exited||diagnosticExit!=0)throw std::runtime_error("--diagnose did not exit cleanly after acknowledgement; exited="+std::to_string(exited)+" code="+std::to_string(diagnosticExit));}
        {
            const std::filesystem::path diagnosticPath=std::filesystem::path(configDir)/L"diagnostic-no-create";
            check(!std::filesystem::exists(diagnosticPath),"diagnostic test directory already exists");
            check(SetEnvironmentVariableW(L"SAT_CONFIG_DIR",diagnosticPath.c_str())!=FALSE,"could not set isolated diagnostic directory");
            struct RestoreConfigDir{const wchar_t* value;~RestoreConfigDir(){SetEnvironmentVariableW(L"SAT_CONFIG_DIR",value);}} restoreConfigDir{configDir};
            {
                Child diagnostic;diagnostic.start(exe,L"--diagnose");HWND dialog=nullptr;const ULONGLONG deadline=GetTickCount64()+5000;
                while(GetTickCount64()<deadline){ProcessDialog search{diagnostic.pi.dwProcessId};EnumWindows(findProcessDialog,reinterpret_cast<LPARAM>(&search));if(search.found){dialog=search.found;break;}if(WaitForSingleObject(diagnostic.pi.hProcess,0)==WAIT_OBJECT_0)break;Sleep(10);}
                check(dialog!=nullptr,"--diagnose did not show a report for a missing config directory");PostMessageW(dialog,WM_CLOSE,0,0);
                check(diagnostic.waitExit(3000)&&diagnostic.exitCode()==0,"--diagnose did not exit cleanly for a missing config directory");
            }
            check(!std::filesystem::exists(diagnosticPath),"--diagnose created a missing config directory");
        }        std::error_code removeError;for(const auto& path:{inputPath,emptyPath,oversizedPath}){std::filesystem::remove(path,removeError);check(!removeError,"text-file fixture cleanup failed");}
        {Child restarted=launch(exe,L"--mock-text Restart");RECT afterRestart{};GetClientRect(restarted.window,&afterRestart);check(std::abs(static_cast<int>(afterRestart.right-afterRestart.left)-beforeClose.right)<2&&std::abs(static_cast<int>(afterRestart.bottom-afterRestart.top)-beforeClose.bottom)<2,"restarted window client dimensions drifted");check(testMessage(restarted.window,102,CB_GETCURSEL,0,0)==2,"selected English target language was not restored after restart");SendMessageW(restarted.window,WM_CLOSE,0,0);check(restarted.waitExit()&&restarted.exitCode()==0,"restarted Translator did not exit cleanly");restarted.window=nullptr;}
        {Child settingsLaunch;settingsLaunch.start(exe,L"--settings");settingsLaunch.waitIdle();const ULONGLONG settingsDeadline=GetTickCount64()+5000;HWND startupSettings=nullptr;while(GetTickCount64()<settingsDeadline&&!startupSettings){startupSettings=FindWindowW(L"SAT.Settings",nullptr);if(!startupSettings)Sleep(10);}check(startupSettings!=nullptr,"--settings did not open the settings window");check(testItem(startupSettings,318)==nullptr,"tutorial remains in settings body");click(startupSettings,352);HWND helpWindow=FindWindowW(L"SAT.Help",nullptr);check(helpWindow!=nullptr,"settings question mark did not open help");HWND guideControl=testItem(helpWindow,318);const std::wstring firstUse=textOf(guideControl);for(const wchar_t* required:{L"Ctrl+V",L"目标语言",L"模型与 API",L"当前 OCR 来源为 Snipaste",L"OCR 来源可执行文件路径",L"DPAPI",L"测试连接",L"输出 Token 上限",L"思考 Token",L"连接中断",L"不包含正文、API Key 或 Authorization",L"已有完整译文会恢复显示",L"Ctrl+R"})check(firstUse.find(required)!=std::wstring::npos,"first-use guide omitted a required product term or recovery detail");check((GetWindowLongPtrW(guideControl,GWL_STYLE)&ES_READONLY)!=0,"help is not selectable native read-only text");SendMessageW(helpWindow,WM_KEYDOWN,VK_ESCAPE,0);check(!IsWindow(helpWindow)&&IsWindow(startupSettings),"help Escape closed settings or failed to close help");SendMessageW(startupSettings,WM_CLOSE,0,0);check(IsWindow(settingsLaunch.window),"closing the --settings window also closed the main window");SendMessageW(settingsLaunch.window,WM_CLOSE,0,0);check(settingsLaunch.waitExit()&&settingsLaunch.exitCode()==0,"--settings launch did not exit cleanly after the last window closed");settingsLaunch.window=nullptr;}
        {auto closingMock=std::make_unique<HeldHttpServer>();Child closing=launch(exe,L"--mock-text \"Shutdown 中文\"");click(closing.window,109);HWND closingSettings=FindWindowW(L"SAT.Settings",nullptr);check(closingSettings!=nullptr,"settings did not open for in-flight shutdown test");check(setRemoteText(closingSettings,302,closingMock->url()),"shutdown test could not set its local HTTP endpoint");click(closingSettings,314);if(IsWindow(closingSettings))throw std::runtime_error("shutdown test settings save failed: "+utf8(textOf(testItem(closingSettings,316))));click(closing.window,108);check(closingMock->waitForRequest(5000),"shutdown test did not start a live WinHTTP request");SendMessageW(closing.window,WM_CLOSE,0,0);check(closing.waitExit(7000)&&closing.exitCode()==0,"Translator did not exit cleanly with a live request");closing.window=nullptr;closingMock->release();}
        {Child minimized=launch(exe,L"");click(minimized.window,109);HWND geometrySettings=FindWindowW(L"SAT.Settings",nullptr);check(geometrySettings!=nullptr,"minimized geometry settings did not open");testMessage(geometrySettings,308,BM_SETCHECK,BST_CHECKED,0);click(geometrySettings,314);check(!IsWindow(geometrySettings),"remember-position preference did not save");check(SetWindowPos(minimized.window,nullptr,0,0,900,620,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"normal geometry resize failed");RECT normalClient{};GetClientRect(minimized.window,&normalClient);const UINT dpi=GetDpiForWindow(minimized.window);const int width=MulDiv(normalClient.right,96,static_cast<int>(dpi)),height=MulDiv(normalClient.bottom,96,static_cast<int>(dpi));ShowWindow(minimized.window,SW_MINIMIZE);check(IsIconic(minimized.window),"geometry test did not enter the actual minimized state");SendMessageW(minimized.window,WM_CLOSE,0,0);check(minimized.waitExit()&&minimized.exitCode()==0,"minimized window did not exit cleanly");minimized.window=nullptr;std::ifstream file(configPath,std::ios::binary);const std::string geometry((std::istreambuf_iterator<char>(file)),{});file.close();check(jsonIntField(geometry,"width_dip")==width&&jsonIntField(geometry,"height_dip")==height,"minimized close overwrote the last normal geometry");Child restored=launch(exe,L"");check(textOf(testItem(restored.window,110)).find(L"按 Ctrl+V 粘贴要翻译的文本。") == 0,"minimized close produced an invalid configuration");SendMessageW(restored.window,WM_CLOSE,0,0);check(restored.waitExit()&&restored.exitCode()==0,"normal restore after minimized close failed");restored.window=nullptr;}
        {
            const std::wstring ocrText=L"OCR 识别内容测试 2026";
            Child ocrApp=launch(exe,L"--mock-ocr \""+ocrText+L"\"");
            const ULONGLONG ocrDeadline=GetTickCount64()+5000;
            HWND ocrSource=testItem(ocrApp.window,100);
            while(textOf(ocrSource)!=ocrText&&GetTickCount64()<ocrDeadline)Sleep(10);
            check(textOf(ocrSource)==ocrText,"reverse OCR launch did not populate source pane with mock text");

            const std::wstring secondOcrText=L"第二次 OCR 文字 转交测试";
            {
                Child second=launch(exe,L"--mock-ocr \""+secondOcrText+L"\"");
                check(second.waitExit()&&second.exitCode()==0,"second mock-ocr instance did not forward cleanly");
            }
            const ULONGLONG secondDeadline=GetTickCount64()+5000;
            while(textOf(ocrSource)!=secondOcrText&&GetTickCount64()<secondDeadline)Sleep(10);
            check(textOf(ocrSource)==secondOcrText,"second mock-ocr text was not delivered via IPC forwarding");
            // Keep cancellation checks independent of GUI paint speed and the mock's two-poll completion.
            awaitState([&]{return SendMessageW(ocrApp.window,testCaptureControl,8,0)!=0;},"previous input transaction did not finish before arming deterministic mock wait");

            // Focus notifications and duplicate requests must not toggle cancellation.
            SendMessageW(ocrApp.window,WM_COMMAND,MAKEWPARAM(117,BN_SETFOCUS),0);
            check(SendMessageW(ocrApp.window,testCaptureControl,5,0)==0,"focus notification started capture");
            const bool beforeCaptureVisible=IsWindowVisible(ocrApp.window)!=FALSE;
            // 1. First click starts wait state
            click(ocrApp.window,117);
            if(textOf(testItem(ocrApp.window,110)).find(L"等待截图文字")==std::wstring::npos)
                std::cout<<"capture wait status superseded; actual capture active="<<SendMessageW(ocrApp.window,testCaptureControl,5,0)<<'\n';
            check(SendMessageW(ocrApp.window,testCaptureControl,5,0)!=0,"clicking captureButton did not start OCR wait state");
            const auto captureGeneration=SendMessageW(ocrApp.window,testCaptureControl,10,0);
            check(!IsWindowVisible(ocrApp.window),"capture button left the translator window visible during screenshot selection");
            // 2. Second click keeps the same wait state.
            click(ocrApp.window,117);
            if(textOf(testItem(ocrApp.window,110)).find(L"等待截图文字")==std::wstring::npos)
                std::cout<<"duplicate capture status superseded; actual capture active="<<SendMessageW(ocrApp.window,testCaptureControl,5,0)<<'\n';
            check(SendMessageW(ocrApp.window,testCaptureControl,5,0)!=0&&SendMessageW(ocrApp.window,testCaptureControl,10,0)==captureGeneration,"duplicate click cancelled or restarted OCR wait state");
            SendMessageW(ocrApp.window,WM_KEYDOWN,VK_ESCAPE,0);
            check(textOf(testItem(ocrApp.window,110))==L"截图已取消。"&&(IsWindowVisible(ocrApp.window)!=FALSE)==beforeCaptureVisible,"Esc did not restore the pre-capture window visibility");
            // 3. Third click after explicit Esc restarts wait state freshly.
            click(ocrApp.window,117);
            check(SendMessageW(ocrApp.window,testCaptureControl,5,0)!=0&&SendMessageW(ocrApp.window,testCaptureControl,10,0)>captureGeneration,"third click on captureButton did not restart OCR wait state");
            // 4. Esc key cancels freshly restarted wait state
            SendMessageW(ocrApp.window,WM_KEYDOWN,VK_ESCAPE,0);
            check(textOf(testItem(ocrApp.window,110))==L"截图已取消。","Esc key did not cancel active OCR capture session");

            // 5. Continuous IPC delivery while idle:
            const std::wstring thirdOcrText=L"连续 OCR 任务交付 3";
            {
                Child third=launch(exe,L"--mock-ocr \""+thirdOcrText+L"\"");
                check(third.waitExit()&&third.exitCode()==0,"third mock-ocr instance did not forward cleanly");
            }
            const ULONGLONG thirdDeadline=GetTickCount64()+5000;
            while(textOf(ocrSource)!=thirdOcrText&&GetTickCount64()<thirdDeadline)Sleep(10);
            check(textOf(ocrSource)==thirdOcrText,"third mock-ocr text was not delivered via IPC forwarding");

            SendMessageW(ocrApp.window,WM_CLOSE,0,0);
            check(ocrApp.waitExit()&&ocrApp.exitCode()==0,"mock-ocr test instance did not close cleanly");
            ocrApp.window=nullptr;
        }

        // P1 UI integration test:
        {
            Child p1App = launch(exe, L"--mock-text \"P1 UI Integration Test\"");
            auto isChildVisible = [](HWND h) { return h && (GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE) != 0; };
            HWND btnRewrite = testItem(p1App.window, 118);
            HWND btnDeAi = testItem(p1App.window, 119);
            HWND btnPrompt = testItem(p1App.window, 120);
            HWND btnVersion = testItem(p1App.window, 121);
            check(btnRewrite && btnDeAi && btnPrompt && btnVersion, "P1 toolbar buttons missing from main window");
            check(isChildVisible(btnRewrite) && isChildVisible(btnDeAi) && isChildVisible(btnPrompt) && isChildVisible(btnVersion), "P1 toolbar buttons not visible by default");
            DWORD handlesBefore=0,handlesAfter=0;check(GetProcessHandleCount(p1App.pi.hProcess,&handlesBefore)!=0,"initial handle measurement failed");
            const DWORD gdiBefore=GetGuiResources(p1App.pi.hProcess,GR_GDIOBJECTS);
            for(int cycle=0;cycle<20;++cycle){click(p1App.window,109);HWND page=FindWindowW(L"SAT.Settings",nullptr);check(page!=nullptr,"settings repeat failed");click(page,315);check(!IsWindow(page),"settings cancel repeat failed");}
            check(GetProcessHandleCount(p1App.pi.hProcess,&handlesAfter)!=0,"final handle measurement failed");
            const DWORD gdiAfter=GetGuiResources(p1App.pi.hProcess,GR_GDIOBJECTS);
            std::cout<<"p1 resource cycles=20 handles="<<handlesBefore<<"->"<<handlesAfter<<" gdi="<<gdiBefore<<"->"<<gdiAfter<<"\n";
            // Initial settings/WinHTTP initialization can populate native caches.
            // Compare a second equally-sized batch to the warmed first batch.
            Sleep(250);DWORD warmedHandles=0;check(GetProcessHandleCount(p1App.pi.hProcess,&warmedHandles)!=0,"warm handle measurement failed");const DWORD warmedGdi=GetGuiResources(p1App.pi.hProcess,GR_GDIOBJECTS);
            for(int cycle=0;cycle<20;++cycle){click(p1App.window,109);HWND page=FindWindowW(L"SAT.Settings",nullptr);check(page!=nullptr,"warm settings repeat failed");click(page,315);check(!IsWindow(page),"warm settings close failed");}
            Sleep(250);check(GetProcessHandleCount(p1App.pi.hProcess,&handlesAfter)!=0,"warm final handles failed");const DWORD finalGdi=GetGuiResources(p1App.pi.hProcess,GR_GDIOBJECTS);
            std::cout<<"p1 warm resource cycles=20 handles="<<warmedHandles<<"->"<<handlesAfter<<" gdi="<<warmedGdi<<"->"<<finalGdi<<"\n";
            check(handlesAfter<=warmedHandles+4&&finalGdi<=warmedGdi+2,"warmed settings lifecycle accumulated handles/GDI objects");
            const RECT row=clientRect(p1App.window,btnRewrite);
            for(int id:{117,108,118,119,120,121,126,109}){HWND h=testItem(p1App.window,id);if(isChildVisible(h))check(clientRect(p1App.window,h).top==row.top,"toolbar reverted to two rows");}

            // Open custom prompt panel
            click(p1App.window, 120);
            HWND promptWnd = FindWindowExW(p1App.window, nullptr, L"SAT.CustomPrompt", nullptr);
            check(promptWnd != nullptr && isChildVisible(promptWnd), "SAT.CustomPrompt floating window did not open");
            RECT panel{};GetClientRect(promptWnd,&panel);check(panel.right>=360&&panel.bottom>=150,"first-open prompt panel has placeholder geometry");
            check(testItem(promptWnd, 501) != nullptr, "promptScopeBoth radio missing");
            check(testItem(promptWnd, 502) != nullptr, "promptScopeResult radio missing");
            check(testItem(promptWnd, 503) != nullptr, "promptScopeSource radio missing");
            check(testItem(promptWnd, 504) != nullptr, "promptTextEdit missing");
            check(testItem(promptWnd, 506) != nullptr, "promptExecuteBtn missing");
            check(testItem(promptWnd, 507) != nullptr, "promptCancelBtn missing");

            // Cancel custom prompt panel via Esc
            SendMessageW(p1App.window, WM_KEYDOWN, VK_ESCAPE, 0);
            check(!isChildVisible(promptWnd), "custom prompt window was not dismissed by Esc");

            // Test P1 toolbar visibility toggling in settings
            click(p1App.window, 109);
            HWND st = FindWindowW(L"SAT.Settings", nullptr);
            check(st != nullptr, "settings window failed to open for P1 visibility test");
            check(testItem(st, 325) != nullptr, "settingRewrite checkbox missing");
            check(testItem(st, 326) != nullptr, "settingDeAi checkbox missing");
            check(testItem(st, 327) != nullptr, "settingCustomPrompt checkbox missing");
            check(testItem(st, 328) != nullptr, "settingVersionHistory checkbox missing");
            check(testItem(st,336)&&testItem(st,337)&&testItem(st,338)&&testItem(st,340),"P1 configuration completeness controls missing");
            testMessage(st,337,BM_SETCHECK,BST_UNCHECKED,0);check(setRemoteText(st,338,L"3"),"version limit edit failed");
            click(st,342);

            // Uncheck them
            testMessage(st, 325, BM_SETCHECK, BST_UNCHECKED, 0);
            testMessage(st, 326, BM_SETCHECK, BST_UNCHECKED, 0);
            testMessage(st, 327, BM_SETCHECK, BST_UNCHECKED, 0);
            testMessage(st, 328, BM_SETCHECK, BST_UNCHECKED, 0);
            click(st, 314); // save
            check(!IsWindow(st), "settings did not close after saving P1 visibility");

            check(!isChildVisible(btnRewrite), "btnRewrite not hidden after settings change");
            check(!isChildVisible(btnDeAi), "btnDeAi not hidden after settings change");
            check(!isChildVisible(btnPrompt), "btnPrompt not hidden after settings change");
            check(!isChildVisible(btnVersion), "btnVersion not hidden after settings change");
            click(p1App.window,109);st=FindWindowW(L"SAT.Settings",nullptr);check(st!=nullptr,"P1 preferences reopen failed");
            check(testMessage(st,337,BM_GETCHECK,0,0)==BST_UNCHECKED&&textOf(testItem(st,338))==L"3","P1 history controls were not persisted");
            check(testMessage(st,340,CB_GETCOUNT,0,0)==12,"toolbar order persistence lost actions");click(st,315);

            SendMessageW(p1App.window, WM_CLOSE, 0, 0);
            check(p1App.waitExit() && p1App.exitCode() == 0, "p1App test instance did not close cleanly");
            p1App.window = nullptr;
        }

        {
            SseResponseServer translations({{200,"base translation"},{200,"after draft cancel"}});
            SseResponseServer rewrites({{200,"natural translation"},{503,"PRIVATE_REWRITE_ERROR"},{200,"custom source result"}});
            Child ai=launch(exe,L"--mock-text \"Review source\"");
            click(ai.window,109);HWND page=FindWindowW(L"SAT.Settings",nullptr);check(page!=nullptr,"P1 role settings unavailable");
            testMessage(page,300,CB_SETCURSEL,0,0);testMessage(page,324,BM_SETCHECK,BST_UNCHECKED,0);
            check(setRemoteText(page,302,translations.url())&&setRemoteText(page,303,L"translation-model")&&setRemoteText(page,304,L"translation-review-key")&&setRemoteText(page,305,L"10")&&setRemoteText(page,306,L"512"),"translation profile input failed");
            click(page,332); // Persist fields to the draft and add a rewrite profile.
            check(setRemoteText(page,302,rewrites.url())&&setRemoteText(page,303,L"rewrite-model")&&setRemoteText(page,304,L"rewrite-review-key")&&setRemoteText(page,305,L"15")&&setRemoteText(page,306,L"99"),"rewrite profile input failed");
            check(setRemoteText(page,302,L"https://api.deepseek.com/v1")&&setRemoteText(page,303,L"deepseek-flash")&&testMessage(page,344,CB_GETCURSEL,0,0)==1,"new official deepseek-flash profile did not default to disabled thinking");
            check(setRemoteText(page,302,rewrites.url())&&setRemoteText(page,303,L"rewrite-model")&&testMessage(page,344,CB_GETCURSEL,0,0)==0,"non-DeepSeek profile did not return to provider automatic mode");
            testMessage(page,344,CB_SETCURSEL,2,0);SendMessageW(page,WM_COMMAND,MAKEWPARAM(344,CBN_SELCHANGE),0);check(textOf(testItem(page,2344)).find(L"当前接口不支持")!=std::wstring::npos,"unsupported protocol did not show a thinking-mode hint");
            testMessage(page,329,CB_SETCURSEL,0,0);SendMessageW(page,WM_COMMAND,MAKEWPARAM(329,CBN_SELCHANGE),0);check(testMessage(page,344,CB_GETCURSEL,0,0)==0,"translation profile thinking mode was overwritten by rewrite profile edits");
            testMessage(page,329,CB_SETCURSEL,1,0);SendMessageW(page,WM_COMMAND,MAKEWPARAM(329,CBN_SELCHANGE),0);check(testMessage(page,344,CB_GETCURSEL,0,0)==2,"rewrite profile thinking mode did not remain independent");
            testMessage(page,344,CB_SETCURSEL,0,0);SendMessageW(page,WM_COMMAND,MAKEWPARAM(344,CBN_SELCHANGE),0);
            testMessage(page,330,BM_SETCHECK,BST_UNCHECKED,0);testMessage(page,331,BM_SETCHECK,BST_CHECKED,0);click(page,314);check(!IsWindow(page),"multi-role save failed");
            auto waitResult=[&](const wchar_t* expected){const auto until=GetTickCount64()+5000;while(textOf(testItem(ai.window,101))!=expected&&GetTickCount64()<until)Sleep(10);check(textOf(testItem(ai.window,101))==expected,"P1 live output mismatch");const auto done=GetTickCount64()+5000;while(!IsWindowEnabled(testItem(ai.window,119))&&GetTickCount64()<done)Sleep(10);};
            click(ai.window,108);check(translations.waitForRequests(1),"translation role did not receive request");waitResult(L"base translation");
            click(ai.window,119);check(rewrites.waitForRequests(1),"rewrite role did not receive request");waitResult(L"natural translation");
            check(textOf(testItem(ai.window,100))==L"Review source","naturalization changed original source");
            const auto firstTranslation=translations.requests()[0],firstRewrite=rewrites.requests()[0];
            const auto diagnosticFile=configDirectory/L"provider-check.json";
            {Child offline;offline.startCaptured(exe,L"--provider-check rewrite",diagnosticFile);check(offline.waitExit()&&offline.exitCode()==0,"offline CLI failed or opened a window");std::ifstream input(diagnosticFile,std::ios::binary);std::string output((std::istreambuf_iterator<char>(input)),{});const auto report=sat::json::Parser(output).parse();check(*report.get("network_requests")->number()==0&&*report.get("status")->string()=="UNVERIFIED","offline CLI unexpectedly requested network or claimed verification");check(output.find("rewrite-review-key")==std::string::npos,"offline CLI exposed the Key");}
            check(firstTranslation.find("translation-review-key")!=std::string::npos&&firstTranslation.find("rewrite-review-key")==std::string::npos&&firstTranslation.find("translation-model")!=std::string::npos&&firstTranslation.find("\"thinking\"")==std::string::npos,"translation mixed role credentials/model/thinking mode");
            check(firstRewrite.find("rewrite-review-key")!=std::string::npos&&firstRewrite.find("translation-review-key")==std::string::npos&&firstRewrite.find("rewrite-model")!=std::string::npos&&firstRewrite.find("\"max_tokens\":99")!=std::string::npos&&firstRewrite.find("\"thinking\"")==std::string::npos,"rewrite mixed role credentials/model/token limit/thinking mode");
            click(ai.window,119);check(rewrites.waitForRequests(2),"rewrite failure request missing");const auto failed=GetTickCount64()+5000;while(textOf(testItem(ai.window,110)).find(L"HTTP 503")==std::wstring::npos&&GetTickCount64()<failed)Sleep(10);
            check(textOf(testItem(ai.window,110)).find(L"HTTP 503")!=std::wstring::npos&&textOf(testItem(ai.window,101))==L"natural translation","rewrite failure did not recover completed base");
            check(textOf(testItem(ai.window,110)).find(L"PRIVATE_REWRITE_ERROR")==std::wstring::npos,"rewrite error body leaked");
            click(ai.window,120);HWND prompt=FindWindowExW(ai.window,nullptr,L"SAT.CustomPrompt",nullptr);check(prompt!=nullptr,"live custom panel absent");
            click(prompt,503);check(setRemoteText(prompt,504,L"校对原文，保留事实"),"custom Unicode input failed");click(prompt,506);
            check(rewrites.waitForRequests(3),"custom scope request missing");waitResult(L"custom source result");
            const auto custom=rewrites.requests()[2];check(custom.find("[Original Source Text]")!=std::string::npos&&custom.find("[Current Translation]")==std::string::npos&&custom.find(utf8(L"校对原文，保留事实"))!=std::string::npos,"custom source-only sent an irrelevant translation");
            click(ai.window,109);page=FindWindowW(L"SAT.Settings",nullptr);
            testMessage(page,329,CB_SETCURSEL,1,0);SendMessageW(page,WM_COMMAND,MAKEWPARAM(329,CBN_SELCHANGE),0);
            testMessage(page,330,BM_SETCHECK,BST_CHECKED,0);
            testMessage(page,329,CB_SETCURSEL,0,0);SendMessageW(page,WM_COMMAND,MAKEWPARAM(329,CBN_SELCHANGE),0);click(page,315);
            click(ai.window,108);check(translations.waitForRequests(2),"cancelled draft changed live translation role");waitResult(L"after draft cancel");
            click(ai.window,109);page=FindWindowW(L"SAT.Settings",nullptr);testMessage(page,329,CB_SETCURSEL,1,0);SendMessageW(page,WM_COMMAND,MAKEWPARAM(329,CBN_SELCHANGE),0);
            check(setRemoteText(page,304,L""),"clear rewrite key failed");click(page,314);check(!IsWindow(page),"cleared-key save failed");click(ai.window,119);
            check(textOf(testItem(ai.window,110)).find(L"API Key")!=std::wstring::npos&&textOf(testItem(ai.window,101))==L"after draft cancel","missing rewrite key borrowed translation credential or discarded result");
            SendMessageW(ai.window,WM_CLOSE,0,0);check(ai.waitExit()&&ai.exitCode()==0,"P1 live session left a process");ai.window=nullptr;
            std::cout<<"p1_live_roles_failure_custom_cancel: PASS\n";
        }
        {
            SseResponseServer server({{200,"PRIVATE_DIAGNOSTIC_TEXT"}});Child setup=launch(exe,L"");click(setup.window,109);HWND page=FindWindowW(L"SAT.Settings",nullptr);
            check(setRemoteText(page,302,server.url()),"CLI fixture endpoint edit failed");click(page,314);check(!IsWindow(page),"CLI fixture save failed");SendMessageW(setup.window,WM_CLOSE,0,0);check(setup.waitExit()&&setup.exitCode()==0,"CLI fixture setup did not exit");setup.window=nullptr;
            std::ifstream beforeInput(configPath,std::ios::binary);const std::string before((std::istreambuf_iterator<char>(beforeInput)),{});beforeInput.close();
            const auto outputPath=configDirectory/L"provider-cli.json";Child online;online.startCaptured(exe,L"--provider-check-online translation",outputPath);check(online.waitExit(10000)&&online.exitCode()==0,"single-request CLI did not complete headlessly");
            std::ifstream outputInput(outputPath,std::ios::binary);const std::string output((std::istreambuf_iterator<char>(outputInput)),{});const auto report=sat::json::Parser(output).parse();
            outputInput.close();
            check(*report.get("status")->string()=="PASS"&&*report.get("network_requests")->number()==1&&*report.get("http_status")->number()==200&&*report.get("response_is_sse")->boolean(),"CLI structured HTTP/SSE result missing");
            check(!*report.get("live_provider_verified")->boolean()&&output.find("PRIVATE_DIAGNOSTIC_TEXT")==std::string::npos&&output.find("translation-review-key")==std::string::npos,"CLI leaked response/Key or promoted mock to live HTTPS");
            check(server.requests().size()==1&&server.requests()[0].find("\"max_tokens\":64")!=std::string::npos,"CLI did not enforce request/token budget");
            std::ifstream afterInput(configPath,std::ios::binary);check(std::string((std::istreambuf_iterator<char>(afterInput)),{})==before,"CLI rewrote the saved config");afterInput.close();
            Child invalid;invalid.startCaptured(exe,L"--provider-check invalid-role",outputPath);check(invalid.waitExit()&&invalid.exitCode()==2,"invalid CLI opened a blocking dialog or passed");
            std::cout<<"provider_diagnostic_cli: PASS (mock only)\n";
        }
        {
            const auto launchRecord=std::filesystem::path(exe).parent_path()/L"launch.json";
            auto fixturePid=[&]()->DWORD{try{std::ifstream file(launchRecord,std::ios::binary);const std::string bytes((std::istreambuf_iterator<char>(file)),{});const auto report=sat::json::Parser(bytes).parse();const auto* value=report.get("pid");return value&&value->number()?static_cast<DWORD>(*value->number()):0;}catch(...){return 0;}};
            const DWORD previousFixturePid=fixturePid();
            Child cold; cold.start(exe,L"--snip");cold.waitIdle();
            check(cold.window&&!IsWindowVisible(cold.window),"cold --snip startup displayed the translation window before OCR");
            const ULONGLONG fixtureDeadline=GetTickCount64()+3000;DWORD coldFixturePid=0;while(GetTickCount64()<fixtureDeadline){coldFixturePid=fixturePid();if(coldFixturePid&&coldFixturePid!=previousFixturePid)break;Sleep(10);}
            check(coldFixturePid&&coldFixturePid!=previousFixturePid,"cold --snip startup did not launch exactly one Snipaste command fixture");
            Child repeat;repeat.start(exe,L"--snip");check(repeat.waitExit()&&repeat.exitCode()==0,"second --snip invocation did not return after IPC forwarding");
            check(cold.waitExit()&&cold.exitCode()==0,"second --snip invocation did not cancel and exit the hidden OCR session");cold.window=nullptr;
            check(fixturePid()==coldFixturePid,"repeated --snip invocation launched another Snipaste command instead of cancelling the wait");
            HANDLE fixtureStop=OpenEventW(EVENT_MODIFY_STATE,FALSE,(L"Local\\SAT.SnipasteFixture."+std::to_wstring(coldFixturePid)).c_str());if(fixtureStop){SetEvent(fixtureStop);CloseHandle(fixtureStop);}
            HANDLE fixtureProcess=OpenProcess(SYNCHRONIZE,FALSE,coldFixturePid);if(fixtureProcess){WaitForSingleObject(fixtureProcess,3000);CloseHandle(fixtureProcess);}
        }
        {
            Child visible=launch(exe,L"--mock-text \"Shortcut close\"");ShowWindow(visible.window,SW_SHOWNOACTIVATE);
            check(IsWindowVisible(visible.window),"hotkey close fixture did not enter the visible-window state");
            Child closeShortcut;closeShortcut.start(exe,L"--snip");
            check(closeShortcut.waitExit()&&closeShortcut.exitCode()==0,"--snip IPC close request did not return cleanly");
            check(visible.waitExit()&&visible.exitCode()==0,"same shortcut did not close the already-visible Translator instance");visible.window=nullptr;
        }
        std::cout<<"process_tests: PASS\n";return 0;
    }catch(const std::exception&e){std::cerr<<"process_tests: FAIL: "<<e.what()<<"\n";return 1;}
}
