#include "config_store.hpp"
#include "app_version.h"
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <imm.h>
#include <richedit.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <deque>
#include <optional>
#include <atomic>
#include <chrono>
#include <ctime>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <algorithm>
#include <climits>
#include <cstring>
#include <cwchar>
#include <exception>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>
#include <shellapi.h>
#include "translation_client.hpp"
#include "translator_ui.hpp"
#include "ui_frame.hpp"
#include "ui_typography.hpp"
#include "ui_layout.hpp"
#include "ui_icons.hpp"
#include "ocr_adapter.hpp"
#include "provider_diagnostics.hpp"
#include "app_resource.h"

namespace sat {
std::wstring formatDiagnosticReport(const Config& config,bool configExists,bool& hasError);
#if defined(SAT_ENABLE_TEST_HOOKS)
bool testMode(){wchar_t value[8]{};return GetEnvironmentVariableW(L"SAT_TEST_MODE",value,static_cast<DWORD>(std::size(value)))==1&&value[0]==L'1';}
bool showTestUi(){wchar_t value[8]{};return testMode()&&GetEnvironmentVariableW(L"SAT_SHOW_TEST_UI",value,static_cast<DWORD>(std::size(value)))==1&&value[0]==L'1';}
#else
bool testMode(){return false;}
bool showTestUi(){return false;}
#endif
UINT uiDpi(HWND window){
#if defined(SAT_ENABLE_TEST_HOOKS)
    // Isolated render evidence only. This does not change Windows display DPI.
    static const UINT renderDpi=[]()->UINT{wchar_t value[16]{};if(!testMode()||!GetEnvironmentVariableW(L"SAT_TEST_RENDER_DPI",value,16))return 0;const int dpi=_wtoi(value);return dpi==96||dpi==120||dpi==144||dpi==192?static_cast<UINT>(dpi):0u;}();
    if(renderDpi)return renderDpi;
#endif
    return ::GetDpiForWindow(window);
}
const wchar_t* mainWindowClass(){return testMode()?L"SnipasteAITranslator.Test.Main.v1":L"SnipasteAITranslator.Main.v1";}
const wchar_t* mainWindowTitle(){return testMode()?L"OCR AI Translator [Test]":L"OCR AI Translator";}
std::wstring instanceName(bool ready){if(testMode())return ready?L"Local\\SnipasteAITranslator.Test.Ready.v1":L"Local\\SnipasteAITranslator.Test.Instance.v1";return ready?L"Local\\SnipasteAITranslator.Ready.v1":L"Local\\SnipasteAITranslator.Instance.v1";}
constexpr UINT msgShowSettings=WM_APP+11;
constexpr UINT msgNetworkChunk=WM_APP+12;
constexpr UINT msgNetworkDone=WM_APP+13;
constexpr UINT msgProcessPending=WM_APP+14;
constexpr UINT msgCaptureShortcutExit=WM_APP+16;
constexpr UINT_PTR taskReaperTimerId=0x5341;
#if defined(SAT_ENABLE_TEST_HOOKS)
constexpr UINT msgTestReaperControl=WM_APP+15;
constexpr UINT msgTestCaptureControl=WM_APP+17;
enum : WPARAM { testQueryRetiredTasks=1,testQueryTimerReapedTasks,testSuppressEventReaping,testForceReaperTimerFailure };
#endif
constexpr ULONG_PTR copyDataTag=0x53415431;
constexpr size_t maxTextBytes=1024*1024;
enum : int { sourceEdit=100,resultEdit,targetCombo,sourceCopyButton,resultCopyButton,unlockButton,restoreButton,cancelEditButton,translateButton,settingsButton,statusLabel,requestCancelButton,sourceLanguageLabel,languageArrowLabel,sourceCaptionLabel,resultCaptionLabel,titleLabel,captureButton=117,
    rewriteButton=118,deAiButton=119,customPromptButton=120,versionButton=121,moreButton=122,
    mainPin=123,mainMinimize=124,mainClose=125,footerCopy=126,retryButton=127,errorDetailsButton=128,mainTranslateButton=129,
    settingHeader=351,settingHelp=352,settingClose=353,settingBrowse=354,settingTabGeneral=360,settingTabSource=361,settingTabResult=362,settingTabApi=363,settingTabAdvanced=364,
    settingTarget=300,settingTheme,settingBaseUrl,settingModel,settingApiKey,settingTimeout,settingTokens,settingTop,settingRemember,settingSourceCopy,settingUnlock,settingRestore,settingResultCopy,settingRetranslate,settingSave,settingCancel,settingStatus,settingTestButton,settingIntro,settingGeneralTitle,settingSourceTitle,settingResultTitle,settingModelTitle,settingAdvancedTitle,
    settingTargetLabel=400,settingThemeLabel,settingBaseUrlLabel,settingModelLabel,settingApiKeyLabel,settingTimeoutLabel,settingTokensLabel,
    settingAutoTranslate=324,
    settingRewrite=325,settingDeAi=326,settingCustomPrompt=327,settingVersionHistory=328,
    settingProfileCombo=329,settingTransRole=330,settingRewriteRole=331,settingAddProfile=332,settingDeleteProfile=333,settingProfileName=335,
    settingEnabled=336,settingRememberPrompts=337,settingVersionLimit=338,settingOcrConfirmation=339,settingToolbarOrder=340,settingMoveUp=341,settingMoveDown=342,
    settingVersionLimitLabel=411,settingToolbarOrderLabel=412,settingSnipastePathLabel=413,settingThinkingModeLabel=414,settingOcrSourceLabel=415,settingThinkingMode=344,settingSnipastePath=343,settingOcrSource=345,settingErrorDetails=357,
    settingProfileComboLabel=407,settingTransRoleLabel=408,settingRewriteRoleLabel=409,settingProfileNameLabel=410,
    ocrCandidatePanel=599,ocrCandidateTitle=600,ocrCandidateNotice=601,ocrCandidatePreview=602,ocrCandidateUse=603,ocrCandidateIgnore=604,ocrCandidateCancel=605,helpScroll=356,
    promptScopeBoth=501,promptScopeResult=502,promptScopeSource=503,promptTextEdit=504,promptHistoryCombo=505,promptExecuteBtn=506,promptCancelBtn=507,promptStatusLabel=508 };
enum class InputSource { manualPaste, manualEdit, clipboardOnce, mockText, textFile, snipasteOcr, clipboardOcrCandidate };
using InputClock=std::chrono::steady_clock;
struct InputCancelMarker {
    // A single CAS winner decides whether an incoming source is committed or cancelled.
    enum class State : uint8_t { pending,accepted,cancelled };
    std::atomic<State> state{State::pending};
    bool tryAccept()noexcept{State expected=State::pending;return state.compare_exchange_strong(expected,State::accepted,std::memory_order_acq_rel,std::memory_order_acquire);}
    void cancelPending()noexcept{State expected=State::pending;state.compare_exchange_strong(expected,State::cancelled,std::memory_order_acq_rel,std::memory_order_acquire);}
    void cancel()noexcept{state.store(State::cancelled,std::memory_order_release);}
    bool isCancelled()const noexcept{return state.load(std::memory_order_acquire)==State::cancelled;}
};
struct Incoming { bool settings=false,showOnly=false,confirmReplacement=false;InputSource source=InputSource::manualPaste;std::wstring text;uint64_t sourceId=0;InputClock::time_point submittedAt{};std::shared_ptr<InputCancelMarker> cancelMarker; };
struct InputSession { InputSource source=InputSource::manualPaste;uint64_t id=0,textRevision=0;InputClock::time_point submittedAt{};std::shared_ptr<InputCancelMarker> cancelMarker;bool cancelled()const noexcept{return !cancelMarker||cancelMarker->isCancelled();} };
struct UiEventQueue { std::mutex mutex;std::deque<TranslationEvent> events;HWND window{};bool alive=true; };
struct SecretWiper { std::wstring& value;~SecretWiper(){clearSecret(value);} };
struct ConfigSecretWiper { Config& value;~ConfigSecretWiper(){clearConfigSecrets(value);} };
struct FontSet { UINT dpi{};HFONT body{},caption{},emphasis{},note{}; };
struct TextVersion {
    uint64_t versionId=0,sourceRevision=0,parentVersionId=0;
    std::wstring targetLanguage,resultText,sourceSnapshot,rawSnapshot,customPrompt,modelProfileId;
    AiOperationType opType=AiOperationType::translate;
    RewriteStyle rewriteStyle=RewriteStyle::natural;
    CustomPromptScope customScope=CustomPromptScope::sourceAndResult;
    std::chrono::system_clock::time_point createdAt{};
    bool completed=false;
};
struct PendingOcrCandidate { std::wstring text;uint64_t generation=0,inputSessionId=0,adapterSessionId=0;DWORD clipboardSequence=0;ULONGLONG deadline=0; };
struct App {
    HINSTANCE instance{};HWND window{},settings{},settingsViewport{},settingsScroll{},help{},ocrConfirmWnd{},tooltip{},taskReaperWindow{};HANDLE mutex{},ready{};HMODULE richEditModule{};FontSet mainFonts,settingsFonts,helpFonts,ocrFonts;HBRUSH backgroundBrush{},controlBrush{};COLORREF backgroundColor=RGB(255,255,255),foregroundColor=RGB(32,35,40),controlBackgroundColor=RGB(255,255,255),paneBackgroundColor=RGB(255,255,255),borderColor=RGB(226,229,234),mutedColor=RGB(104,113,126);int settingsScrollDip=0,settingsTab=0,settingsWheelRemainder=0;std::wstring settingsPathTip;std::optional<Theme> previewTheme;
    Config config;std::wstring rawSource,source,editRollback,result,status,language=L"zh-Hans",lastErrorDetails,settingsErrorDetails;InputSource sourceKind=InputSource::manualPaste;InputSession inputSession;uint64_t nextSourceId=0,activeSourceId=0,sourceRevision=0,revision=0,requestSerial=0,activeRequestId=0,activeTestId=0;bool editing=false,initialized=false,windowPlaced=false,requestActive=false,connectionTestActive=false,retryAvailable=false;
    bool startHiddenForCapture=false,startupSnipPending=false,captureSessionActive=false,captureWasVisible=false,captureExitOnFailure=false,clipboardListenerRegistered=false,clipboardChangedWhileConfirming=false,clipboardRetryPending=false,exitRequested=false;
    std::shared_ptr<UiEventQueue> uiQueue=std::make_shared<UiEventQueue>();std::shared_ptr<TranslationTask> task,connectionTask;
    std::deque<Incoming> pending;std::vector<std::shared_ptr<TranslationTask>> retiredTasks;
    std::unique_ptr<IOcrAdapter> ocrAdapter;
    uint64_t ocrGeneration=0,ocrInputId=0,ocrAdapterSessionId=0;ULONGLONG ocrDeadline=0;std::optional<PendingOcrCandidate> pendingOcrCandidate;bool ocrConfirming=false;
    std::deque<TextVersion> versions;uint64_t currentVersionId=0,versionSerial=0;
    std::deque<std::wstring> recentPrompts;HWND customPromptWnd=nullptr;
    std::wstring lastCompletedResult,lastCompletedLanguage,retryTargetLanguage;
    AiOperationType activeOpType=AiOperationType::translate;RewriteStyle activeRewriteStyle=RewriteStyle::natural;CustomPromptScope activeCustomScope=CustomPromptScope::sourceAndResult;std::wstring activeCustomPrompt;
    std::vector<int> overflowActions;
    std::wstring settingsTranslationId,settingsRewriteId;
    std::vector<int> settingsToolbarOrder;
    std::vector<ProviderProfile> settingsWorkingProfiles;std::vector<bool> settingsThinkingExplicit;size_t currentSettingsProfileIndex=0;
#if defined(SAT_ENABLE_TEST_HOOKS)
    bool suppressEventReapingForTest=false,forceReaperTimerFailureForTest=false;size_t timerReapedTasksForTest=0;
    unsigned inputApplyDepthForTest=0;
    unsigned failedThemeBrushDeletes=0;
    DWORD testClipboardSequence=0;std::wstring testClipboardText;bool testClipboardBusy=false;
#endif
    ~App(){if(clipboardListenerRegistered&&window&&IsWindow(window))RemoveClipboardFormatListener(window);if(ocrAdapter)ocrAdapter->cancelCapture();if(inputSession.cancelMarker)inputSession.cancelMarker->cancel();for(auto& incoming:pending)if(incoming.cancelMarker)incoming.cancelMarker->cancel();if(taskReaperWindow&&IsWindow(taskReaperWindow))KillTimer(taskReaperWindow,taskReaperTimerId);if(window&&IsWindow(window))KillTimer(window,0x5342);if(uiQueue){std::lock_guard lock(uiQueue->mutex);uiQueue->alive=false;uiQueue->window=nullptr;uiQueue->events.clear();}
        if(task)task->cancel();if(connectionTask)connectionTask->cancel();for(auto& retired:retiredTasks)if(retired)retired->cancel();
        if(task){task->join();task.reset();}if(connectionTask){connectionTask->join();connectionTask.reset();}for(auto& retired:retiredTasks)if(retired)retired->join();retiredTasks.clear();
        clearConfigSecrets(config);for(auto& p:settingsWorkingProfiles)clearSecret(p.apiKey);settingsWorkingProfiles.clear();versions.clear();recentPrompts.clear();
        if(customPromptWnd&&IsWindow(customPromptWnd))DestroyWindow(customPromptWnd);if(window&&IsWindow(window))DestroyWindow(window);if(settings&&IsWindow(settings))DestroyWindow(settings);if(tooltip&&IsWindow(tooltip))DestroyWindow(tooltip);if(mainFonts.body)DeleteObject(mainFonts.body);if(mainFonts.caption)DeleteObject(mainFonts.caption);if(mainFonts.emphasis)DeleteObject(mainFonts.emphasis);if(mainFonts.note)DeleteObject(mainFonts.note);if(settingsFonts.body)DeleteObject(settingsFonts.body);if(settingsFonts.caption)DeleteObject(settingsFonts.caption);if(settingsFonts.emphasis)DeleteObject(settingsFonts.emphasis);if(settingsFonts.note)DeleteObject(settingsFonts.note);if(helpFonts.body)DeleteObject(helpFonts.body);if(helpFonts.caption)DeleteObject(helpFonts.caption);if(helpFonts.emphasis)DeleteObject(helpFonts.emphasis);if(helpFonts.note)DeleteObject(helpFonts.note);if(ocrFonts.body)DeleteObject(ocrFonts.body);if(ocrFonts.caption)DeleteObject(ocrFonts.caption);if(ocrFonts.emphasis)DeleteObject(ocrFonts.emphasis);if(ocrFonts.note)DeleteObject(ocrFonts.note);if(backgroundBrush)DeleteObject(backgroundBrush);if(controlBrush)DeleteObject(controlBrush);if(richEditModule)FreeLibrary(richEditModule);if(ready)CloseHandle(ready);if(mutex){ReleaseMutex(mutex);CloseHandle(mutex);}}
};
App* g=nullptr;
void traceCapture(const char* stage,const char* reason="none",DWORD error=0)noexcept{
    wchar_t enabled[8]{};
    if(!g||GetEnvironmentVariableW(L"SAT_CAPTURE_DIAGNOSTICS",enabled,8)!=1||enabled[0]!=L'1')return;
    try{
        const DWORD uiThread=g->window?GetWindowThreadProcessId(g->window,nullptr):0;
        const std::string line="tick="+std::to_string(GetTickCount64())+" pid="+std::to_string(GetCurrentProcessId())+
            " thread="+std::to_string(GetCurrentThreadId())+" ui_thread="+std::to_string(uiThread)+" stage="+stage+" reason="+reason+
            " error="+std::to_string(error)+" generation="+std::to_string(g->ocrGeneration)+" capture_input="+std::to_string(g->ocrInputId)+
            " source_session="+std::to_string(g->inputSession.id)+" adapter_session="+std::to_string(g->ocrAdapter?g->ocrAdapter->sessionId():0)+
            " expected_adapter="+std::to_string(g->ocrAdapterSessionId)+" request="+std::to_string(g->activeRequestId)+
            " candidate_sequence="+std::to_string(g->pendingOcrCandidate?g->pendingOcrCandidate->clipboardSequence:0)+
            " visible="+std::to_string(g->window&&IsWindowVisible(g->window))+" confirmation_visible="+std::to_string(g->ocrConfirmWnd&&IsWindowVisible(g->ocrConfirmWnd))+
            " active="+std::to_string(g->captureSessionActive)+" listener="+std::to_string(g->clipboardListenerRegistered)+
            " confirming="+std::to_string(g->ocrConfirming)+" retry="+std::to_string(g->clipboardRetryPending)+"\r\n";
        OutputDebugStringA(line.c_str());
        std::filesystem::create_directories(g->config.folder);const auto path=g->config.folder/L"capture-lifecycle.log";
        std::error_code ec;const auto size=std::filesystem::file_size(path,ec);
        if(!ec&&size+line.size()>1024*1024)return;
        std::ofstream out(path,std::ios::binary|std::ios::app);out<<line;
    }catch(...){}
}
// All settings business code addresses IDs through the single content viewport.
HWND uiItem(HWND parent,int id){HWND item=GetDlgItem(parent,id);if(!item&&g&&parent==g->settings&&g->settingsViewport)item=GetDlgItem(g->settingsViewport,id);return item;}
LRESULT uiMessage(HWND parent,int id,UINT message,WPARAM wp,LPARAM lp){return SendMessageW(uiItem(parent,id),message,wp,lp);}
BOOL uiSetText(HWND parent,int id,const wchar_t* text){return SetWindowTextW(uiItem(parent,id),text);}
UINT uiGetText(HWND parent,int id,wchar_t* text,int count){return static_cast<UINT>(GetWindowTextW(uiItem(parent,id),text,count));}
LRESULT CALLBACK settingsViewportProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_PRINT||m==WM_PRINTCLIENT){
        // PrintWindow's recursive child rendering does not always inherit the
        // viewport's desktop visible region. Keep scrolled children out of the
        // fixed header/footer in print DCs as well as on the real desktop.
        HDC dc=reinterpret_cast<HDC>(wp);const int saved=SaveDC(dc);RECT client{};GetClientRect(w,&client);IntersectClipRect(dc,client.left,client.top,client.right,client.bottom);
        const LRESULT result=DefWindowProcW(w,m,wp,lp);if(saved)RestoreDC(dc,saved);return result;
    }
    switch(m){
    case WM_COMMAND:case WM_DRAWITEM:case WM_MEASUREITEM:case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:
    case WM_VSCROLL:case WM_MOUSEWHEEL:return SendMessageW(GetParent(w),m,wp,lp);
    case WM_ERASEBKGND:{RECT r{};GetClientRect(w,&r);FillRect(reinterpret_cast<HDC>(wp),&r,g->backgroundBrush?g->backgroundBrush:GetSysColorBrush(COLOR_WINDOW));return 1;}
    case WM_PAINT:{PAINTSTRUCT paint{};HDC dc=BeginPaint(w,&paint);RECT r{};GetClientRect(w,&r);const int dpi=static_cast<int>(uiDpi(w));const auto page=ui::settingsPage(g->settingsTab,r.right,r.bottom,dpi,g->settingsScrollDip);HPEN pen=CreatePen(PS_SOLID,1,g->borderColor);HGDIOBJ old=SelectObject(dc,pen);for(const auto& item:page.items)if(item.id>=settingGeneralTitle&&item.id<=settingAdvancedTitle){const int y=item.rect.y+ui::scale(36,dpi);MoveToEx(dc,ui::scale(24,dpi),y,nullptr);LineTo(dc,r.right-ui::scale(24,dpi),y);}SelectObject(dc,old);DeleteObject(pen);EndPaint(w,&paint);return 0;}
    }
    return DefWindowProcW(w,m,wp,lp);
}
LRESULT CALLBACK nativeComboProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
LRESULT CALLBACK nativeEditProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
LRESULT CALLBACK richPaneProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
LRESULT CALLBACK hoverButtonProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
LRESULT CALLBACK nativeScrollProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
LRESULT CALLBACK tooltipCleanupProc(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR owner){
    if(m==WM_NCDESTROY&&g&&g->tooltip&&IsWindow(g->tooltip)){TOOLINFOW tool{sizeof(tool)};tool.hwnd=reinterpret_cast<HWND>(owner);tool.uId=reinterpret_cast<UINT_PTR>(h);SendMessageW(g->tooltip,TTM_DELTOOLW,0,reinterpret_cast<LPARAM>(&tool));}
    return DefSubclassProc(h,m,wp,lp);
}
BOOL CALLBACK styleNativeControl(HWND child,LPARAM){
    wchar_t name[40]{};GetClassNameW(child,name,40);
    // Multiline EDIT keeps its native frame/formatting rectangle. Only the
    // single-line EDIT uses centered client padding below.
    if(_wcsicmp(name,L"EDIT")==0&&(GetWindowLongPtrW(child,GWL_STYLE)&ES_MULTILINE))return TRUE;
    if(_wcsicmp(name,L"COMBOBOX")==0){SetWindowSubclass(child,nativeComboProc,11,0);const LONG_PTR ex=GetWindowLongPtrW(child,GWL_EXSTYLE);if(ex&(WS_EX_STATICEDGE|WS_EX_CLIENTEDGE)){SetWindowLongPtrW(child,GWL_EXSTYLE,ex&~(WS_EX_STATICEDGE|WS_EX_CLIENTEDGE));SetWindowPos(child,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);}const int dpi=static_cast<int>(uiDpi(child));SendMessageW(child,CB_SETITEMHEIGHT,0,MulDiv(30,dpi,96));SendMessageW(child,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),MulDiv(30,dpi,96));}
    if(_wcsicmp(name,L"EDIT")==0){SetWindowSubclass(child,nativeEditProc,10,0);const LONG_PTR ex=GetWindowLongPtrW(child,GWL_EXSTYLE);SetWindowLongPtrW(child,GWL_EXSTYLE,ex&~WS_EX_CLIENTEDGE);SetWindowPos(child,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);const int margin=ui::scale(8,static_cast<int>(uiDpi(child)));SendMessageW(child,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(margin,margin));}
    if(_wcsicmp(name,L"BUTTON")==0)SetWindowSubclass(child,hoverButtonProc,7,0);
    return TRUE;
}
BOOL CALLBACK dismissDroppedCombo(HWND child,LPARAM state){wchar_t name[40]{};GetClassNameW(child,name,40);if(_wcsicmp(name,L"COMBOBOX")==0&&SendMessageW(child,CB_GETDROPPEDSTATE,0,0)){SendMessageW(child,CB_SHOWDROPDOWN,FALSE,0);*reinterpret_cast<bool*>(state)=true;}return TRUE;}
bool closeAnyDroppedCombo(){bool closed=false;for(HWND root:{g->window,g->settings})if(root)EnumChildWindows(root,dismissDroppedCombo,reinterpret_cast<LPARAM>(&closed));return closed;}
bool nativePageNavigation(HWND child){wchar_t name[40]{};GetClassNameW(child,name,40);return _wcsicmp(name,L"EDIT")==0||_wcsicmp(name,L"COMBOBOX")==0||_wcsicmp(name,L"ComboLBox")==0||_wcsicmp(name,L"SCROLLBAR")==0||_wcsicmp(name,MSFTEDIT_CLASS)==0;}
void uiWindowLimits(HWND w,MINMAXINFO& limits,int minWidth,int minHeight){MONITORINFO monitor{sizeof(monitor)};const int dpi=static_cast<int>(uiDpi(w));if(GetMonitorInfoW(MonitorFromWindow(w,MONITOR_DEFAULTTONEAREST),&monitor)){const LONG width=monitor.rcWork.right-monitor.rcWork.left,height=monitor.rcWork.bottom-monitor.rcWork.top;limits.ptMinTrackSize={std::min<LONG>(ui::scale(minWidth,dpi),width),std::min<LONG>(ui::scale(minHeight,dpi),height)};limits.ptMaxPosition={monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top};limits.ptMaxSize={width,height};}else limits.ptMinTrackSize={ui::scale(minWidth,dpi),ui::scale(minHeight,dpi)};}
LRESULT uiHitTest(HWND window,LPARAM pointParam,int dragHeight,int rightButtons){
    POINT point{GET_X_LPARAM(pointParam),GET_Y_LPARAM(pointParam)};ScreenToClient(window,&point);RECT client{};GetClientRect(window,&client);const int dpi=static_cast<int>(uiDpi(window)),edge=ui::scale(6,dpi);
    if(!IsZoomed(window)){const bool left=point.x<edge,right=point.x>=client.right-edge,top=point.y<edge,bottom=point.y>=client.bottom-edge;if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;if(left)return HTLEFT;if(right)return HTRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;}
    return point.y<ui::scale(dragHeight,dpi)&&point.x<client.right-ui::scale(rightButtons,dpi)?HTCAPTION:HTCLIENT;
}
void scheduleTaskReaper() noexcept {
    if(!g)return;
    if(g->retiredTasks.empty()){
        if(g->taskReaperWindow&&IsWindow(g->taskReaperWindow))KillTimer(g->taskReaperWindow,taskReaperTimerId);
        g->taskReaperWindow=nullptr;return;
    }
    HWND owner=g->window?g->window:g->settings;if(!owner)return;if(g->taskReaperWindow==owner)return;
    if(g->taskReaperWindow&&IsWindow(g->taskReaperWindow))KillTimer(g->taskReaperWindow,taskReaperTimerId);
    g->taskReaperWindow=nullptr;
#if defined(SAT_ENABLE_TEST_HOOKS)
    if(g->forceReaperTimerFailureForTest)return;
#endif
    if(SetTimer(owner,taskReaperTimerId,25,nullptr))g->taskReaperWindow=owner;
}
void reapRetiredTasks(bool fromTimer=false) noexcept {
    if(!g)return;
    auto it=g->retiredTasks.begin();while(it!=g->retiredTasks.end()){
        if((*it)->finished()){(*it)->join();it=g->retiredTasks.erase(it);
#if defined(SAT_ENABLE_TEST_HOOKS)
            if(fromTimer)++g->timerReapedTasksForTest;
#else
            (void)fromTimer;
#endif
        }else ++it;
    }
    scheduleTaskReaper();
}
#if defined(SAT_ENABLE_TEST_HOOKS)
LRESULT handleTestReaperControl(WPARAM command,LPARAM value) noexcept {
    if(!g)return FALSE;
    if(command==testQueryRetiredTasks)return static_cast<LRESULT>(g->retiredTasks.size());
    if(command==testQueryTimerReapedTasks)return static_cast<LRESULT>(g->timerReapedTasksForTest);
    if(command==testSuppressEventReaping){g->suppressEventReapingForTest=value!=0;return TRUE;}
    if(command==testForceReaperTimerFailure){if(g->taskReaperWindow){KillTimer(g->taskReaperWindow,taskReaperTimerId);g->taskReaperWindow=nullptr;}g->forceReaperTimerFailureForTest=value!=0;scheduleTaskReaper();return TRUE;}
    return FALSE;
}
#endif
void retireTask(std::shared_ptr<TranslationTask>& task){
    if(!task)return;task->cancel();if(task->finished()){task->join();task.reset();return;}
    try{g->retiredTasks.push_back(task);}catch(...){task->join();task.reset();throw;}
    task.reset();scheduleTaskReaper();
}
void deleteFonts(FontSet& set){for(HFONT font:{set.body,set.caption,set.emphasis,set.note})if(font)DeleteObject(font);set={};}
FontSet makeFontSet(UINT dpi){
    using namespace ui::typography;FontSet set;set.dpi=dpi;
    set.body=create(Role::body,dpi);set.caption=create(Role::title,dpi);set.emphasis=create(Role::heading,dpi);set.note=create(Role::note,dpi);
    if(!set.body||!set.caption||!set.emphasis||!set.note)deleteFonts(set);return set;
}
HWND fontOwner(HWND window){
    // Settings controls live under a viewport, help text is a child HWND.
    const HWND root=GetAncestor(window,GA_ROOT);
    return root==g->settings?g->settings:root==g->help?g->help:root==g->ocrConfirmWnd?g->ocrConfirmWnd:g->window;
}
FontSet& fontSetFor(HWND window){const HWND root=fontOwner(window);return root==g->settings?g->settingsFonts:root==g->help?g->helpFonts:root==g->ocrConfirmWnd?g->ocrFonts:g->mainFonts;}
ui::typography::Role textRole(HWND parent,int id){
    using ui::typography::Role;const HWND root=fontOwner(parent);
    if(root==g->settings){if(id==settingHeader)return Role::title;if(id>=settingGeneralTitle&&id<=settingAdvancedTitle)return Role::heading;if(id>=2000||id==settingStatus)return Role::note;}
    else if(root==g->help){if(id==settingHeader)return Role::title;}
    else {if(id==titleLabel)return Role::title;if(id==sourceCaptionLabel||id==resultCaptionLabel||id==ocrCandidateTitle)return Role::heading;if(id==statusLabel||id==promptStatusLabel||id==ocrCandidateNotice)return Role::note;}
    return Role::body;
}
HFONT roleFont(FontSet& set,ui::typography::Role role){using ui::typography::Role;switch(role){case Role::title:return set.caption;case Role::heading:return set.emphasis;case Role::note:return set.note;default:return set.body;}}
void refreshFonts(HWND window);
HFONT fontFor(HWND window,bool caption=false){FontSet& set=fontSetFor(window);const UINT dpi=uiDpi(fontOwner(window));if(!set.body){set=makeFontSet(dpi);if(!set.body)throw std::runtime_error("DPI font creation failed");}else if(set.dpi!=dpi)refreshFonts(fontOwner(window));return caption?set.caption:set.body;}
HFONT fontForRole(HWND window,ui::typography::Role role){(void)fontFor(window);return roleFont(fontSetFor(window),role);}
void applyHelpHeadings(HWND window);
BOOL CALLBACK applyChildFont(HWND child,LPARAM parameter){HWND root=reinterpret_cast<HWND>(parameter);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(roleFont(fontSetFor(root),textRole(root,GetDlgCtrlID(child)))),TRUE);return TRUE;}
void refreshFonts(HWND window){
    FontSet& set=fontSetFor(window);const UINT dpi=uiDpi(fontOwner(window));if(set.body&&set.dpi==dpi)return;
    FontSet next=makeFontSet(dpi);if(!next.body)return;FontSet previous=set;set=next;
    EnumChildWindows(window,applyChildFont,reinterpret_cast<LPARAM>(window));
    if(window==g->help)applyHelpHeadings(window);
    deleteFonts(previous);
}
void postTranslationEvent(const std::weak_ptr<UiEventQueue>& weak,TranslationEvent event){
    auto q=weak.lock();if(!q)return;std::lock_guard lock(q->mutex);if(!q->alive)return;const bool notify=q->events.empty();
    if(!notify&&event.kind==TranslationEvent::Kind::delta&&q->events.back().kind==TranslationEvent::Kind::delta&&event.requestId==q->events.back().requestId&&event.revision==q->events.back().revision&&event.sourceSessionId==q->events.back().sourceSessionId&&event.testConnection==q->events.back().testConnection)q->events.back().text+=event.text;
    else q->events.push_back(std::move(event));
    if(notify&&q->window)PostMessageW(q->window,msgNetworkChunk,0,0);
}
void postTaskFinished(const std::weak_ptr<UiEventQueue>& weak) noexcept {try{auto q=weak.lock();if(!q)return;std::lock_guard lock(q->mutex);if(q->alive&&q->window)PostMessageW(q->window,msgNetworkDone,0,0);}catch(...){}}
void stopMainRequest(){if(g)retireTask(g->task);}
void stopTestRequest(){if(g){retireTask(g->connectionTask);g->connectionTestActive=false;}}
int px(HWND w,int dip){return MulDiv(dip,static_cast<int>(uiDpi(w)),96);}
std::wstring trimCopy(std::wstring s){while(!s.empty()&&(s.back()==L'\r'||s.back()==L'\n'||s.back()==L' '||s.back()==L'\t'))s.pop_back();size_t p=0;while(p<s.size()&&(s[p]==L' '||s[p]==L'\t'||s[p]==L'\r'||s[p]==L'\n'))++p;s.erase(0,p);return s;}
bool parseUnsignedSetting(std::wstring_view text,unsigned minimum,unsigned maximum,unsigned& value) noexcept {
    if(text.empty()||minimum>maximum)return false;unsigned parsed=0;
    for(const wchar_t c:text){if(c<L'0'||c>L'9')return false;const unsigned digit=static_cast<unsigned>(c-L'0');if(parsed>maximum/10||(parsed==maximum/10&&digit>maximum%10))return false;parsed=parsed*10+digit;}
    if(parsed<minimum)return false;value=parsed;return true;
}
bool validText(const std::wstring& s){if(s.size()>maxTextBytes/sizeof(wchar_t))return false;try{(void)json::encode(s);}catch(...){return false;}for(wchar_t c:s)if(c==0||(c<0x20&&c!=L'\r'&&c!=L'\n'&&c!=L'\t'))return false;return true;}
std::wstring getText(HWND h){const LRESULT n=GetWindowTextLengthW(h);if(n<0||n>static_cast<LRESULT>(maxTextBytes/sizeof(wchar_t)))throw std::runtime_error("text length outside bounds");std::wstring s(static_cast<size_t>(n)+1,L'\0');const int got=GetWindowTextW(h,s.data(),static_cast<int>(s.size()));if(got<0)throw std::runtime_error("text read failed");s.resize(static_cast<size_t>(got));return s;}
void setText(HWND h,const std::wstring& s){if(s.size()>static_cast<size_t>(INT_MAX))throw std::runtime_error("text too long");SetWindowTextW(h,s.c_str());}
void appendRichEditPreservingView(HWND edit,const std::wstring& addition){
    if(addition.empty())return;DWORD selectionStart=0,selectionEnd=0;SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&selectionStart),reinterpret_cast<LPARAM>(&selectionEnd));POINT scroll{};SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));const bool wasReadOnly=(GetWindowLongPtrW(edit,GWL_STYLE)&ES_READONLY)!=0;if(wasReadOnly)SendMessageW(edit,EM_SETREADONLY,FALSE,0);const auto end=SendMessageW(edit,WM_GETTEXTLENGTH,0,0);SendMessageW(edit,EM_SETSEL,static_cast<WPARAM>(end),static_cast<LPARAM>(end));SendMessageW(edit,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(addition.c_str()));if(wasReadOnly)SendMessageW(edit,EM_SETREADONLY,TRUE,0);SendMessageW(edit,EM_SETSEL,selectionStart,selectionEnd);SendMessageW(edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
}
HWND control(DWORD ex,const wchar_t* cls,const wchar_t* text,DWORD style,int id,HWND parent){HWND h=CreateWindowExW(ex,cls,text,WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|style,0,0,10,10,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),g->instance,nullptr);if(!h)throw std::runtime_error("native control creation failed");SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(fontForRole(parent,textRole(parent,id))),TRUE);return h;}
void refreshTooltipFont(HWND h,UINT dpi){
        HFONT next=ui::typography::create(ui::typography::Role::note,dpi);
        if(next){HFONT old=reinterpret_cast<HFONT>(GetPropW(h,L"SAT.TooltipFont"));if(SetPropW(h,L"SAT.TooltipFont",next)){SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);if(old)DeleteObject(old);}else DeleteObject(next);}
        SendMessageW(h,TTM_SETMAXTIPWIDTH,0,MulDiv(350,static_cast<int>(dpi),96));
}
LRESULT CALLBACK tooltipFontProc(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if((message==WM_SHOWWINDOW&&wp)||message==WM_DPICHANGED)refreshTooltipFont(h,message==WM_DPICHANGED?LOWORD(wp):uiDpi(h));
    const auto result=DefSubclassProc(h,message,wp,lp);
    if(message==WM_NCDESTROY){if(auto font=RemovePropW(h,L"SAT.TooltipFont"))DeleteObject(font);}
    return result;
}
void tooltip(HWND h,const wchar_t* text){if(!g->tooltip){g->tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,g->window,nullptr,g->instance,nullptr);if(!g->tooltip)throw std::runtime_error("tooltip creation failed");if(!SetWindowSubclass(g->tooltip,tooltipFontProc,15,0))throw std::runtime_error("tooltip typography initialization failed");refreshTooltipFont(g->tooltip,uiDpi(g->window));}
    TOOLINFOW ti{sizeof(ti)};ti.uFlags=TTF_IDISHWND|TTF_SUBCLASS;ti.hwnd=GetAncestor(h,GA_ROOT);ti.uId=reinterpret_cast<UINT_PTR>(h);ti.lpszText=const_cast<wchar_t*>(text);SendMessageW(g->tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&ti));SetWindowSubclass(h,tooltipCleanupProc,9,reinterpret_cast<DWORD_PTR>(ti.hwnd));}
LRESULT CALLBACK hoverButtonProc(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    // Dialog default-button updates must not replace an owner-draw type.
    if(m==BM_SETSTYLE&&(GetWindowLongPtrW(h,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW)wp=(wp&~static_cast<WPARAM>(BS_TYPEMASK))|BS_OWNERDRAW;
    if(m==WM_MOUSEMOVE&&!GetPropW(h,L"SAT.Hot")){SetPropW(h,L"SAT.Hot",reinterpret_cast<HANDLE>(1));TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);InvalidateRect(h,nullptr,FALSE);}
    if(m==WM_MOUSELEAVE||m==WM_CANCELMODE||m==WM_ENABLE||m==WM_NCDESTROY){RemovePropW(h,L"SAT.Hot");if(m!=WM_NCDESTROY)InvalidateRect(h,nullptr,FALSE);}
    if(m==WM_SETFOCUS||m==WM_KILLFOCUS||m==WM_UPDATEUISTATE||m==WM_THEMECHANGED)InvalidateRect(h,nullptr,FALSE);
    return DefSubclassProc(h,m,wp,lp);
}
HWND button(const wchar_t* label,int id,const wchar_t* tip){auto h=control(0,L"BUTTON",label,WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY,id,g->window);SetWindowSubclass(h,hoverButtonProc,7,0);tooltip(h,tip);return h;}
void status(const std::wstring& s){g->status=s;if(g->window){HWND h=uiItem(g->window,statusLabel);if(h)setText(h,s);}}
void revealMainWindow(bool forceVisible=false){
    if(!g||!g->window||(!forceVisible&&testMode()&&!showTestUi()))return;
    if(GetCurrentThreadId()!=GetWindowThreadProcessId(g->window,nullptr)){traceCapture("reveal_rejected","wrong_thread");return;}
    ShowWindow(g->window,SW_RESTORE);
    // Explicit visibility also works when the launcher supplied SW_HIDE.
    if(!SetWindowPos(g->window,g->config.alwaysOnTop?HWND_TOPMOST:HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW))traceCapture("reveal_failed","set_window_pos",GetLastError());
    UpdateWindow(g->window);traceCapture("main_revealed",IsWindowVisible(g->window)?"visible":"still_hidden");
    if(!testMode()||showTestUi()){
        SetForegroundWindow(g->window);
        if(GetForegroundWindow()!=g->window){
            const HWND foreground=GetForegroundWindow();const DWORD thread=foreground?GetWindowThreadProcessId(foreground,nullptr):0,current=GetCurrentThreadId();
            // OCR ends in another app's input queue. Attach only while activating,
            // then restore the separate queues immediately; no polling or delay.
            if(thread&&thread!=current&&AttachThreadInput(current,thread,TRUE)){
                BringWindowToTop(g->window);SetForegroundWindow(g->window);AttachThreadInput(current,thread,FALSE);
            }
        }
        const bool activated=GetForegroundWindow()==g->window;traceCapture("main_activation",activated?"activated":"foreground_denied");
        if(!activated){FLASHWINFO flash{sizeof(flash),g->window,FLASHW_TRAY|FLASHW_TIMERNOFG,3,0};FlashWindowEx(&flash);}
    }
}
bool copyClipboard(const std::wstring& text){if(!validText(text)||text.empty())return false;const SIZE_T bytes=(text.size()+1)*sizeof(wchar_t);HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE,bytes);if(!h)return false;void* p=GlobalLock(h);if(!p){GlobalFree(h);return false;}memcpy(p,text.c_str(),bytes);GlobalUnlock(h);if(!OpenClipboard(g->window)){GlobalFree(h);return false;}struct Close{~Close(){CloseClipboard();}}close;if(!EmptyClipboard()){GlobalFree(h);return false;}if(!SetClipboardData(CF_UNICODETEXT,h)){GlobalFree(h);return false;}return true;}
void updatePanes(){if(!g->window)return;HWND source=uiItem(g->window,sourceEdit);setText(source,g->source);SendMessageW(source,EM_SETREADONLY,!g->editing,0);EnableWindow(source,TRUE);setText(uiItem(g->window,resultEdit),g->result);ShowWindow(uiItem(g->window,resultEdit),g->editing?SW_HIDE:SW_SHOW);ShowWindow(uiItem(g->window,resultCaptionLabel),g->editing?SW_HIDE:SW_SHOW);InvalidateRect(source,nullptr,FALSE);InvalidateRect(uiItem(g->window,resultEdit),nullptr,FALSE);InvalidateRect(g->window,nullptr,TRUE);}
const TextVersion* completedVersion(){
    for(const auto& v:g->versions)if(v.versionId==g->currentVersionId&&v.completed&&v.sourceSnapshot==g->source&&v.targetLanguage==g->language&&v.resultText==g->lastCompletedResult)return &v;
    return nullptr;
}
void clearRetryMetadata(){g->retryAvailable=false;g->lastErrorDetails.clear();g->retryTargetLanguage.clear();g->activeOpType=AiOperationType::translate;g->activeRewriteStyle=RewriteStyle::natural;g->activeCustomScope=CustomPromptScope::sourceAndResult;g->activeCustomPrompt.clear();}
void clearCompleted(){g->currentVersionId=0;g->lastCompletedResult.clear();g->lastCompletedLanguage.clear();clearRetryMetadata();}
void invalidateCompletedAssociation(){g->currentVersionId=0;}
void selectTargetLanguage(const std::wstring& language){
    g->language=language;g->config.targetLanguage=language;
    const wchar_t* codes[]={L"zh-Hans",L"zh-Hant",L"en",L"ja",L"ko",L"fr",L"de",L"es"};
    for(int i=0;i<8;++i)if(language==codes[i])uiMessage(g->window,targetCombo,CB_SETCURSEL,i,0);
}
void recoverCompleted(bool restoreAssociation=false){
    g->result=g->lastCompletedResult;setText(uiItem(g->window,resultEdit),g->result);
    if(restoreAssociation&&!g->lastCompletedResult.empty()){
        for(auto it=g->versions.rbegin();it!=g->versions.rend();++it)if(it->completed&&it->sourceSnapshot==g->source&&it->targetLanguage==g->lastCompletedLanguage&&it->resultText==g->lastCompletedResult){
            selectTargetLanguage(it->targetLanguage);g->currentVersionId=it->versionId;return;
        }
    }
}
void cancelOperation(bool restoreAssociation=false){++g->revision;g->activeRequestId=0;g->requestActive=false;stopMainRequest();recoverCompleted(restoreAssociation);}
void updateButtons(){
    if(!g->window)return;
    ShowWindow(uiItem(g->window,captureButton),!g->editing?SW_SHOW:SW_HIDE);
    ShowWindow(uiItem(g->window,sourceCopyButton),g->config.showSourceCopy?SW_SHOW:SW_HIDE);
    ShowWindow(uiItem(g->window,resultCopyButton),(!g->editing&&g->config.showResultCopy)?SW_SHOW:SW_HIDE);
    ShowWindow(uiItem(g->window,unlockButton),g->config.showUnlock&&!g->editing?SW_SHOW:SW_HIDE);
    ShowWindow(uiItem(g->window,restoreButton),g->config.showRestore&&g->editing?SW_SHOW:SW_HIDE);
    ShowWindow(uiItem(g->window,cancelEditButton),g->editing?SW_SHOW:SW_HIDE);
    ShowWindow(uiItem(g->window,translateButton),g->config.showRetranslate||g->editing?SW_SHOW:SW_HIDE);
    ShowWindow(uiItem(g->window,requestCancelButton),g->requestActive?SW_SHOW:SW_HIDE);
    EnableWindow(uiItem(g->window,translateButton),g->editing||!g->source.empty());
    ShowWindow(uiItem(g->window,mainTranslateButton),SW_SHOW);
    EnableWindow(uiItem(g->window,mainTranslateButton),!g->requestActive&&SendMessageW(uiItem(g->window,sourceEdit),WM_GETTEXTLENGTH,0,0)>0);
    setText(uiItem(g->window,mainTranslateButton),g->requestActive?L"翻译中":L"翻译");

    const bool allowAi=!g->editing&&!g->requestActive;
    ShowWindow(uiItem(g->window,rewriteButton),(!g->editing&&g->config.showRewrite)?SW_SHOW:SW_HIDE);
    ShowWindow(uiItem(g->window,deAiButton),(!g->editing&&g->config.showDeAi)?SW_SHOW:SW_HIDE);
    ShowWindow(uiItem(g->window,customPromptButton),(!g->editing&&g->config.showCustomPrompt)?SW_SHOW:SW_HIDE);
    ShowWindow(uiItem(g->window,versionButton),(!g->editing&&g->config.showVersionHistory)?SW_SHOW:SW_HIDE);
    EnableWindow(uiItem(g->window,rewriteButton),allowAi&&completedVersion()!=nullptr);
    EnableWindow(uiItem(g->window,deAiButton),allowAi&&completedVersion()!=nullptr);
    EnableWindow(uiItem(g->window,customPromptButton),allowAi&&(!g->source.empty()||!g->result.empty()));
    EnableWindow(uiItem(g->window,versionButton),allowAi&&!g->versions.empty());
    ShowWindow(uiItem(g->window,retryButton),g->retryAvailable&&!g->requestActive?SW_SHOW:SW_HIDE);
    ShowWindow(uiItem(g->window,errorDetailsButton),!g->lastErrorDetails.empty()?SW_SHOW:SW_HIDE);
    for(int id:g->overflowActions)ShowWindow(uiItem(g->window,id),SW_HIDE);
}
HICON loadAppIcon(int width,int height){return static_cast<HICON>(LoadImageW(g->instance,MAKEINTRESOURCEW(IDI_TRANSLATOR_APP),IMAGE_ICON,width,height,LR_DEFAULTCOLOR|LR_SHARED));}
void applyAppIcons(HWND hwnd){const HICON iconSmall=loadAppIcon(GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON));const HICON iconLarge=loadAppIcon(GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON));if(iconSmall)SendMessageW(hwnd,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(iconSmall));if(iconLarge)SendMessageW(hwnd,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(iconLarge));}
void drawTitleMark(HDC dc,HWND hwnd,int xDip=24,int yDip=15,int sizeDip=28){const int dpi=static_cast<int>(uiDpi(hwnd)),size=ui::scale(sizeDip,dpi);if(const HICON icon=loadAppIcon(size,size))DrawIconEx(dc,ui::scale(xDip,dpi),ui::scale(yDip,dpi),icon,size,size,0,nullptr,DI_NORMAL);}
std::wstring errorDetailsText(const TranslationEvent& event){
    std::wstring detail=L"代码："+(event.errorCode.empty()?std::wstring(L"request.failed"):event.errorCode);
    detail+=L"\r\n阶段：";switch(event.phase){case TranslationPhase::setup:detail+=L"准备";break;case TranslationPhase::send:detail+=L"发送";break;case TranslationPhase::headers:detail+=L"响应头";break;case TranslationPhase::body:detail+=L"响应内容";break;case TranslationPhase::complete:detail+=L"完成";break;}
    if(event.httpStatus)detail+=L"\r\nHTTP 状态："+std::to_wstring(event.httpStatus);
    if(event.networkError)detail+=L"\r\nWinHTTP 错误："+std::to_wstring(event.networkError);
    if(event.responseIsStream)detail+=L"\r\n响应类型：SSE";
    if(event.maxOutputTokens)detail+=L"\r\n输出 Token 上限："+std::to_wstring(event.maxOutputTokens);
    detail+=L"\r\n思考模式："+(event.thinkingMode==ThinkingMode::enabled?std::wstring(L"开启"):event.thinkingMode==ThinkingMode::disabled?std::wstring(L"关闭"):std::wstring(L"自动"));
    if(event.streamFallbackBlocked)detail+=L"\r\n兼容回退：请求次数上限已阻止非流式重试";
    return detail;
}
void showErrorDetails(HWND owner,const std::wstring& detail){if(!detail.empty())MessageBoxW(owner,detail.c_str(),L"错误详情 · Translator",MB_OK|MB_ICONINFORMATION);}
void retryLastOperation();
bool imeComposing(HWND edit){
    if(!edit)return false;
    if(GetPropW(edit,L"SAT.IME.Composing"))return true;
    HIMC context=ImmGetContext(edit);if(!context)return false;
    const bool composing=ImmGetCompositionStringW(context,GCS_COMPSTR,nullptr,0)>0;
    ImmReleaseContext(edit,context);return composing;
}
void trackImeMessage(HWND edit,UINT message){
    if(message==WM_IME_STARTCOMPOSITION)SetPropW(edit,L"SAT.IME.Composing",reinterpret_cast<HANDLE>(1));
    if(message==WM_IME_ENDCOMPOSITION||message==WM_NCDESTROY)RemovePropW(edit,L"SAT.IME.Composing");
}
LRESULT CALLBACK sourceImeSubclass(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    trackImeMessage(h,message);return DefSubclassProc(h,message,wp,lp);
}
void createMainControls(){
    auto title=control(0,L"STATIC",L"OCR AI Translator",SS_LEFT,titleLabel,g->window);SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(g->window,true)),TRUE);
    control(0,L"STATIC",L"自动识别",SS_LEFT|SS_CENTERIMAGE,sourceLanguageLabel,g->window);control(0,L"STATIC",L"→",SS_CENTER|SS_CENTERIMAGE,languageArrowLabel,g->window);
    auto combo=control(WS_EX_STATICEDGE,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,targetCombo,g->window);
    SendMessageW(combo,CB_SETITEMHEIGHT,0,px(g->window,30));SendMessageW(combo,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),px(g->window,30));
    const std::pair<const wchar_t*,const wchar_t*> languages[]={{L"简体中文",L"zh-Hans"},{L"繁體中文",L"zh-Hant"},{L"English",L"en"},{L"日本語",L"ja"},{L"한국어",L"ko"},{L"Français",L"fr"},{L"Deutsch",L"de"},{L"Español",L"es"}};
    for(size_t i=0;i<std::size(languages);++i){SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(languages[i].first));if(g->config.targetLanguage==languages[i].second)SendMessageW(combo,CB_SETCURSEL,i,0);}
    auto sourceCaption=control(0,L"STATIC",L"原文",SS_LEFT,sourceCaptionLabel,g->window);auto resultCaption=control(0,L"STATIC",L"译文",SS_LEFT,resultCaptionLabel,g->window);
    SendMessageW(sourceCaption,WM_SETFONT,reinterpret_cast<WPARAM>(g->mainFonts.emphasis),TRUE);SendMessageW(resultCaption,WM_SETFONT,reinterpret_cast<WPARAM>(g->mainFonts.emphasis),TRUE);
    const DWORD editorStyle=ES_MULTILINE|ES_AUTOVSCROLL|ES_NOHIDESEL|WS_VSCROLL|WS_TABSTOP;
    auto left=control(0,MSFTEDIT_CLASS,L"",editorStyle|ES_READONLY,sourceEdit,g->window);SendMessageW(left,EM_EXLIMITTEXT,0,maxTextBytes/sizeof(wchar_t));SendMessageW(left,EM_SETEVENTMASK,0,SendMessageW(left,EM_GETEVENTMASK,0,0)|ENM_CHANGE);
    if(!SetWindowSubclass(left,richPaneProc,13,0))throw std::runtime_error("source pane rendering initialization failed");
    if(!SetWindowSubclass(left,sourceImeSubclass,3,0))throw std::runtime_error("source IME tracking initialization failed");
    auto right=control(0,MSFTEDIT_CLASS,L"",editorStyle|ES_READONLY,resultEdit,g->window);SendMessageW(right,EM_EXLIMITTEXT,0,maxTextBytes/sizeof(wchar_t));if(!SetWindowSubclass(right,richPaneProc,13,0))throw std::runtime_error("result pane rendering initialization failed");
    button(L"截图翻译",captureButton,L"从当前 OCR 来源截图并识别文字");button(L"复制原文",sourceCopyButton,L"复制当前原文");button(L"复制译文",resultCopyButton,L"复制当前译文");button(L"解锁原文",unlockButton,L"解锁后在整宽区域编辑原文");button(L"恢复原文",restoreButton,L"恢复到会话中保存的原文");button(L"取消编辑",cancelEditButton,L"放弃未提交编辑并恢复编辑前原文");button(L"重新翻译",translateButton,L"重新提交当前原文和目标语言");button(L"取消请求",requestCancelButton,L"取消当前请求");
    button(L"AI 改写",rewriteButton,L"选择风格进行 AI 改写（自然、简洁、口语、正式、文艺）");
    button(L"自然化表达",deAiButton,L"减少翻译腔和模板化措辞，保留原意");
    button(L"自定义指令",customPromptButton,L"输入自定义指令处理原文或译文");
    button(L"版本历史",versionButton,L"查看并回退当前会话的译文版本");
    button(L"设置",settingsButton,L"模型与 API、OCR 来源和外观");
    button(L"更多",moreButton,L"窄窗口中收纳的操作");
    button(L"置顶",mainPin,L"切换浮窗置顶");button(L"最小化",mainMinimize,L"最小化窗口");button(L"关闭",mainClose,L"关闭翻译窗口");button(L"复制译文",footerCopy,L"复制当前完整译文（Ctrl+Shift+C）");
    button(L"重试",retryButton,L"使用当前操作和模型设置重试");button(L"查看详情",errorDetailsButton,L"查看错误代码、阶段与请求限制；不包含正文或密钥");
    button(L"翻译",mainTranslateButton,L"翻译当前原文；编辑后再次点击可提交新文本");
    control(0,L"STATIC",L"",SS_LEFT|SS_NOPREFIX|SS_ENDELLIPSIS,statusLabel,g->window);
    g->customPromptWnd=CreateWindowExW(WS_EX_CONTROLPARENT,L"SAT.CustomPrompt",L"",WS_CHILD|WS_BORDER|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,0,0,10,10,g->window,nullptr,g->instance,nullptr);
    updateButtons();
}
bool childVisible(HWND child){return child&&(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)!=0;}
void layoutMain(){
    if(!g->window)return;HWND w=g->window;RECT r{};GetClientRect(w,&r);const int dpi=static_cast<int>(uiDpi(w));auto d=[&](int v){return ui::scale(v,dpi);};
    const auto frame=ui::mainFrame(r.right,r.bottom,dpi,g->editing);const int margin=d(24),bh=d(36),gap=d(6);
    auto move=[&](int id,int x,int y,int width,int height){SetWindowPos(uiItem(w,id),nullptr,x,y,std::max(0,width),std::max(0,height),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS);};
    move(titleLabel,margin+d(36),d(16),d(320),d(32));
    move(mainClose,r.right-margin-bh,d(12),bh,bh);move(mainMinimize,r.right-margin-bh*2-gap,d(12),bh,bh);move(mainPin,r.right-margin-bh*3-gap*2,d(12),bh,bh);
    const auto language=ui::languageRow(r.right,dpi);
    for(const auto& placement:{std::pair{sourceLanguageLabel,language.source},std::pair{languageArrowLabel,language.arrow},std::pair{targetCombo,language.target},std::pair{mainTranslateButton,language.translate}}){const auto& box=placement.second;move(placement.first,box.x,box.y,box.width,box.height);}
    const int sourceLabelX=frame.sourceSurface.x+d(16),sourceActionsX=frame.sourceSurface.x+frame.sourceSurface.width-d(16)-2*bh-gap;
    const int resultLabelX=frame.resultSurface.x+d(16),resultActionX=frame.resultSurface.x+frame.resultSurface.width-d(16)-bh;
    move(sourceCaptionLabel,sourceLabelX,frame.captionY,std::max(0,sourceActionsX-sourceLabelX-d(8)),d(28));move(resultCaptionLabel,resultLabelX,frame.captionY,std::max(0,resultActionX-resultLabelX-d(8)),d(28));
    move(sourceEdit,frame.source.x,frame.source.y,frame.source.width,frame.source.height);move(resultEdit,frame.result.x,frame.result.y,frame.result.width,frame.result.height);
    const std::wstring displayedLanguage=!g->requestActive&&!g->lastCompletedResult.empty()&&g->result==g->lastCompletedResult&&!g->lastCompletedLanguage.empty()?g->lastCompletedLanguage:getText(uiItem(w,targetCombo));
    setText(uiItem(w,resultCaptionLabel),L"译文 · "+displayedLanguage);
    g->overflowActions.clear();updateButtons();
    move(sourceCopyButton,frame.sourceSurface.x+frame.sourceSurface.width-d(16)-bh,frame.captionY-d(6),bh,bh);move(unlockButton,frame.sourceSurface.x+frame.sourceSurface.width-d(16)-2*bh-gap,frame.captionY-d(6),bh,bh);move(resultCopyButton,resultActionX,frame.captionY-d(6),bh,bh);
    ShowWindow(uiItem(w,footerCopy),!g->editing&&g->config.showResultCopy?SW_SHOW:SW_HIDE);EnableWindow(uiItem(w,footerCopy),!g->result.empty());
    const int right=r.right-margin-bh,limitRight=right-bh-gap*2;int count=0;
    for(int id:g->config.toolbarOrder)if(id!=sourceCopyButton&&id!=resultCopyButton&&id!=unlockButton&&childVisible(uiItem(w,id)))++count;
    const bool overflow=count*bh+std::max(0,count-1)*gap>limitRight-margin;const int limit=limitRight-(overflow?bh+gap:0);int x=margin;
    for(int id:g->config.toolbarOrder){if(id==sourceCopyButton||id==resultCopyButton||id==unlockButton)continue;HWND h=uiItem(w,id);if(!childVisible(h))continue;if(x+bh<=limit){move(id,x,frame.toolbarY,bh,bh);x+=bh+gap;}else{ShowWindow(h,SW_HIDE);g->overflowActions.push_back(id);}}
    ShowWindow(uiItem(w,moreButton),g->overflowActions.empty()?SW_HIDE:SW_SHOW);if(!g->overflowActions.empty())move(moreButton,limit+gap,frame.toolbarY,bh,bh);
    move(footerCopy,right-bh-gap,frame.toolbarY,bh,bh);move(settingsButton,right,frame.toolbarY,bh,bh);
    const int detailWidth=d(80),retryWidth=d(64),actionGap=d(6),errorActionsWidth=g->lastErrorDetails.empty()?0:(g->retryAvailable?detailWidth+retryWidth+actionGap:detailWidth+actionGap);
    move(statusLabel,margin,frame.statusY,std::max(0,static_cast<int>(r.right)-margin*2-errorActionsWidth),d(24));
    move(retryButton,static_cast<int>(r.right)-margin-detailWidth-actionGap-retryWidth,frame.statusY-d(3),retryWidth,d(28));
    move(errorDetailsButton,static_cast<int>(r.right)-margin-detailWidth,frame.statusY-d(3),detailWidth,d(28));
    if(g->customPromptWnd&&IsWindow(g->customPromptWnd)&&childVisible(g->customPromptWnd)){const int panelW=std::clamp(static_cast<int>(r.right)-margin*2,d(360),d(560)),panelH=d(176);SetWindowPos(g->customPromptWnd,HWND_TOP,margin+(r.right-margin*2-panelW)/2,std::max(d(60),frame.source.y+frame.source.height-panelH-d(6)),panelW,panelH,SWP_NOACTIVATE|SWP_NOCOPYBITS);}
    RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
}
void setTheme(){
    if(!g->window)return;
    const Theme theme=g->previewTheme.value_or(g->config.theme);DWORD appsLight=1,bytes=sizeof(appsLight);
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&appsLight,&bytes);
    HIGHCONTRASTW contrast{sizeof(contrast)};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0);
    const unsigned resolved=resolveUiTheme(theme==Theme::dark?2u:theme==Theme::light?1u:0u,appsLight==0,(contrast.dwFlags&HCF_HIGHCONTRASTON)!=0);
    const bool explicitDark=resolved==2,explicitLight=resolved==1;
    const COLORREF bg=explicitDark?RGB(14,15,18):explicitLight?RGB(247,248,250):GetSysColor(COLOR_WINDOW);
    const COLORREF fg=explicitDark?RGB(241,243,245):explicitLight?RGB(28,34,43):GetSysColor(COLOR_WINDOWTEXT);
    const COLORREF controlBg=explicitDark?RGB(24,26,31):explicitLight?RGB(255,255,255):GetSysColor(COLOR_WINDOW);
    HBRUSH background=CreateSolidBrush(bg),control=CreateSolidBrush(controlBg);if(!background||!control){if(background)DeleteObject(background);if(control)DeleteObject(control);throw std::runtime_error("theme brush creation failed");}
#if defined(SAT_ENABLE_TEST_HOOKS)
    if(g->backgroundBrush&&!DeleteObject(g->backgroundBrush))++g->failedThemeBrushDeletes;
    if(g->controlBrush&&!DeleteObject(g->controlBrush))++g->failedThemeBrushDeletes;
#else
    if(g->backgroundBrush)DeleteObject(g->backgroundBrush);if(g->controlBrush)DeleteObject(g->controlBrush);
#endif
    g->backgroundBrush=background;g->controlBrush=control;g->backgroundColor=bg;g->foregroundColor=fg;g->controlBackgroundColor=controlBg;g->paneBackgroundColor=controlBg;g->borderColor=explicitDark?RGB(48,52,60):explicitLight?RGB(226,229,234):GetSysColor(COLOR_WINDOWFRAME);g->mutedColor=explicitDark?RGB(167,173,184):explicitLight?RGB(104,113,126):GetSysColor(COLOR_GRAYTEXT);
    for(int id:{sourceEdit,resultEdit}){HWND h=uiItem(g->window,id);if(h){SendMessageW(h,EM_SETBKGNDCOLOR,0,g->paneBackgroundColor);CHARFORMAT2W f{};f.cbSize=sizeof(f);f.dwMask=CFM_COLOR;f.crTextColor=fg;SendMessageW(h,EM_SETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&f));SendMessageW(h,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&f));}}
    const BOOL darkAttr=explicitDark;for(HWND window:{g->window,g->settings,g->help,g->ocrConfirmWnd})if(window){if(window!=g->ocrConfirmWnd)ui::useClientFrame(window);DwmSetWindowAttribute(window,20,&darkAttr,sizeof(darkAttr));EnumChildWindows(window,styleNativeControl,0);}
    if(g->help){HWND edit=uiItem(g->help,settingIntro);SendMessageW(edit,EM_SETBKGNDCOLOR,0,bg);CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_COLOR;format.crTextColor=fg;SendMessageW(edit,EM_SETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&format));SendMessageW(edit,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&format));RedrawWindow(g->help,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);}
    RedrawWindow(g->window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    if(g->settings)RedrawWindow(g->settings,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
}
LRESULT themedControlColor(UINT message,WPARAM dcParam,LPARAM childParam){
    if(!g||!g->backgroundBrush||!g->controlBrush)return 0;HDC dc=reinterpret_cast<HDC>(dcParam);HWND child=reinterpret_cast<HWND>(childParam);SetTextColor(dc,ui::typography::color(ui::typography::style(textRole(GetParent(child),GetDlgCtrlID(child))).ink,g->foregroundColor,g->mutedColor));
    if(message==WM_CTLCOLOREDIT||message==WM_CTLCOLORLISTBOX){wchar_t className[32]{};GetClassNameW(child,className,static_cast<int>(std::size(className)));const bool richEdit=std::wcscmp(className,MSFTEDIT_CLASS)==0;const COLORREF bg=richEdit?g->backgroundColor:g->controlBackgroundColor;SetBkMode(dc,OPAQUE);SetBkColor(dc,bg);return reinterpret_cast<LRESULT>(richEdit?g->backgroundBrush:g->controlBrush);}
    if(message==WM_CTLCOLORSTATIC){const int id=GetDlgCtrlID(child);const bool paneCaption=GetParent(child)==g->window&&(id==sourceCaptionLabel||id==resultCaptionLabel);const COLORREF background=paneCaption?g->paneBackgroundColor:g->backgroundColor;SetBkMode(dc,OPAQUE);SetBkColor(dc,background);return reinterpret_cast<LRESULT>(paneCaption?g->controlBrush:g->backgroundBrush);}
    SetBkMode(dc,TRANSPARENT);return reinterpret_cast<LRESULT>(g->backgroundBrush);
}
LRESULT CALLBACK settingsCheckboxProc(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==BM_SETCHECK){SetWindowLongPtrW(h,GWLP_USERDATA,wp==BST_CHECKED?BST_CHECKED:BST_UNCHECKED);InvalidateRect(h,nullptr,TRUE);return 0;}
    if(message==BM_GETCHECK)return GetWindowLongPtrW(h,GWLP_USERDATA);
    return DefSubclassProc(h,message,wp,lp);
}
void drawSettingsCheckbox(const DRAWITEMSTRUCT& item){
    const int dpi=static_cast<int>(uiDpi(item.hwndItem));const bool checked=GetWindowLongPtrW(item.hwndItem,GWLP_USERDATA)==BST_CHECKED;
    const float width=(item.rcItem.right-item.rcItem.left)*96.f/dpi,height=(item.rcItem.bottom-item.rcItem.top)*96.f/dpi;
    auto& p=ui::vectorPainter();if(!p.begin(item.hDC,item.rcItem,dpi))return;p.clear(g->backgroundColor);
    const bool disabled=!IsWindowEnabled(item.hwndItem);const COLORREF fill=checked?g->foregroundColor:g->controlBackgroundColor;
    p.round(2,4,width-4,height-8,(height-8)/2,disabled?g->controlBackgroundColor:fill,checked?fill:g->mutedColor);
    const float radius=(height-14)/2;p.dot(checked?width-5-radius:5+radius,height/2,radius,checked?g->backgroundColor:g->foregroundColor);
    if((item.itemState&ODS_FOCUS)&&!(item.itemState&ODS_NOFOCUSRECT)&&!(SendMessageW(item.hwndItem,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS))p.ring(1,1,width-2,height-2,7,RGB(104,144,205));p.end();
}
void openSettings();
void layoutSettings(HWND w);
bool darkSurface(){return (GetRValue(g->backgroundColor)*3+GetGValue(g->backgroundColor)*6+GetBValue(g->backgroundColor))/10<128;}
COLORREF blendColor(COLORREF a,COLORREF b,int amount){const int inverse=100-amount;return RGB((GetRValue(a)*inverse+GetRValue(b)*amount)/100,(GetGValue(a)*inverse+GetGValue(b)*amount)/100,(GetBValue(a)*inverse+GetBValue(b)*amount)/100);}
void drawActionButton(const DRAWITEMSTRUCT& item,int renderDpi=0){
    if(!g||item.CtlType!=ODT_BUTTON)return;const int dpi=renderDpi?renderDpi:static_cast<int>(uiDpi(item.hwndItem));const bool dark=darkSurface(),accent=item.CtlID==settingSave||item.CtlID==ocrCandidateUse||item.CtlID==promptExecuteBtn||item.CtlID==mainTranslateButton,danger=item.CtlID==requestCancelButton;
    const bool disabled=(item.itemState&ODS_DISABLED)!=0||!IsWindowEnabled(item.hwndItem),pressed=(item.itemState&ODS_SELECTED)!=0,hot=(item.itemState&ODS_HOTLIGHT)!=0||GetPropW(item.hwndItem,L"SAT.Hot")!=nullptr;
    const bool paneAction=GetParent(item.hwndItem)==g->window&&(item.CtlID==sourceCopyButton||item.CtlID==resultCopyButton||item.CtlID==unlockButton);
    const HWND parent=GetParent(item.hwndItem);const bool controlSurface=paneAction||parent==g->customPromptWnd||parent==g->ocrConfirmWnd;
    const COLORREF base=controlSurface?g->controlBackgroundColor:g->backgroundColor;
    FillRect(item.hDC,&item.rcItem,controlSurface?g->controlBrush:g->backgroundBrush);
    COLORREF fill=accent?(dark?RGB(72,130,235):RGB(48,105,220)):danger?(dark?RGB(145,57,61):RGB(190,53,59)):base;
    const bool iconButton=(GetParent(item.hwndItem)==g->window&&item.CtlID!=retryButton&&item.CtlID!=errorDetailsButton&&item.CtlID!=mainTranslateButton)||item.CtlID==settingHelp||item.CtlID==settingClose;
    const bool tab=item.CtlID>=settingTabGeneral&&item.CtlID<=settingTabAdvanced;
    const bool secondary=!iconButton&&!tab&&!accent&&!danger;
    COLORREF border=accent?fill:danger?fill:secondary?g->borderColor:base;COLORREF text=(accent||danger)?RGB(255,255,255):g->foregroundColor;
    if(disabled){fill=dark?RGB(40,44,50):RGB(239,242,246);border=dark?RGB(62,67,75):RGB(224,228,234);text=dark?RGB(132,139,149):RGB(151,158,168);}
    else if(pressed){fill=blendColor((accent||danger)?fill:base,dark?RGB(255,255,255):RGB(0,0,0),dark?12:7);border=fill;}else if(hot){fill=(accent||danger)?blendColor(fill,RGB(255,255,255),dark?12:9):blendColor(base,dark?RGB(255,255,255):RGB(0,0,0),dark?8:5);border=fill;}
    if(disabled&&GetParent(item.hwndItem)==g->window&&item.CtlID!=mainTranslateButton){fill=base;border=fill;}
    if(item.CtlID==mainPin&&g->config.alwaysOnTop){fill=g->controlBackgroundColor;border=RGB(104,144,205);}
    if(item.CtlID>=settingTabGeneral&&item.CtlID<=settingTabAdvanced){const bool selected=static_cast<int>(item.CtlID)-settingTabGeneral==g->settingsTab;fill=selected?(dark?RGB(43,49,59):RGB(231,236,244)):hot?(dark?RGB(35,38,45):RGB(239,242,247)):g->backgroundColor;text=g->foregroundColor;border=fill;}
    if(secondary&&!disabled)border=g->borderColor;
    const bool focus=(item.itemState&ODS_FOCUS)&&!(item.itemState&ODS_NOFOCUSRECT)&&!(SendMessageW(item.hwndItem,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS);
    RECT face=item.rcItem;const float width=(face.right-face.left)*96.f/dpi,height=(face.bottom-face.top)*96.f/dpi;
    auto& painter=ui::vectorPainter();bool vectorIcon=false;
    if(painter.begin(item.hDC,face,dpi)){
        painter.clear(base);
        painter.round(1.5f,1.5f,width-3.f,height-3.f,6.f,fill,border,secondary||accent||danger?1.f:0.f);
        if(focus)painter.ring(2.f,2.f,width-4.f,height-4.f,6.f,RGB(104,144,205),1.5f);
        if(iconButton)vectorIcon=painter.icon(static_cast<int>(item.CtlID),width,height,text);
        painter.end();
    }
    const int length=GetWindowTextLengthW(item.hwndItem);std::wstring label(static_cast<size_t>(std::max(0,length))+1,wchar_t{});if(length>0)GetWindowTextW(item.hwndItem,label.data(),length+1);label.resize(static_cast<size_t>(std::max(0,length)));
    RECT textRect=face;InflateRect(&textRect,-MulDiv(10,dpi,96),-MulDiv(2,dpi,96));const int saved=SaveDC(item.hDC);SelectObject(item.hDC,reinterpret_cast<HFONT>(SendMessageW(item.hwndItem,WM_GETFONT,0,0)));SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,text);if(!vectorIcon&&!iconButton)DrawTextW(item.hDC,label.c_str(),static_cast<int>(label.size()),&textRect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);if(saved)RestoreDC(item.hDC,saved);
}
void drawThemeComboItem(const DRAWITEMSTRUCT& item){
    if(!g||item.CtlType!=ODT_COMBOBOX)return;const bool dark=darkSurface();const bool listSelection=(item.itemState&ODS_SELECTED)!=0&&(item.itemState&ODS_COMBOBOXEDIT)==0;COLORREF fill=listSelection?(dark?RGB(65,111,194):RGB(51,105,210)):g->controlBackgroundColor;COLORREF text=listSelection?RGB(255,255,255):g->foregroundColor;if(item.itemState&ODS_DISABLED)text=dark?RGB(132,139,149):RGB(151,158,168);
    const int saved=SaveDC(item.hDC);SelectObject(item.hDC,reinterpret_cast<HFONT>(SendMessageW(item.hwndItem,WM_GETFONT,0,0)));
    HBRUSH surface=CreateSolidBrush(fill);if(surface){FillRect(item.hDC,&item.rcItem,surface);DeleteObject(surface);}else FillRect(item.hDC,&item.rcItem,g->controlBrush);
    if(item.itemID!=static_cast<UINT>(-1)){const LRESULT length=SendMessageW(item.hwndItem,CB_GETLBTEXTLEN,item.itemID,0);if(length>=0&&length<512){std::wstring label(static_cast<size_t>(length)+1,wchar_t{});SendMessageW(item.hwndItem,CB_GETLBTEXT,item.itemID,reinterpret_cast<LPARAM>(label.data()));label.resize(static_cast<size_t>(length));RECT textRect=item.rcItem;const int inset=MulDiv(10,static_cast<int>(uiDpi(item.hwndItem)),96);textRect.left+=(item.itemState&ODS_COMBOBOXEDIT)?std::max(0,inset-static_cast<int>(item.rcItem.left)):inset;textRect.right-=inset;SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,text);DrawTextW(item.hDC,label.c_str(),static_cast<int>(label.size()),&textRect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);}}
    if(saved)RestoreDC(item.hDC,saved);
}
void paintNativeCombo(HWND h,HDC dc){
    RECT r{};GetClientRect(h,&r);const int dpi=static_cast<int>(uiDpi(h));const int saved=SaveDC(dc);COMBOBOXINFO info{sizeof(info)};
    // Selected text stays in the native owner-draw item rectangle. Chrome must
    // never cover it or redraw a second, differently centered copy of the text.
    const bool nativeInfo=GetComboBoxInfo(h,&info)!=FALSE;
    if(nativeInfo)ExcludeClipRect(dc,info.rcItem.left,info.rcItem.top,info.rcItem.right,info.rcItem.bottom);
    auto& p=ui::vectorPainter();if(p.begin(dc,r,dpi)){p.clear(g->backgroundColor);p.round(.5f,.5f,r.right*96.f/dpi-1,r.bottom*96.f/dpi-1,6,g->controlBackgroundColor,GetFocus()==h?RGB(104,144,205):g->borderColor,GetFocus()==h?1.5f:1.f);p.end();}
    RECT arrow=nativeInfo?info.rcButton:RECT{r.right-MulDiv(24,dpi,96),MulDiv(2,dpi,96),r.right-MulDiv(2,dpi,96),r.bottom-MulDiv(2,dpi,96)};InflateRect(&arrow,-1,-1);ui::drawIcon(dc,arrow,1001,g->mutedColor,dpi,g->controlBackgroundColor);if(saved)RestoreDC(dc,saved);
}
LRESULT CALLBACK nativeComboProc(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    const LRESULT result=DefSubclassProc(h,m,wp,lp);
    if(m==WM_PAINT){HDC dc=GetDC(h);if(dc){paintNativeCombo(h,dc);ReleaseDC(h,dc);}}
    if(m==WM_PRINTCLIENT||m==WM_PRINT)paintNativeCombo(h,reinterpret_cast<HDC>(wp));
    if(m==WM_SETFOCUS||m==WM_KILLFOCUS||m==CB_SETCURSEL)InvalidateRect(h,nullptr,FALSE);
    return result;
}
void paintEditFrame(HWND h,HDC dc){
    RECT window{};GetWindowRect(h,&window);RECT r{0,0,window.right-window.left,window.bottom-window.top};
    RECT client{};GetClientRect(h,&client);POINT origin{};ClientToScreen(h,&origin);OffsetRect(&client,origin.x-window.left,origin.y-window.top);
    const int saved=SaveDC(dc);ExcludeClipRect(dc,client.left,client.top,client.right,client.bottom);
    const int dpi=static_cast<int>(uiDpi(h));auto& painter=ui::vectorPainter();
    if(painter.begin(dc,r,dpi)){const float w=r.right*96.f/dpi,height=r.bottom*96.f/dpi;painter.clear(g->backgroundColor);painter.round(.5f,.5f,w-1.f,height-1.f,6.f,g->controlBackgroundColor,GetFocus()==h?RGB(104,144,205):g->borderColor,GetFocus()==h?1.5f:1.f);painter.end();}
    if(saved)RestoreDC(dc,saved);
}
LRESULT CALLBACK nativeEditProc(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    const bool single=(GetWindowLongPtrW(h,GWL_STYLE)&ES_MULTILINE)==0;
    if(m==WM_NCCALCSIZE&&single){
        RECT& r=wp?reinterpret_cast<NCCALCSIZE_PARAMS*>(lp)->rgrc[0]:*reinterpret_cast<RECT*>(lp);
        HDC dc=GetDC(h);const auto font=reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0));const auto old=SelectObject(dc,font);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,old);ReleaseDC(h,dc);
        const int height=static_cast<int>(r.bottom-r.top),line=std::min(height,static_cast<int>(metrics.tmHeight)+1);
        const int top=std::max(0,(height-line)/2);r.top+=top;r.bottom=r.top+line;
        const int border=std::min(ui::scale(2,static_cast<int>(uiDpi(h))),std::max(0,static_cast<int>(r.right-r.left)/2));r.left+=border;r.right-=border;
        return wp?WVR_REDRAW:0;
    }
    if(m==WM_NCPAINT&&single){HDC dc=GetWindowDC(h);if(dc){paintEditFrame(h,dc);ReleaseDC(h,dc);}return 0;}
    // PRF_NONCLIENT is in window coordinates; native EDIT prints only its client.
    if(m==WM_PRINT&&single){HDC dc=reinterpret_cast<HDC>(wp);const int saved=SaveDC(dc);if(lp&PRF_NONCLIENT)paintEditFrame(h,dc);if(lp&PRF_CLIENT){RECT r{};GetWindowRect(h,&r);POINT origin{};ClientToScreen(h,&origin);OffsetViewportOrgEx(dc,origin.x-r.left,origin.y-r.top,nullptr);DefSubclassProc(h,WM_PRINTCLIENT,wp,lp);}if(saved)RestoreDC(dc,saved);return 0;}
    const LRESULT result=DefSubclassProc(h,m,wp,lp);
    if(m==WM_SETFONT&&single){SetWindowPos(h,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);const int margin=ui::scale(8,static_cast<int>(uiDpi(h)));SendMessageW(h,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(margin,margin));}
    if(m==WM_SETFOCUS||m==WM_KILLFOCUS||m==WM_THEMECHANGED)RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME);
    return result;
}
bool richPaneHintVisible(HWND h){
    if(!g||!g->window||!h||SendMessageW(h,WM_GETTEXTLENGTH,0,0)!=0)return false;
    const bool source=h==uiItem(g->window,sourceEdit);
    if((source&&GetFocus()==h)||imeComposing(h))return false;
    return true;
}
void paintRichPaneHint(HWND h,HDC dc){
    if(!richPaneHintVisible(h))return;
    const bool source=h==uiItem(g->window,sourceEdit);const int saved=SaveDC(dc);RECT r{};GetClientRect(h,&r);const int dpi=static_cast<int>(uiDpi(h)),inset=ui::scale(2,dpi);InflateRect(&r,-inset,-inset);
    SetBkMode(dc,TRANSPARENT);SelectObject(dc,g->mainFonts.emphasis);SetTextColor(dc,g->foregroundColor);
    const wchar_t* heading=source?(g->editing?L"输入或粘贴要翻译的文本":L"粘贴要翻译的文本"):L"译文将在此显示";
    RECT title{r.left,r.top+ui::scale(12,dpi),r.right,r.top+ui::scale(38,dpi)};DrawTextW(dc,heading,-1,&title,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    SelectObject(dc,g->mainFonts.body);SetTextColor(dc,g->mutedColor);RECT note{r.left,r.top+ui::scale(42,dpi),r.right,r.top+ui::scale(68,dpi)};
    DrawTextW(dc,source?L"Ctrl+V 粘贴，或在此直接输入":L"填入原文后即可开始翻译",-1,&note,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    if(saved)RestoreDC(dc,saved);
}
LRESULT CALLBACK richPaneProc(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    const LRESULT result=DefSubclassProc(h,m,wp,lp);
    if(m==WM_PAINT){HDC dc=GetDC(h);if(dc){paintRichPaneHint(h,dc);ReleaseDC(h,dc);}}
    if(m==WM_PRINTCLIENT||m==WM_PRINT)paintRichPaneHint(h,reinterpret_cast<HDC>(wp));
    if(m==WM_SETFOCUS||m==WM_KILLFOCUS||m==WM_IME_STARTCOMPOSITION||m==WM_IME_ENDCOMPOSITION)InvalidateRect(h,nullptr,FALSE);
    return result;
}
void paintNativeScroll(HWND h,HDC dc){
    RECT r{};GetClientRect(h,&r);FillRect(dc,&r,g->backgroundBrush);SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};if(!GetScrollInfo(h,SB_CTL,&info))return;
    const int range=std::max(0,info.nMax-info.nMin+1),page=std::clamp(static_cast<int>(info.nPage),0,range),maxPosition=std::max(0,range-page);if(range<=0||page<=0||maxPosition<=0||r.bottom<=0)return;
    const int dpi=static_cast<int>(uiDpi(h)),trackHeight=static_cast<int>(r.bottom),thumbHeight=std::clamp(MulDiv(trackHeight,page,range),std::min(ui::scale(24,dpi),trackHeight),trackHeight),travel=std::max(0,trackHeight-thumbHeight),thumbTop=maxPosition?MulDiv(travel,std::clamp(info.nPos,0,maxPosition),maxPosition):0;
    const bool hot=GetPropW(h,L"SAT.ScrollHot")!=nullptr;const int thickness=ui::scale(hot?8:6,dpi),center=(r.left+r.right)/2;RECT thumb{center-thickness/2,thumbTop,center+(thickness+1)/2,thumbTop+thumbHeight};
    const COLORREF color=hot?(darkSurface()?RGB(119,133,157):RGB(104,126,160)):(darkSurface()?RGB(74,79,90):RGB(185,191,201));HBRUSH brush=CreateSolidBrush(color);HGDIOBJ oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,GetStockObject(NULL_PEN));RoundRect(dc,thumb.left,thumb.top,thumb.right,thumb.bottom,thickness,thickness);SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(brush);
}
LRESULT CALLBACK nativeScrollProc(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(m==WM_PAINT){PAINTSTRUCT p{};HDC dc=BeginPaint(h,&p);paintNativeScroll(h,dc);EndPaint(h,&p);return 0;}
    if(m==WM_PRINTCLIENT||m==WM_PRINT){paintNativeScroll(h,reinterpret_cast<HDC>(wp));return 0;}
    if(m==WM_MOUSEMOVE&&!GetPropW(h,L"SAT.ScrollHot")){SetPropW(h,L"SAT.ScrollHot",reinterpret_cast<HANDLE>(1));TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);InvalidateRect(h,nullptr,FALSE);}
    if(m==WM_MOUSELEAVE){RemovePropW(h,L"SAT.ScrollHot");InvalidateRect(h,nullptr,FALSE);}
    const LRESULT result=DefSubclassProc(h,m,wp,lp);if(m==WM_LBUTTONUP||m==WM_KEYDOWN||m==SBM_SETSCROLLINFO)InvalidateRect(h,nullptr,FALSE);return result;
}
void beginTranslate();
void beginRewrite(RewriteStyle style);
void beginNaturalize();
void beginCustomPrompt(const std::wstring& prompt, CustomPromptScope scope);
void restoreVersion(uint64_t targetVersionId);
void showCustomPromptPanel();
void showRewriteMenu(HWND buttonWnd);
void showVersionMenu(HWND buttonWnd);
LRESULT CALLBACK customPromptProc(HWND w,UINT m,WPARAM wp,LPARAM lp);
void prepareIncoming(Incoming& in){
    if(in.settings||in.showOnly)return;
    if(!in.cancelMarker)in.cancelMarker=std::make_shared<InputCancelMarker>();
    if(!in.sourceId){if(g->nextSourceId==UINT64_MAX)throw std::runtime_error("input session identifier exhausted");in.sourceId=++g->nextSourceId;}
    else g->nextSourceId=std::max(g->nextSourceId,in.sourceId);
    if(in.submittedAt==InputClock::time_point{})in.submittedAt=InputClock::now();
}
void cancelInputSession(InputSession& session)noexcept{if(session.cancelMarker)session.cancelMarker->cancel();}
void invalidateInputSession()noexcept{cancelInputSession(g->inputSession);g->activeSourceId=0;}
void installInputSession(const Incoming& in){
    if(g->inputSession.cancelMarker!=in.cancelMarker)invalidateInputSession();
    g->sourceRevision=in.sourceId;
    g->inputSession={in.source,in.sourceId,g->revision,in.submittedAt,in.cancelMarker};g->sourceKind=in.source;g->activeSourceId=0;
}
void invalidateOcrCapture(){
    traceCapture("session_release");
    if(g->ocrConfirmWnd&&IsWindow(g->ocrConfirmWnd))DestroyWindow(g->ocrConfirmWnd);g->ocrConfirmWnd=nullptr;g->pendingOcrCandidate.reset();g->ocrConfirming=false;g->ocrDeadline=0;g->ocrAdapterSessionId=0;g->captureSessionActive=false;g->captureWasVisible=false;g->captureExitOnFailure=false;g->clipboardChangedWhileConfirming=false;g->clipboardRetryPending=false;
    ++g->ocrGeneration;
    if(g->ocrAdapter)g->ocrAdapter->cancelCapture();
    if(g->clipboardListenerRegistered&&g->window&&IsWindow(g->window)){RemoveClipboardFormatListener(g->window);g->clipboardListenerRegistered=false;}
    if(g->window)KillTimer(g->window,0x5342);
}
void acceptEditedSource(std::wstring text){
    Incoming in;in.source=InputSource::manualEdit;prepareIncoming(in);if(!in.cancelMarker->tryAccept())return;invalidateOcrCapture();invalidateInputSession();++g->revision;g->requestActive=false;stopMainRequest();g->source=std::move(text);installInputSession(in);clearCompleted();g->result.clear();
}
void applyIncoming(Incoming in){
#if defined(SAT_ENABLE_TEST_HOOKS)
    // Readiness instrumentation only. Cross-process SendMessage can reenter
    // while RichEdit has updated but the enclosing input transaction is active.
    struct InputApplyScope {InputApplyScope(){++g->inputApplyDepthForTest;}~InputApplyScope(){--g->inputApplyDepthForTest;}} inputApplyScope;
#endif
    if(in.settings){openSettings();return;}if(in.showOnly)return;prepareIncoming(in);if(in.cancelMarker&&in.cancelMarker->isCancelled())return;if(in.text.size()>maxTextBytes/sizeof(wchar_t)||!validText(in.text)){status(L"输入文本无效或超过 1 MiB 上限，未替换当前会话。" );return;}
    const bool interruptedCapture=g->captureSessionActive;
    if(g->editing||in.confirmReplacement){const int choice=MessageBoxW(g->window,L"新文本已到达。要放弃当前编辑或替换当前会话吗？",L"替换原文",MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2);if(in.cancelMarker&&in.cancelMarker->isCancelled())return;if(choice!=IDYES){in.confirmReplacement=true;status(L"新文本已保留在待处理队列中。关闭编辑后可再处理。" );if(!in.cancelMarker||!in.cancelMarker->isCancelled())g->pending.push_front(std::move(in));return;}g->editing=false;}
    std::wstring newRawSource=in.text,newSource=std::move(in.text);
    if(!in.cancelMarker||!in.cancelMarker->tryAccept())return;
    invalidateOcrCapture();
    invalidateInputSession();++g->revision;g->requestActive=false;stopMainRequest();
    g->rawSource=std::move(newRawSource);g->source=std::move(newSource);installInputSession(in);clearCompleted();g->result.clear();updatePanes();updateButtons();
    if(interruptedCapture){g->startupSnipPending=false;traceCapture("source_committed");revealMainWindow(true);}
    if(g->source.empty())status(L"粘贴或输入要翻译的文字。");else if(g->config.autoStart)beginTranslate();else status(L"原文已就绪。点击“翻译”开始。");
    if(interruptedCapture)traceCapture("translation_policy",g->requestActive?"request_started":g->config.autoStart?"configuration_required":"auto_start_disabled");
}
void processPending(){if(g->editing||g->pending.empty())return;auto in=std::move(g->pending.front());g->pending.pop_front();applyIncoming(std::move(in));}
bool queueIncoming(Incoming in){if(g->pending.size()>=4)return false;if(g->editing&&!in.settings&&!in.showOnly)in.confirmReplacement=true;prepareIncoming(in);g->pending.push_back(std::move(in));if(!PostMessageW(g->window,msgProcessPending,0,0)){if(g->pending.back().cancelMarker)g->pending.back().cancelMarker->cancel();g->pending.pop_back();return false;}return true;}
constexpr UINT_PTR ocrPollTimerId=0x5342;
void layoutOcrConfirmation(HWND w){
    if(!g||!w)return;RECT r{};GetClientRect(w,&r);const int dpi=static_cast<int>(uiDpi(w));auto p=[&](int n){return MulDiv(n,dpi,96);};const int margin=p(20),buttonW=p(112),buttonH=p(34),gap=p(8),bottom=static_cast<int>(r.bottom)-p(16);
    MoveWindow(uiItem(w,ocrCandidateTitle),margin,p(14),std::max(0,static_cast<int>(r.right)-margin*2),p(28),FALSE);
    MoveWindow(uiItem(w,ocrCandidateNotice),margin,p(43),std::max(0,static_cast<int>(r.right)-margin*2),p(22),FALSE);
    MoveWindow(uiItem(w,ocrCandidatePreview),margin,p(70),std::max(0,static_cast<int>(r.right)-margin*2),std::max(p(20),static_cast<int>(r.bottom)-p(126)),FALSE);
    MoveWindow(uiItem(w,ocrCandidateCancel),static_cast<int>(r.right)-margin-buttonW,bottom-buttonH,buttonW,buttonH,FALSE);
    MoveWindow(uiItem(w,ocrCandidateIgnore),static_cast<int>(r.right)-margin-buttonW*2-gap,bottom-buttonH,buttonW,buttonH,FALSE);
    MoveWindow(uiItem(w,ocrCandidateUse),static_cast<int>(r.right)-margin-buttonW*3-gap*2,bottom-buttonH,buttonW,buttonH,FALSE);
}
void cancelOcrCapture();
void resolveOcrCandidate(bool accept,bool cancelCapture=false);
void pollOcrSession();
LRESULT CALLBACK ocrConfirmProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    try{
        switch(m){
        case WM_NCCREATE:g->ocrConfirmWnd=w;return TRUE;
        case WM_CREATE:{
            auto title=control(0,L"STATIC",L"确认 OCR 候选",SS_LEFT,ocrCandidateTitle,w);SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(fontSetFor(w).emphasis),TRUE);
            control(0,L"STATIC",L"剪贴板来源无法验证。采用后可能按自动翻译设置发送；请先核对。",SS_LEFT,ocrCandidateNotice,w);
            control(0,L"STATIC",L"",SS_LEFT|SS_EDITCONTROL|SS_NOPREFIX,ocrCandidatePreview,w);
            for(const auto& item:{std::pair{ocrCandidateUse,L"采用候选"},std::pair{ocrCandidateIgnore,L"忽略"},std::pair{ocrCandidateCancel,L"取消截图"}}){auto button=control(0,L"BUTTON",item.second,WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY,item.first,w);SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(w)),TRUE);}
            layoutOcrConfirmation(w);return 0;
        }
        case WM_ERASEBKGND:{RECT r{};GetClientRect(w,&r);FillRect(reinterpret_cast<HDC>(wp),&r,g->controlBrush);return 1;}
        case WM_PAINT:{PAINTSTRUCT paint{};HDC dc=BeginPaint(w,&paint);RECT r{};GetClientRect(w,&r);FillRect(dc,&r,g->controlBrush);HBRUSH border=CreateSolidBrush(g->borderColor);if(border){FrameRect(dc,&r,border);DeleteObject(border);}EndPaint(w,&paint);return 0;}
        case WM_CTLCOLORSTATIC:{HWND child=reinterpret_cast<HWND>(lp);HDC dc=reinterpret_cast<HDC>(wp);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,GetDlgCtrlID(child)==ocrCandidateNotice?g->mutedColor:g->foregroundColor);return reinterpret_cast<LRESULT>(g->controlBrush);}
        case WM_DRAWITEM:if(auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lp)){drawActionButton(*item);return TRUE;}break;
        case WM_SIZE:layoutOcrConfirmation(w);return 0;
        case WM_DPICHANGED:{const auto* r=reinterpret_cast<const RECT*>(lp);SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);refreshFonts(w);layoutOcrConfirmation(w);return 0;}
        case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED){if(LOWORD(wp)==ocrCandidateUse){resolveOcrCandidate(true);return 0;}if(LOWORD(wp)==ocrCandidateIgnore){resolveOcrCandidate(false);return 0;}if(LOWORD(wp)==ocrCandidateCancel){resolveOcrCandidate(false,true);return 0;}}break;
        case WM_KEYDOWN:if(wp==VK_ESCAPE){resolveOcrCandidate(false,true);return 0;}break;
        case WM_CLOSE:resolveOcrCandidate(false,true);return 0;
        case WM_DESTROY:if(g->ocrConfirmWnd==w)g->ocrConfirmWnd=nullptr;return 0;
        }
        return DefWindowProcW(w,m,wp,lp);
    }catch(...){if(g&&g->window&&IsWindow(g->window))status(L"OCR 候选确认界面无法完成操作；未发送候选。");return 0;}
}
void finishOcrCaptureFailure(const std::wstring& message){
    if(!g)return;traceCapture("capture_failure","release_wait");const bool restore=g->captureWasVisible,exitAfterFailure=g->captureExitOnFailure;invalidateOcrCapture();
    if(exitAfterFailure){g->startupSnipPending=false;g->exitRequested=true;if(g->window&&IsWindow(g->window))DestroyWindow(g->window);PostQuitMessage(0);return;}
    if(restore)revealMainWindow(true);status(message);
}
void resumeOcrPolling(const wchar_t* message,bool recheckClipboard=false){
    if(!g||!g->window)return;
    if(!g->captureSessionActive||!g->ocrAdapter||!g->ocrAdapter->isCapturing()){finishOcrCaptureFailure(message);return;}
    if(GetTickCount64()>=g->ocrDeadline){finishOcrCaptureFailure(L"截图 OCR 等待超时；候选未采用。");return;}
    if(!SetTimer(g->window,ocrPollTimerId,50,nullptr)){finishOcrCaptureFailure(L"无法继续等待截图；候选未采用。");return;}
    const bool recheck=recheckClipboard||g->clipboardChangedWhileConfirming;g->clipboardChangedWhileConfirming=false;status(message);if(recheck)pollOcrSession();
}
void openOcrConfirmation(OcrResult result,uint64_t generation,uint64_t inputId){
    if(!g||!g->window||g->ocrConfirmWnd||!g->ocrAdapter)return;
    PendingOcrCandidate candidate;candidate.text=std::move(result.text);candidate.generation=generation;candidate.inputSessionId=inputId;candidate.adapterSessionId=g->ocrAdapter->sessionId();candidate.clipboardSequence=result.sequence;candidate.deadline=g->ocrDeadline;g->pendingOcrCandidate=std::move(candidate);g->ocrConfirming=true;
    const DWORD style=WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME,exStyle=WS_EX_CONTROLPARENT|WS_EX_TOOLWINDOW;const UINT dpi=uiDpi(g->window);RECT outer{0,0,MulDiv(560,static_cast<int>(dpi),96),MulDiv(220,static_cast<int>(dpi),96)};AdjustWindowRectExForDpi(&outer,style,FALSE,exStyle,dpi);
    RECT owner{};GetWindowRect(g->window,&owner);MONITORINFO monitor{sizeof(monitor)};const HMONITOR target=MonitorFromWindow(g->window,MONITOR_DEFAULTTONEAREST);const bool haveMonitor=GetMonitorInfoW(target,&monitor)!=FALSE;const int areaLeft=haveMonitor?monitor.rcWork.left:owner.left,areaTop=haveMonitor?monitor.rcWork.top:owner.top,areaRight=haveMonitor?monitor.rcWork.right:owner.right,areaBottom=haveMonitor?monitor.rcWork.bottom:owner.bottom;
    const int width=outer.right-outer.left,height=outer.bottom-outer.top,x=areaLeft+std::max(0,(areaRight-areaLeft-width)/2),y=areaTop+std::max(0,(areaBottom-areaTop-height)/2);
    HWND panel=CreateWindowExW(exStyle,L"SAT.OcrConfirm",L"截图翻译候选确认 · Translator",style,x,y,width,height,nullptr,nullptr,g->instance,nullptr);
    if(!panel){g->ocrConfirming=false;g->pendingOcrCandidate.reset();finishOcrCaptureFailure(L"无法显示 OCR 候选确认界面；候选未采用。");return;}
    size_t length=std::min<size_t>(160,g->pendingOcrCandidate->text.size());if(length&&length<g->pendingOcrCandidate->text.size()&&g->pendingOcrCandidate->text[length-1]>=0xD800&&g->pendingOcrCandidate->text[length-1]<=0xDBFF)--length;
    std::wstring preview=g->pendingOcrCandidate->text.substr(0,length);if(length<g->pendingOcrCandidate->text.size())preview+=L"…";setText(uiItem(panel,ocrCandidatePreview),preview);
    setTheme();layoutOcrConfirmation(panel);
    // A new top-level popup can inherit the launcher's initial SW_HIDE.
    // SetWindowPos expresses the session's visibility without startup overrides.
    if(GetCurrentThreadId()!=GetWindowThreadProcessId(panel,nullptr)||
       !SetWindowPos(panel,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW)||!IsWindowVisible(panel)){
        traceCapture("confirmation_failed","show_window",GetLastError());finishOcrCaptureFailure(L"无法显示 OCR 候选确认界面；候选未采用。");return;
    }
    UpdateWindow(panel);traceCapture("confirmation_shown");
    if(!testMode()||showTestUi()){const bool foreground=SetForegroundWindow(panel)!=FALSE;traceCapture("confirmation_activation",foreground?"activated":"foreground_denied");}
    SetFocus(uiItem(panel,ocrCandidateUse));InvalidateRect(panel,nullptr,TRUE);status(L"OCR 候选待确认；忽略可继续等待，Esc 可取消截图。");
}
void resolveOcrCandidate(bool accept,bool cancelCapture){
    if(!g||!g->pendingOcrCandidate)return;
    PendingOcrCandidate candidate=std::move(*g->pendingOcrCandidate);g->pendingOcrCandidate.reset();g->ocrConfirming=false;HWND panel=g->ocrConfirmWnd;g->ocrConfirmWnd=nullptr;if(panel&&IsWindow(panel))DestroyWindow(panel);
    if(cancelCapture){traceCapture("candidate_cancelled","user_cancel");cancelOcrCapture();return;}
    const bool sessionCurrent=g->ocrAdapter&&candidate.generation==g->ocrGeneration&&candidate.inputSessionId==g->inputSession.id&&candidate.adapterSessionId==g->ocrAdapter->sessionId()&&candidate.adapterSessionId==g->ocrAdapterSessionId;
    if(!sessionCurrent){traceCapture("candidate_rejected","session_mismatch");resumeOcrPolling(L"OCR 候选已过期，未采用；继续等待当前会话。",true);return;}
    if(!accept){traceCapture("candidate_ignored","user_ignore");resumeOcrPolling(L"未采用此候选；继续等待本次 OCR（按 Esc 取消）。");return;}
    if(GetTickCount64()>=candidate.deadline||!g->captureSessionActive||!g->ocrAdapter->isCapturing()){finishOcrCaptureFailure(L"OCR 候选已超时；未采用。");return;}
    if(!g->ocrAdapter->isCandidateCurrent(candidate.clipboardSequence)){traceCapture("candidate_rejected","clipboard_competition");resumeOcrPolling(L"剪贴板已再次变化，旧候选未采用；正在等待新候选。",true);return;}
    traceCapture("candidate_accepted","user_confirm");
    Incoming in;in.source=InputSource::clipboardOcrCandidate;in.text=std::move(candidate.text);applyIncoming(std::move(in));
}
void cancelOcrCapture(){if(!g)return;if(!g->captureSessionActive){invalidateOcrCapture();return;}finishOcrCaptureFailure(L"截图已取消。");}
void pollOcrSession(){
    if(!g||!g->window||!g->ocrAdapter||!g->captureSessionActive)return;
    if(g->ocrConfirming){if(GetTickCount64()>=g->ocrDeadline)finishOcrCaptureFailure(L"截图 OCR 等待超时；候选未采用。");return;}
    const auto generation=g->ocrGeneration,inputId=g->ocrInputId;
    auto res=g->ocrAdapter->poll();if(res.status==OcrSessionStatus::waiting){g->clipboardRetryPending=res.retrySoon;if(res.retrySoon)traceCapture("clipboard_retry","clipboard_busy");status(res.detail);return;}g->clipboardRetryPending=false;
    if(res.status==OcrSessionStatus::idle)return;
    traceCapture("adapter_result",res.status==OcrSessionStatus::candidate?"candidate":res.status==OcrSessionStatus::captured?"captured":res.status==OcrSessionStatus::timeout?"timeout":res.status==OcrSessionStatus::cancelled?"cancelled":"failed");
    if(res.status==OcrSessionStatus::candidate||res.status==OcrSessionStatus::captured){
        if(generation!=g->ocrGeneration||inputId!=g->inputSession.id||g->ocrAdapterSessionId!=g->ocrAdapter->sessionId()||g->editing){traceCapture("result_rejected","session_mismatch");finishOcrCaptureFailure(L"OCR 会话已失效；候选未采用。");return;}
        if(GetTickCount64()>=g->ocrDeadline){traceCapture("result_rejected","deadline");finishOcrCaptureFailure(L"OCR 候选已超时；未采用。");return;}
        if(res.text.size()>maxTextBytes/sizeof(wchar_t)){traceCapture("result_rejected","text_limit");finishOcrCaptureFailure(L"OCR 文字超过安全上限；未采用。");return;}
    }
    if(res.status==OcrSessionStatus::candidate){
        if(!validText(res.text)||!SnipasteReverseAdapter::isMeaningfulText(res.text)){traceCapture("result_rejected","invalid_text");finishOcrCaptureFailure(L"候选文字为空或格式无效，未替换原文，请重试。");return;}
        if(!g->ocrAdapter->isCandidateCurrent(res.sequence)){resumeOcrPolling(L"剪贴板再次变化，未采用旧候选；继续等待本次 OCR。");return;}
        if(g->config.confirmOcrCandidates){openOcrConfirmation(std::move(res),generation,inputId);return;}
        if(generation!=g->ocrGeneration||inputId!=g->inputSession.id||g->editing)return;
        traceCapture("candidate_accepted","configured_adopt");Incoming in;in.source=InputSource::clipboardOcrCandidate;in.text=std::move(res.text);applyIncoming(std::move(in));return;
    }
    if(!g->window||generation!=g->ocrGeneration||inputId!=g->inputSession.id||g->editing)return;
    if(res.status==OcrSessionStatus::captured){if(!validText(res.text)||!SnipasteReverseAdapter::isMeaningfulText(res.text)){finishOcrCaptureFailure(L"OCR 没有返回有效文字；未显示空白翻译窗口。");return;}Incoming in;in.source=InputSource::snipasteOcr;in.text=std::move(res.text);applyIncoming(std::move(in));}
    else if(res.status==OcrSessionStatus::timeout)finishOcrCaptureFailure(L"等待截图 / OCR 超时；未改变原文。请重试或手动粘贴。");else if(res.status==OcrSessionStatus::cancelled)finishOcrCaptureFailure(L"截图已取消。");else if(res.status==OcrSessionStatus::failed)finishOcrCaptureFailure(res.detail.empty()?L"截图捕获失败，请重试。":res.detail);
}
void monitorOcrSession(){
    if(!g||!g->captureSessionActive)return;
    if((GetAsyncKeyState(VK_ESCAPE)&0x8000)!=0){traceCapture("capture_cancelled","escape");cancelOcrCapture();return;}
    if(GetTickCount64()>=g->ocrDeadline){traceCapture("capture_timeout","deadline");finishOcrCaptureFailure(L"等待截图 / OCR 超时；未改变原文。");return;}
    if(!g->ocrAdapter||g->ocrInputId!=g->inputSession.id||g->ocrAdapterSessionId!=g->ocrAdapter->sessionId()){
        traceCapture("capture_invalidated","session_mismatch");finishOcrCaptureFailure(L"OCR 会话已失效；候选未采用。");return;
    }
    if(g->ocrConfirming)return;
    bool clipboardUpdate=false;
    if(g->ocrAdapter->kind()==OcrAdapterKind::reverseSnipaste)
        clipboardUpdate=static_cast<SnipasteReverseAdapter*>(g->ocrAdapter.get())->hasClipboardUpdate();
    // The existing session timer can recover a missing clipboard notification.
    // Sequence changes still produce unverified candidates through the same checks.
    if(clipboardUpdate&&!g->clipboardRetryPending)traceCapture("clipboard_sequence_recovery","notification_not_processed");
    if(g->ocrAdapter->kind()==OcrAdapterKind::mock||g->clipboardRetryPending||clipboardUpdate)pollOcrSession();
}
void startOcrCapture(){
    if(!g||!g->window)return;
    if(g->editing){status(L"请先提交或取消原文编辑，再截图。");return;}
    if(g->captureSessionActive||g->ocrConfirming)return;
    if(!g->ocrAdapter)g->ocrAdapter=std::make_unique<SnipasteReverseAdapter>(g->config.snipasteExePath);
    if(g->ocrAdapter->isCapturing())return;
    if(g->ocrAdapter->kind()==OcrAdapterKind::reverseSnipaste)static_cast<SnipasteReverseAdapter*>(g->ocrAdapter.get())->setSnipastePath(g->config.snipasteExePath);
#if defined(SAT_ENABLE_TEST_HOOKS)
    // Test executable only: fixtures model launch/argv, not Snipaste's IPC readiness.
    if(testMode()&&g->ocrAdapter->kind()==OcrAdapterKind::reverseSnipaste)
        static_cast<SnipasteReverseAdapter*>(g->ocrAdapter.get())->setRunningProbeForTest([]{return SnipasteRunningStatus::running;});
#endif
    g->captureWasVisible=IsWindowVisible(g->window)!=FALSE;g->captureExitOnFailure=g->startupSnipPending&&!g->captureWasVisible;g->captureSessionActive=true;
    traceCapture("capture_start");
    if(g->captureWasVisible)ShowWindow(g->window,SW_HIDE);
    if(!AddClipboardFormatListener(g->window)){traceCapture("capture_failed","listener",GetLastError());finishOcrCaptureFailure(L"无法监听 OCR 识别结果；尚未触发截图工具。");return;}g->clipboardListenerRegistered=true;traceCapture("listener_registered");
    g->ocrDeadline=GetTickCount64()+30000;
    if(!SetTimer(g->window,ocrPollTimerId,50,nullptr)){finishOcrCaptureFailure(L"无法创建截图监视器；尚未触发截图工具。");return;}
    std::wstring err;if(!g->ocrAdapter->startCapture(30000,err)){traceCapture("capture_failed","adapter_start");finishOcrCaptureFailure(err.empty()?L"无法启动截图 OCR。":err);return;}
    ++g->ocrGeneration;g->ocrInputId=g->inputSession.id;g->ocrAdapterSessionId=g->ocrAdapter->sessionId();
    traceCapture("snipaste_invoked");
    status(L"正在等待 OCR 文字（Esc 取消）；识别结果到达后请核对候选。");
}
bool isCurrentSourceEvent(const TranslationEvent& event)noexcept{return g->activeSourceId!=0&&g->inputSession.id==g->activeSourceId&&!g->inputSession.cancelled()&&isCurrentTranslationEvent(event,g->activeRequestId,g->revision,false,g->activeSourceId);}
void rememberNormalGeometry(HWND window){
    if(!g->windowPlaced||IsIconic(window)||IsZoomed(window))return;RECT outer{},client{};if(!GetWindowRect(window,&outer)||!GetClientRect(window,&client))return;
    const UINT dpi=uiDpi(window);if(!dpi)return;MONITORINFO monitor{sizeof(monitor)};if(!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor))return;
    if(g->config.rememberPosition){g->config.xDip=std::clamp(MulDiv(outer.left-monitor.rcWork.left,96,static_cast<int>(dpi)),-10000,10000);g->config.yDip=std::clamp(MulDiv(outer.top-monitor.rcWork.top,96,static_cast<int>(dpi)),-10000,10000);}
    g->config.widthDip=std::clamp(MulDiv(client.right-client.left,96,static_cast<int>(dpi)),620,1600);g->config.heightDip=std::clamp(MulDiv(client.bottom-client.top,96,static_cast<int>(dpi)),360,1200);
}
void beginAiOperation(AiOperationType type,RewriteStyle style=RewriteStyle::natural,const std::wstring& prompt=L"",CustomPromptScope scope=CustomPromptScope::sourceAndResult){
    if(g->editing||g->requestActive){status(L"请先结束编辑或当前请求。");return;}
    if(g->inputSession.id==0||g->inputSession.cancelled()){status(L"原文会话已失效，请重新输入。");return;}
    const bool needsResult=type!=AiOperationType::custom||scope!=CustomPromptScope::sourceOnly;
    if(needsResult&&!completedVersion()){status(L"请先获得与当前原文和语种匹配的完整译文。");return;}
    if(type==AiOperationType::custom&&(trimCopy(prompt).empty()||prompt.size()>2000||!validText(prompt))){status(L"自定义指令为空、过长或包含无效字符。");return;}
    if(type==AiOperationType::custom&&scope!=CustomPromptScope::resultOnly&&trimCopy(g->source).empty()){status(L"当前没有有效原文。");return;}
    const auto* profile=g->config.getProfileForRole(L"rewrite");
    if(!profile||!profile->enabled||profile->baseUrl.empty()||profile->model.empty()||profile->apiKey.empty()){status(L"请配置并启用绑定的改写模型和 API Key。");return;}
    std::wstring error;if(!validateEndpoint(profile->baseUrl,error)){status(error);return;}
    g->retryTargetLanguage=g->language;cancelOperation();g->activeRequestId=++g->requestSerial;g->activeSourceId=g->inputSession.id;g->activeOpType=type;g->activeRewriteStyle=style;g->activeCustomScope=scope;g->activeCustomPrompt=prompt;g->retryAvailable=false;g->lastErrorDetails.clear();
    TranslationRequest req;SecretWiper wiper{req.apiKey};
    req.baseUrl=profile->baseUrl;req.model=profile->model;req.apiKey=profile->apiKey;
    req.timeoutSeconds=profile->timeoutSeconds;req.maxOutputTokens=profile->maxOutputTokens;req.thinkingMode=profile->thinkingMode;req.modelProfileId=profile->id;
    req.text=g->source;req.targetLanguage=g->language;req.resultText=g->lastCompletedResult;
    req.opType=type;req.rewriteStyle=style;req.customScope=scope;req.customPrompt=prompt;
    req.requestId=g->activeRequestId;req.revision=g->revision;req.sourceSessionId=g->activeSourceId;req.baseVersionId=g->currentVersionId;
    g->result.clear();updatePanes();
    std::weak_ptr<UiEventQueue> weak=g->uiQueue;
    try{g->task=TranslationTask::start(std::move(req),[weak](TranslationEvent ev){postTranslationEvent(weak,std::move(ev));},[weak]{postTaskFinished(weak);});g->requestActive=true;status(L"正在处理文本…");}
    catch(...){g->activeRequestId=0;recoverCompleted(true);status(L"无法启动请求，请检查输入与模型配置。");}
    updateButtons();layoutMain();
}
void beginRewrite(RewriteStyle style){beginAiOperation(AiOperationType::rewrite,style);}
void beginNaturalize(){beginAiOperation(AiOperationType::naturalize);}
void beginCustomPrompt(const std::wstring& prompt,CustomPromptScope scope){beginAiOperation(AiOperationType::custom,RewriteStyle::natural,prompt,scope);}
void restoreVersion(uint64_t targetVersionId){
    if(g->editing){status(L"请先结束原文编辑。");return;}
    for(const auto& v:g->versions)if(v.versionId==targetVersionId&&v.completed){
        invalidateOcrCapture();
        cancelOperation();
        if(v.sourceSnapshot!=g->source){Incoming in;in.source=InputSource::manualEdit;prepareIncoming(in);if(!in.cancelMarker->tryAccept())return;installInputSession(in);}
        g->source=v.sourceSnapshot;g->rawSource=v.rawSnapshot;g->sourceRevision=v.sourceRevision;
        g->language=v.targetLanguage;g->config.targetLanguage=g->language;
        const wchar_t* codes[]={L"zh-Hans",L"zh-Hant",L"en",L"ja",L"ko",L"fr",L"de",L"es"};
        for(int i=0;i<8;++i)if(g->language==codes[i])uiMessage(g->window,targetCombo,CB_SETCURSEL,i,0);
        g->currentVersionId=v.versionId;g->result=v.resultText;g->lastCompletedResult=v.resultText;g->lastCompletedLanguage=v.targetLanguage;clearRetryMetadata();
        updatePanes();updateButtons();layoutMain();status(L"已恢复版本 #"+std::to_wstring(v.versionId));return;
    }
}
LRESULT CALLBACK promptEditSubclass(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    trackImeMessage(h,msg);
    if(msg==WM_KEYDOWN&&!imeComposing(h)){
        if(wp==VK_RETURN&&(GetKeyState(VK_CONTROL)&0x8000)){
            SendMessageW(GetParent(h),WM_COMMAND,MAKEWPARAM(promptExecuteBtn,BN_CLICKED),0);
            return 0;
        }
        if(wp==VK_ESCAPE){
            SendMessageW(GetParent(h),WM_COMMAND,MAKEWPARAM(promptCancelBtn,BN_CLICKED),0);
            return 0;
        }
    }
    return DefSubclassProc(h,msg,wp,lp);
}
LRESULT CALLBACK promptScopeKeyProc(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==WM_GETDLGCODE)return DefSubclassProc(h,message,wp,lp)|DLGC_WANTARROWS;
    if(message==WM_KEYDOWN&&(wp==VK_LEFT||wp==VK_UP||wp==VK_RIGHT||wp==VK_DOWN)){
        const int offset=GetDlgCtrlID(h)-promptScopeBoth,step=(wp==VK_LEFT||wp==VK_UP)?2:1,id=promptScopeBoth+(offset+step)%3;
        SendMessageW(GetParent(h),WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),0);SetFocus(uiItem(GetParent(h),id));return 0;
    }
    return DefSubclassProc(h,message,wp,lp);
}
void drawPromptScope(const DRAWITEMSTRUCT& item){
    const int dpi=static_cast<int>(uiDpi(item.hwndItem));const float width=(item.rcItem.right-item.rcItem.left)*96.f/dpi,height=(item.rcItem.bottom-item.rcItem.top)*96.f/dpi;
    const COLORREF ink=IsWindowEnabled(item.hwndItem)?g->foregroundColor:g->mutedColor;auto& painter=ui::vectorPainter();
    if(painter.begin(item.hDC,item.rcItem,dpi)){
        painter.clear(g->controlBackgroundColor);painter.ring(3,height/2-6,12,12,6,ink,1.2f);
        if(SendMessageW(item.hwndItem,BM_GETCHECK,0,0)==BST_CHECKED)painter.dot(9,height/2,3,ink);
        if((item.itemState&ODS_FOCUS)&&!(item.itemState&ODS_NOFOCUSRECT))painter.ring(1,1,width-2,height-2,4,RGB(104,144,205));painter.end();
    }
    RECT label=item.rcItem;label.left+=MulDiv(20,dpi,96);const auto text=getText(item.hwndItem);const int saved=SaveDC(item.hDC);
    SelectObject(item.hDC,reinterpret_cast<HFONT>(SendMessageW(item.hwndItem,WM_GETFONT,0,0)));SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,ink);DrawTextW(item.hDC,text.c_str(),static_cast<int>(text.size()),&label,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);if(saved)RestoreDC(item.hDC,saved);
}
LRESULT CALLBACK customPromptProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    try{
    switch(m){
    case WM_CREATE:{
        auto radio=[&](int id,const wchar_t* text,bool checked){
            HWND h=CreateWindowExW(0,L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY,0,0,10,10,w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),g->instance,nullptr);
            if(!h||!SetWindowSubclass(h,settingsCheckboxProc,1,0)||!SetWindowSubclass(h,hoverButtonProc,7,0)||!SetWindowSubclass(h,promptScopeKeyProc,16,0))throw std::runtime_error("prompt scope control initialization failed");
            SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(g->window)),TRUE);
            if(checked)SendMessageW(h,BM_SETCHECK,BST_CHECKED,0);
            return h;
        };
        radio(promptScopeBoth,L"原文+译文",true);
        radio(promptScopeResult,L"仅译文",false);
        radio(promptScopeSource,L"仅原文",false);
        HWND combo=CreateWindowExW(WS_EX_CLIENTEDGE,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL,0,0,10,10,w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(promptHistoryCombo)),g->instance,nullptr);
        SendMessageW(combo,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(g->window)),TRUE);
        SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"最近指令…"));
        SendMessageW(combo,CB_SETCURSEL,0,0);
        HWND edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,0,0,10,10,w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(promptTextEdit)),g->instance,nullptr);
        SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(g->window)),TRUE);
        SendMessageW(edit,EM_SETLIMITTEXT,2001,0);
        SetWindowSubclass(edit,promptEditSubclass,2,0);
        HWND status=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,10,10,w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(promptStatusLabel)),g->instance,nullptr);
        SendMessageW(status,WM_SETFONT,reinterpret_cast<WPARAM>(g->mainFonts.note),TRUE);
        HWND exec=CreateWindowExW(0,L"BUTTON",L"执行",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY|BS_DEFPUSHBUTTON,0,0,10,10,w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(promptExecuteBtn)),g->instance,nullptr);
        SendMessageW(exec,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(g->window)),TRUE);
        HWND cancel=CreateWindowExW(0,L"BUTTON",L"取消",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY,0,0,10,10,w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(promptCancelBtn)),g->instance,nullptr);
        SendMessageW(cancel,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(g->window)),TRUE);
        if(!SetWindowSubclass(exec,hoverButtonProc,7,0)||!SetWindowSubclass(cancel,hoverButtonProc,7,0))throw std::runtime_error("prompt action rendering initialization failed");
        return 0;
    }
    case WM_SIZE:{
        RECT r{};GetClientRect(w,&r);const int dpi=static_cast<int>(uiDpi(w));auto layout=ui::promptFrame(r.right,r.bottom,dpi);
        // ComboBox height passed to MoveWindow includes its drop-down list.
        // Reserve the actual closed field height before placing the input below it.
        const auto& history=layout.history;MoveWindow(uiItem(w,promptHistoryCombo),history.x,history.y,history.width,ui::scale(140,dpi),TRUE);
        RECT combo{};if(GetWindowRect(uiItem(w,promptHistoryCombo),&combo))layout=ui::promptFrame(r.right,r.bottom,dpi,combo.bottom-combo.top);
        for(const auto& placement:{std::pair{promptScopeBoth,layout.both},std::pair{promptScopeResult,layout.result},std::pair{promptScopeSource,layout.source},std::pair{promptTextEdit,layout.input},std::pair{promptStatusLabel,layout.status},std::pair{promptExecuteBtn,layout.execute},std::pair{promptCancelBtn,layout.cancel}}){const auto& box=placement.second;MoveWindow(uiItem(w,placement.first),box.x,box.y,box.width,box.height,TRUE);}
        return 0;
    }
    case WM_ERASEBKGND:{RECT r{};GetClientRect(w,&r);FillRect(reinterpret_cast<HDC>(wp),&r,g->controlBrush?g->controlBrush:GetSysColorBrush(COLOR_WINDOW));return 1;}
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:return themedControlColor(m,wp,lp);
    case WM_DRAWITEM:{auto*item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(!item)break;if(item->CtlType==ODT_BUTTON&&item->CtlID>=promptScopeBoth&&item->CtlID<=promptScopeSource){drawPromptScope(*item);return TRUE;}if(item->CtlType==ODT_BUTTON&&(item->CtlID==promptExecuteBtn||item->CtlID==promptCancelBtn)){drawActionButton(*item);return TRUE;}if(item->CtlType==ODT_COMBOBOX&&item->CtlID==promptHistoryCombo){drawThemeComboItem(*item);return TRUE;}break;}
    case WM_COMMAND:{
        const int id=LOWORD(wp);
        if(id!=promptHistoryCombo&&HIWORD(wp)!=BN_CLICKED)return 0;
        if(id==promptScopeBoth||id==promptScopeResult||id==promptScopeSource){
            SendMessageW(uiItem(w,promptScopeBoth),BM_SETCHECK,id==promptScopeBoth?BST_CHECKED:BST_UNCHECKED,0);
            SendMessageW(uiItem(w,promptScopeResult),BM_SETCHECK,id==promptScopeResult?BST_CHECKED:BST_UNCHECKED,0);
            SendMessageW(uiItem(w,promptScopeSource),BM_SETCHECK,id==promptScopeSource?BST_CHECKED:BST_UNCHECKED,0);
            return 0;
        }
        if(id==promptHistoryCombo&&HIWORD(wp)==CBN_SELCHANGE){
            int sel=static_cast<int>(uiMessage(w,promptHistoryCombo,CB_GETCURSEL,0,0));
            if(sel>0&&sel<=static_cast<int>(g->recentPrompts.size())){
                setText(uiItem(w,promptTextEdit),g->recentPrompts[sel-1]);
            }
            return 0;
        }
        if(id==promptCancelBtn){
            ShowWindow(w,SW_HIDE);
            if(g->window)SetFocus(uiItem(g->window,resultEdit));
            return 0;
        }
        if(id==promptExecuteBtn){
            std::wstring promptText=trimCopy(getText(uiItem(w,promptTextEdit)));
            if(promptText.empty()){uiSetText(w,promptStatusLabel,L"自定义指令不能为空。");return 0;}
            if(promptText.size()>2000){uiSetText(w,promptStatusLabel,L"自定义指令不能超过 2000 字符。");return 0;}
            CustomPromptScope scope=CustomPromptScope::sourceAndResult;
            if(uiMessage(w,promptScopeResult,BM_GETCHECK,0,0)==BST_CHECKED)scope=CustomPromptScope::resultOnly;
            else if(uiMessage(w,promptScopeSource,BM_GETCHECK,0,0)==BST_CHECKED)scope=CustomPromptScope::sourceOnly;
            if(scope==CustomPromptScope::sourceAndResult&&(g->source.empty()||!completedVersion())){uiSetText(w,promptStatusLabel,L"此范围需要原文和完整译文；可切换为仅原文。");return 0;}
            if(scope==CustomPromptScope::resultOnly&&!completedVersion()){uiSetText(w,promptStatusLabel,L"当前没有译文。");return 0;}
            if(scope==CustomPromptScope::sourceOnly&&g->source.empty()){uiSetText(w,promptStatusLabel,L"当前没有原文。");return 0;}
            auto& recents=g->recentPrompts;
            auto it=std::find(recents.begin(),recents.end(),promptText);
            if(it!=recents.end())recents.erase(it);
            if(g->config.rememberPrompts)recents.push_front(promptText);
            while(recents.size()>5)recents.pop_back();
            ShowWindow(w,SW_HIDE);
            if(g->window)SetFocus(uiItem(g->window,resultEdit));
            beginCustomPrompt(promptText,scope);
            return 0;
        }
        break;
    }
    }
    return DefWindowProcW(w,m,wp,lp);
    }catch(...){return 0;}
}
void showCustomPromptPanel(){
    if(!g->customPromptWnd||g->editing||g->requestActive)return;
    if(childVisible(g->customPromptWnd)){
        ShowWindow(g->customPromptWnd,SW_HIDE);
        return;
    }
    HWND combo=uiItem(g->customPromptWnd,promptHistoryCombo);
    SendMessageW(combo,CB_RESETCONTENT,0,0);
    SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"最近指令…"));
    for(const auto& item:g->recentPrompts){
        std::wstring s=item.size()>25?item.substr(0,25)+L"…":item;
        SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s.c_str()));
    }
    SendMessageW(combo,CB_SETCURSEL,0,0);
    uiSetText(g->customPromptWnd,promptStatusLabel,L"");
    ShowWindow(g->customPromptWnd,SW_SHOW);
    layoutMain();
    SetFocus(uiItem(g->customPromptWnd,promptTextEdit));
}
void showRewriteMenu(HWND buttonWnd){
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,1,L"自然 (Natural)");
    AppendMenuW(menu,MF_STRING,2,L"简洁 (Concise)");
    AppendMenuW(menu,MF_STRING,3,L"口语 (Casual)");
    AppendMenuW(menu,MF_STRING,4,L"正式 (Formal)");
    AppendMenuW(menu,MF_STRING,5,L"文艺 (Literary)");
    RECT r{};GetWindowRect(buttonWnd,&r);
    const int cmd=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_NONOTIFY,r.left,r.bottom,g->window,nullptr);
    DestroyMenu(menu);
    if(cmd>=1&&cmd<=5){
        const RewriteStyle styles[]={RewriteStyle::natural,RewriteStyle::concise,RewriteStyle::casual,RewriteStyle::formal,RewriteStyle::literary};
        beginRewrite(styles[cmd-1]);
    }
}
void showVersionMenu(HWND buttonWnd){
    if(g->versions.empty()){status(L"暂无历史版本。");return;}
    HMENU menu=CreatePopupMenu();
    if(!menu)return;std::vector<uint64_t> versionIds;
    for(size_t i=0;i<g->versions.size();++i){
        const auto& v=g->versions[i];
        const wchar_t* opName=v.opType==AiOperationType::translate?L"初译":v.opType==AiOperationType::naturalize?L"去AI味":v.opType==AiOperationType::custom?L"自定义":L"改写";
        std::wstring summary=v.resultText.substr(0,16);
        if(!summary.empty()&&summary.back()>=0xd800&&summary.back()<=0xdbff)summary.pop_back();
        if(v.resultText.size()>16)summary+=L"…";
        while(!summary.empty()&&(summary.back()==L'\r'||summary.back()==L'\n'))summary.pop_back();
        std::replace(summary.begin(),summary.end(),L'\r',L' ');std::replace(summary.begin(),summary.end(),L'\n',L' ');
        for(size_t at=0;(at=summary.find(L'&',at))!=std::wstring::npos;at+=2)summary.insert(at,1,L'&');
        const auto timestamp=std::chrono::system_clock::to_time_t(v.createdAt);std::tm local{};wchar_t time[32]{};
        if(localtime_s(&local,&timestamp)==0)wcsftime(time,std::size(time),L"%m-%d %H:%M:%S",&local);
        std::wstring item=L"#" + std::to_wstring(v.versionId) + L" " + time + L" [" + opName + L"] " + v.targetLanguage + L" - " + summary;
        versionIds.push_back(v.versionId);
        UINT flags=MF_STRING|(v.versionId==g->currentVersionId?MF_CHECKED:0);
        AppendMenuW(menu,flags,static_cast<UINT_PTR>(100+i),item.c_str());
    }
    RECT r{};GetWindowRect(buttonWnd,&r);
    const int cmd=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_NONOTIFY,r.left,r.bottom,g->window,nullptr);
    DestroyMenu(menu);
    if(cmd>=100&&cmd<static_cast<int>(100+versionIds.size())){
        restoreVersion(versionIds[cmd-100]);
    }
}
void processNetworkEvents(){
    std::deque<TranslationEvent> events;{std::lock_guard lock(g->uiQueue->mutex);events.swap(g->uiQueue->events);}
    for(auto& event:events){
        if(event.testConnection){if(!isCurrentTranslationEvent(event,g->activeTestId,g->revision,true)||!g->settings)continue;
            if(event.kind==TranslationEvent::Kind::complete){g->connectionTestActive=false;g->settingsErrorDetails.clear();uiSetText(g->settings,settingStatus,L"连接成功，模型已返回译文。测试使用少量 API Token。");uiSetText(g->settings,settingTestButton,L"测试连接");ShowWindow(uiItem(g->settings,settingErrorDetails),SW_HIDE);layoutSettings(g->settings);}
            else if(event.kind==TranslationEvent::Kind::error){g->connectionTestActive=false;g->settingsErrorDetails=errorDetailsText(event);uiSetText(g->settings,settingStatus,event.text.c_str());uiSetText(g->settings,settingTestButton,L"重试");ShowWindow(uiItem(g->settings,settingErrorDetails),SW_SHOW);layoutSettings(g->settings);}
            else if(event.kind==TranslationEvent::Kind::cancelled){g->connectionTestActive=false;uiSetText(g->settings,settingStatus,L"连接测试已取消。");uiSetText(g->settings,settingTestButton,L"测试连接");}
            continue;
        }
        if(!isCurrentSourceEvent(event))continue;
        if(event.kind==TranslationEvent::Kind::delta){if(event.text.size()>maxTextBytes/sizeof(wchar_t)-g->result.size()){++g->revision;g->activeRequestId=0;stopMainRequest();g->requestActive=false;recoverCompleted(true);status(L"译文超过 1 MiB 安全上限，当前请求已取消。");updateButtons();continue;}g->result+=event.text;appendRichEditPreservingView(uiItem(g->window,resultEdit),event.text);}
        else if(event.kind==TranslationEvent::Kind::complete){
            g->requestActive=false;g->activeRequestId=0;
            TextVersion ver;
            ver.versionId=++g->versionSerial;
            ver.sourceRevision=g->sourceRevision;
            ver.targetLanguage=g->language;
            ver.opType=event.opType;
            ver.rewriteStyle=event.rewriteStyle;
            ver.customScope=event.customScope;
            ver.customPrompt=event.customPrompt;
            ver.modelProfileId=event.modelProfileId;
            ver.createdAt=std::chrono::system_clock::now();
            ver.resultText=g->result;
            ver.sourceSnapshot=g->source;ver.rawSnapshot=g->rawSource;
            ver.parentVersionId=event.baseVersionId;
            ver.completed=true;
            if(g->versions.size()>=g->config.versionLimit){
                auto it=g->versions.begin();
                while(it!=g->versions.end()&&it->versionId==g->currentVersionId)++it;
                if(it!=g->versions.end())g->versions.erase(it);
                else g->versions.pop_front();
            }
            g->versions.push_back(std::move(ver));
            g->currentVersionId=g->versionSerial;
            g->lastCompletedResult=g->result;g->lastCompletedLanguage=g->language;
            const wchar_t* doneMsg=event.opType==AiOperationType::rewrite?L"AI 改写完成。":event.opType==AiOperationType::naturalize?L"自然化表达完成。":event.opType==AiOperationType::custom?L"自定义指令处理完成。":L"翻译完成。";
            g->retryAvailable=false;g->lastErrorDetails.clear();status(doneMsg);updateButtons();
        }
        else if(event.kind==TranslationEvent::Kind::error){
            g->requestActive=false;g->activeRequestId=0;
            recoverCompleted(true);
            g->lastErrorDetails=errorDetailsText(event);g->retryAvailable=true;
            status(event.text);updateButtons();layoutMain();
        }
        else {
            g->requestActive=false;g->activeRequestId=0;
            recoverCompleted(true);
            status(L"翻译请求已取消。");updateButtons();
        }
    }
#if defined(SAT_ENABLE_TEST_HOOKS)
    if(!g->suppressEventReapingForTest)reapRetiredTasks();
#else
    reapRetiredTasks();
#endif
    layoutMain();
}
LRESULT CALLBACK mainProc(HWND hwnd,UINT m,WPARAM w,LPARAM l){
    try{
    switch(m){
    case WM_NCCREATE:g->window=hwnd;{std::lock_guard lock(g->uiQueue->mutex);g->uiQueue->window=hwnd;}return TRUE;
    case WM_CREATE:
        try{applyAppIcons(hwnd);createMainControls();layoutMain();setTheme();if(g->ready)SetEvent(g->ready);g->initialized=true;return 0;}catch(...){return -1;}
    case WM_ERASEBKGND:{RECT r{};GetClientRect(hwnd,&r);FillRect(reinterpret_cast<HDC>(w),&r,g->backgroundBrush?g->backgroundBrush:GetSysColorBrush(COLOR_WINDOW));return 1;}
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:return themedControlColor(m,w,l);
    case WM_DRAWITEM:{auto*item=reinterpret_cast<DRAWITEMSTRUCT*>(l);if(!item)break;if(item->CtlType==ODT_BUTTON){drawActionButton(*item);return TRUE;}if(item->CtlType==ODT_COMBOBOX&&item->CtlID==targetCombo){drawThemeComboItem(*item);return TRUE;}break;}
    case WM_SETTINGCHANGE:case WM_SYSCOLORCHANGE:case WM_THEMECHANGED:setTheme();return 0;
    case WM_NCCALCSIZE:return 0;
    case WM_NCPAINT:return 0;
    case WM_NCACTIVATE:return ui::activateClientFrame(hwnd,w);
    case WM_DWMCOMPOSITIONCHANGED:ui::useClientFrame(hwnd);return 0;
    case WM_NCHITTEST:return uiHitTest(hwnd,l,50,142);
    case WM_PAINT:{PAINTSTRUCT paint{};HDC dc=BeginPaint(hwnd,&paint);RECT r{};GetClientRect(hwnd,&r);FillRect(dc,&paint.rcPaint,g->backgroundBrush);const int dpi=static_cast<int>(uiDpi(hwnd));const auto frame=ui::mainFrame(r.right,r.bottom,dpi,g->editing);HPEN pen=CreatePen(PS_SOLID,std::max(1,ui::scale(1,dpi)),g->borderColor);HGDIOBJ oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,g->controlBrush);for(const auto& pane:{frame.sourceSurface,frame.resultSurface})if(pane.width>0&&pane.height>0){RECT face{pane.x,pane.y,pane.x+pane.width,pane.y+pane.height};RoundRect(dc,face.left,face.top,face.right,face.bottom,px(hwnd,12),px(hwnd,12));}MoveToEx(dc,px(hwnd,24),frame.toolbarY-px(hwnd,10),nullptr);LineTo(dc,r.right-px(hwnd,24),frame.toolbarY-px(hwnd,10));SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);drawTitleMark(dc,hwnd);EndPaint(hwnd,&paint);return 0;}
    case WM_SIZE:rememberNormalGeometry(hwnd);layoutMain();return 0;
    case WM_MOVE:rememberNormalGeometry(hwnd);return 0;
    case WM_DPICHANGED:{const auto*r=reinterpret_cast<const RECT*>(l);SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);refreshFonts(hwnd);layoutMain();setTheme();return 0;}
    case WM_GETMINMAXINFO:uiWindowLimits(hwnd,*reinterpret_cast<MINMAXINFO*>(l),620,360);return 0;
    case WM_COPYDATA:{auto*cd=reinterpret_cast<COPYDATASTRUCT*>(l);if(!cd||cd->dwData!=copyDataTag||cd->cbData<sizeof(wchar_t)||cd->cbData>2*maxTextBytes||cd->cbData%sizeof(wchar_t)||!cd->lpData)return FALSE;
        const auto*p=static_cast<const wchar_t*>(cd->lpData);const size_t n=cd->cbData/sizeof(wchar_t);if(p[n-1]!=L'\0')return FALSE;std::wstring payload(p,n-1);if(payload.find(L'\0')!=std::wstring::npos)return FALSE;
        constexpr std::wstring_view settingsPrefix=L"SAT/1:settings\n",showPrefix=L"SAT/1:show\n",textPrefix=L"SAT/1:text\n",snipPrefix=L"SAT/1:snip\n";
        if(payload==settingsPrefix)return queueIncoming({true,false})?TRUE:FALSE;
        if(payload==showPrefix)return queueIncoming({false,true})?TRUE:FALSE;
        if(payload==snipPrefix){traceCapture("shortcut_ipc","close_or_cancel");PostMessageW(hwnd,msgCaptureShortcutExit,0,0);return TRUE;}
#if defined(SAT_ENABLE_TEST_HOOKS)
        constexpr std::wstring_view testCandidatePrefix=L"SAT/1:test_candidate\n";
        constexpr std::wstring_view testSilentCandidatePrefix=L"SAT/1:test_candidate_silent\n";
        const bool silentCandidate=payload.rfind(testSilentCandidatePrefix,0)==0;
        if(testMode()&&(payload.rfind(testCandidatePrefix,0)==0||silentCandidate)){
            if(!g->captureSessionActive||!g->ocrAdapter||g->ocrAdapter->kind()!=OcrAdapterKind::reverseSnipaste)return FALSE;
            auto* adapter=static_cast<SnipasteReverseAdapter*>(g->ocrAdapter.get());
            g->testClipboardText=payload.substr(silentCandidate?testSilentCandidatePrefix.size():testCandidatePrefix.size());
            g->testClipboardSequence=adapter->baselineSequence()+1;if(!g->testClipboardSequence)g->testClipboardSequence=1;
            adapter->setClipboardReaderForTest([]{return g->testClipboardSequence;},[](std::wstring& text,DWORD& sequence){
                if(g->testClipboardBusy){g->testClipboardBusy=false;return ClipboardReadStatus::retry;}
                text=g->testClipboardText;sequence=g->testClipboardSequence;
                return SnipasteReverseAdapter::isMeaningfulText(text)?ClipboardReadStatus::text:ClipboardReadStatus::ignored;
            });
            if(!silentCandidate)PostMessageW(hwnd,WM_CLIPBOARDUPDATE,0,0);return TRUE;
        }
#endif
#if defined(SAT_ENABLE_TEST_HOOKS)
        constexpr std::wstring_view mockOcrPrefix=L"SAT/1:mock_ocr\n";
        if(payload.rfind(mockOcrPrefix,0)==0){if(!testMode())return FALSE;payload.erase(0,mockOcrPrefix.size());if(payload.empty()||payload.size()>maxTextBytes/sizeof(wchar_t)||!validText(payload))return FALSE;g->ocrAdapter=std::make_unique<MockOcrAdapter>(OcrSessionStatus::captured,std::move(payload),1);startOcrCapture();return TRUE;}
#endif
        if(payload.rfind(textPrefix,0)!=0)return FALSE;payload.erase(0,textPrefix.size());if(payload.empty()||payload.size()>maxTextBytes/sizeof(wchar_t)||!validText(payload))return FALSE;
        return queueIncoming({false,false,false,InputSource::manualPaste,std::move(payload)})?TRUE:FALSE;}
    case msgShowSettings:queueIncoming({true,false});return 0;
    case msgProcessPending:processPending();return 0;
    case msgCaptureShortcutExit:if(!g->exitRequested){g->exitRequested=true;invalidateOcrCapture();DestroyWindow(hwnd);PostQuitMessage(0);}return 0;
    case msgNetworkChunk:case msgNetworkDone:processNetworkEvents();return 0;
    case WM_CLIPBOARDUPDATE:if(g->clipboardListenerRegistered&&g->captureSessionActive){traceCapture("clipboard_notification");if(g->ocrConfirming)g->clipboardChangedWhileConfirming=true;else{g->clipboardRetryPending=false;pollOcrSession();}}return 0;
    case WM_TIMER:if(w==taskReaperTimerId){reapRetiredTasks(true);return 0;}if(w==ocrPollTimerId){monitorOcrSession();return 0;}break;
#if defined(SAT_ENABLE_TEST_HOOKS)
    case msgTestReaperControl:return handleTestReaperControl(w,l);
    case msgTestCaptureControl:
        if(!testMode())return FALSE;
        if(w==1){g->config.confirmOcrCandidates=l!=0;return TRUE;}
        if(w==2){g->ocrDeadline=GetTickCount64();return TRUE;}
        if(w==3){++g->testClipboardSequence;return TRUE;}
        if(w==4){g->testClipboardBusy=true;return TRUE;}
        if(w==5)return g->captureSessionActive||g->clipboardListenerRegistered||g->ocrConfirming;
        if(w==6){g->ocrAdapterSessionId+=1;return TRUE;}
        if(w==7){g->config.autoStart=l!=0;return TRUE;}
        if(w==8&&g->inputApplyDepthForTest==0&&g->ocrAdapter&&g->ocrAdapter->kind()==OcrAdapterKind::mock&&!g->captureSessionActive){static_cast<MockOcrAdapter*>(g->ocrAdapter.get())->configure(OcrSessionStatus::captured,g->source,MAXDWORD);return TRUE;}
        if(w==9)return static_cast<LRESULT>(g->inputSession.id);
        if(w==10)return static_cast<LRESULT>(g->ocrGeneration);
        return FALSE;
#endif
    case WM_COMMAND:{if(LOWORD(w)==sourceEdit&&HIWORD(w)==EN_CHANGE){InvalidateRect(uiItem(hwnd,sourceEdit),nullptr,FALSE);updateButtons();return 0;}if(LOWORD(w)!=targetCombo&&HIWORD(w)!=BN_CLICKED)return 0;switch(LOWORD(w)){
        case moreButton:{HMENU menu=CreatePopupMenu();if(!menu)return 0;for(int id:g->overflowActions){HWND h=uiItem(hwnd,id);const auto label=getText(h);AppendMenuW(menu,MF_STRING|(IsWindowEnabled(h)?MF_ENABLED:MF_GRAYED),id,label.c_str());}RECT r{};GetWindowRect(uiItem(hwnd,moreButton),&r);const int id=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_NONOTIFY,r.left,r.top,hwnd,nullptr);DestroyMenu(menu);if(id)SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),0);return 0;}
        case captureButton:if(HIWORD(w)==BN_CLICKED)startOcrCapture();return 0;
        case rewriteButton:showRewriteMenu(uiItem(hwnd,rewriteButton));return 0;
        case deAiButton:beginNaturalize();return 0;
        case customPromptButton:showCustomPromptPanel();return 0;
        case versionButton:showVersionMenu(uiItem(hwnd,versionButton));return 0;
        case targetCombo:if(HIWORD(w)==CBN_SELCHANGE){const int i=static_cast<int>(uiMessage(hwnd,targetCombo,CB_GETCURSEL,0,0));const wchar_t* codes[]={L"zh-Hans",L"zh-Hant",L"en",L"ja",L"ko",L"fr",L"de",L"es"};if(i>=0&&i<8&&g->language!=codes[i]){invalidateOcrCapture();cancelOperation();g->language=codes[i];g->config.targetLanguage=g->language;invalidateCompletedAssociation();g->result.clear();setText(uiItem(hwnd,resultEdit),L"");if(g->editing)status(L"目标语言已更改；提交原文后开始翻译。");else beginTranslate();updateButtons();layoutMain();}}return 0;
        case sourceCopyButton:status(copyClipboard(g->editing?getText(uiItem(hwnd,sourceEdit)):g->source)?L"原文已复制。":L"复制原文失败；剪贴板暂不可用或内容为空。");return 0;
        case resultCopyButton:if(g->result.empty())status(L"当前没有可复制的译文。");else status(copyClipboard(g->result)?L"当前译文已复制。":L"复制译文失败；剪贴板暂不可用。");return 0;
        case unlockButton:invalidateOcrCapture();cancelOperation();if(g->customPromptWnd)ShowWindow(g->customPromptWnd,SW_HIDE);g->editRollback=g->source;g->editing=true;ShowWindow(uiItem(hwnd,resultEdit),SW_HIDE);updatePanes();updateButtons();layoutMain();SetFocus(uiItem(hwnd,sourceEdit));SendMessageW(uiItem(hwnd,sourceEdit),EM_SETSEL,static_cast<WPARAM>(-1),static_cast<LPARAM>(-1));status(L"编辑原文后，按 Ctrl+Enter 或点击“重新翻译”提交。");return 0;
        case restoreButton:if(g->editing)g->source=g->rawSource;else acceptEditedSource(g->rawSource);updatePanes();status(L"已恢复本会话的初始原文。");return 0;
        case cancelEditButton:g->source=g->editRollback;g->editing=false;ShowWindow(uiItem(hwnd,resultEdit),SW_SHOW);updatePanes();updateButtons();layoutMain();status(L"已取消未提交的编辑。");processPending();return 0;
        case mainTranslateButton:case translateButton:{const bool submittedEdit=g->editing;auto text=getText(uiItem(hwnd,sourceEdit));if(trimCopy(text).empty()){status(L"原文为空，尚未发送请求。");return 0;}if(!validText(text)){status(L"原文格式无效或超过长度上限，尚未发送请求。");return 0;}if(g->editing||text!=g->source){if(g->source.empty()&&g->rawSource.empty())g->rawSource=text;acceptEditedSource(std::move(text));g->editing=false;ShowWindow(uiItem(hwnd,resultEdit),SW_SHOW);updatePanes();updateButtons();layoutMain();}beginTranslate();if(submittedEdit&&!g->pending.empty())PostMessageW(hwnd,msgProcessPending,0,0);return 0;}
        case requestCancelButton:if(g->requestActive){cancelOperation(true);status(L"请求已取消，已保留完整版本。");updateButtons();layoutMain();}return 0;
        case mainClose:SendMessageW(hwnd,WM_CLOSE,0,0);return 0;
        case mainMinimize:ShowWindow(hwnd,SW_MINIMIZE);return 0;
        case mainPin:g->config.alwaysOnTop=!g->config.alwaysOnTop;SetWindowPos(hwnd,g->config.alwaysOnTop?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);InvalidateRect(uiItem(hwnd,mainPin),nullptr,FALSE);return 0;
        case footerCopy:SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(resultCopyButton,BN_CLICKED),0);return 0;
        case retryButton:retryLastOperation();return 0;
        case errorDetailsButton:showErrorDetails(hwnd,g->lastErrorDetails);return 0;
        case settingsButton:openSettings();return 0;
        }}return 0;
    case WM_KEYDOWN:if(w==VK_ESCAPE){if(g->customPromptWnd&&childVisible(g->customPromptWnd)){ShowWindow(g->customPromptWnd,SW_HIDE);SetFocus(uiItem(hwnd,resultEdit));return 0;}if(g->captureSessionActive){cancelOcrCapture();return 0;}if(g->editing)SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(cancelEditButton,BN_CLICKED),0);else DestroyWindow(hwnd);return 0;}break;
    case WM_SYSKEYDOWN:break;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:{
        traceCapture("main_destroyed");
        invalidateOcrCapture();
        if(g->customPromptWnd&&IsWindow(g->customPromptWnd)){DestroyWindow(g->customPromptWnd);g->customPromptWnd=nullptr;}
        invalidateInputSession();for(auto& incoming:g->pending)if(incoming.cancelMarker)incoming.cancelMarker->cancel();++g->revision;g->requestActive=false;stopMainRequest();stopTestRequest();
        {std::lock_guard lock(g->uiQueue->mutex);g->uiQueue->alive=false;g->uiQueue->window=nullptr;g->uiQueue->events.clear();}
        rememberNormalGeometry(hwnd);std::wstring error;if(!g->config.damagedConfig&&g->config.warning.empty())(void)saveConfig(g->config,error);
        if(g->taskReaperWindow==hwnd){KillTimer(hwnd,taskReaperTimerId);g->taskReaperWindow=nullptr;}g->window=nullptr;scheduleTaskReaper();if(!g->settings)PostQuitMessage(0);return 0;}
    }
    return DefWindowProcW(hwnd,m,w,l);
    }catch(...){traceCapture("ui_dispatch_failed","exception");if(g&&g->window&&IsWindow(g->window))uiSetText(g->window,statusLabel,L"操作失败，请重试。");return 0;}
}

HWND settingLabel(HWND w,int id,const wchar_t* text){HWND h=CreateWindowExW(0,L"STATIC",text,WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,10,10,w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),g->instance,nullptr);if(!h)throw std::runtime_error("settings label creation failed");SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(fontForRole(w,textRole(w,id))),TRUE);return h;}
HWND settingEdit(HWND w,int id,const std::wstring& value,int x,int y,int width,DWORD extra=0){HWND h=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",value.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL|extra,x,y,width,px(w,28),w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),g->instance,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(fontForRole(w,textRole(w,id))),TRUE);return h;}
ThinkingMode selectedThinkingMode(HWND w){const int selected=static_cast<int>(uiMessage(w,settingThinkingMode,CB_GETCURSEL,0,0));return selected>=0&&selected<=static_cast<int>(ThinkingMode::enabled)?static_cast<ThinkingMode>(selected):ThinkingMode::automatic;}
void setThinkingModeSelection(HWND w,ThinkingMode mode){uiMessage(w,settingThinkingMode,CB_SETCURSEL,static_cast<WPARAM>(mode),0);}
void updateThinkingModeHint(HWND w,std::wstring_view baseUrl,std::wstring_view model,ThinkingMode mode){
    const bool supported=detectThinkingProtocol(baseUrl,model)==ThinkingProtocol::deepSeekChatCompletions;
    const wchar_t* hint=supported
        ?(mode==ThinkingMode::automatic?L"DeepSeek 官方 deepseek-flash：自动会省略开关并沿用服务商默认值。":L"DeepSeek 官方 deepseek-flash：本次请求会显式发送思考模式开关。")
        :(mode==ThinkingMode::automatic?L"当前协议未适配思考开关；自动模式会省略 DeepSeek 专用参数。":L"当前接口不支持强制开/关思考模式；请改为自动或使用 DeepSeek 官方 deepseek-flash。");
    uiSetText(w,settingThinkingMode+2000,hint);
}
constexpr const wchar_t* helpText=
    L"关于\r\nOCR AI Translator " SAT_VERSION_WIDE L"\r\n一款极轻量、可扩展 OCR 来源、支持自定义 AI 模型的 Windows 翻译与文本处理工具。采用 C++20 与 Win32，关闭后无本工具常驻进程。当前 Snipaste OCR 适配器仍待实机验收，本项目是独立工具。Apache-2.0 许可。\r\n\r\n"
    L"快速开始\r\n按 Ctrl+V 粘贴文字，选择目标语言后翻译。需要配置模型时，打开模型与 API，填写服务地址、模型 ID 和 API Key。\r\n\r\n"
    L"截图翻译\r\n当前 OCR 来源为 Snipaste。在“常规”页设置 OCR 来源可执行文件路径，再点“截图翻译”框选文字。开启候选确认时先核对文字；关闭时自动采用，并按“自动翻译”设置处理。原文到达后会显示窗口，也可点击目标语言后的“翻译”手动提交。按 Esc 取消等待。\r\n\r\n"
    L"模型与 API\r\n服务地址需兼容 OpenAI Chat Completions。可管理多个模型配置，并分别绑定翻译和改写用途。API Key 使用当前 Windows 用户的 DPAPI 加密保存。测试连接会发送少量请求并消耗 API 用量；保存可保留配置，取消会丢弃本次未保存的修改。\r\n\r\n"
    L"错误恢复\r\n输出截断时，可提高输出 Token 上限；部分模型的思考 Token 也会占用此上限。内容过滤时，可调整文本或更换模型。连接中断后可重试。错误详情只显示状态和错误代码，不包含正文、API Key 或 Authorization。失败或不完整结果不会加入版本历史；已有完整译文会恢复显示。\r\n\r\n"
    L"快捷键\r\nCtrl+V：载入剪贴板；编辑状态使用原生粘贴。Ctrl+Enter：提交原文编辑或执行自定义指令。Ctrl+R：重新翻译。Ctrl+Shift+C：复制译文。Tab / Shift+Tab：移动焦点。Esc：关闭帮助或指令面板、取消截图等待或结束编辑。";
void applyHelpHeadings(HWND window){HWND text=uiItem(window,settingIntro);if(!text)return;CHARRANGE selection{};SendMessageW(text,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
        for(const wchar_t* heading:{L"关于",L"快速开始",L"截图翻译",L"模型与 API",L"错误恢复",L"快捷键"}){const std::wstring paragraph=std::wstring(heading)+L"\r";FINDTEXTEXW find{{0,-1},const_cast<wchar_t*>(paragraph.c_str()),{}};if(SendMessageW(text,EM_FINDTEXTEXW,FR_DOWN|FR_MATCHCASE,reinterpret_cast<LPARAM>(&find))>=0){--find.chrgText.cpMax;SendMessageW(text,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&find.chrgText));CHARFORMAT2W style{};style.cbSize=sizeof(style);style.dwMask=CFM_BOLD|CFM_SIZE;style.dwEffects=CFE_BOLD;style.yHeight=ui::typography::style(ui::typography::Role::heading).sizeDip*15;SendMessageW(text,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&style));}}SendMessageW(text,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));}
void syncHelpScroll(HWND w){
    if(!w)return;HWND edit=uiItem(w,settingIntro),scroll=uiItem(w,helpScroll);if(!edit||!scroll)return;
    RECT area{};GetClientRect(edit,&area);HDC dc=GetDC(edit);TEXTMETRICW metrics{};HFONT font=fontFor(edit);HGDIOBJ old=dc?SelectObject(dc,font):nullptr;if(dc)GetTextMetricsW(dc,&metrics);if(dc){if(old)SelectObject(dc,old);ReleaseDC(edit,dc);}
    const int dpi=static_cast<int>(uiDpi(edit));const int lineHeight=std::max(ui::scale(ui::typography::style(ui::typography::Role::heading).lineDip,dpi),static_cast<int>(metrics.tmHeight+metrics.tmExternalLeading));const int visible=std::max(1,static_cast<int>(area.bottom)/std::max(1,lineHeight));
    const int lines=std::max(1,static_cast<int>(SendMessageW(edit,EM_GETLINECOUNT,0,0))),first=std::max(0,static_cast<int>(SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0))),maxPosition=std::max(0,lines-visible);
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};info.nMin=0;info.nMax=lines-1;info.nPage=static_cast<UINT>(visible);info.nPos=std::clamp(first,0,maxPosition);SetScrollInfo(scroll,SB_CTL,&info,TRUE);ShowWindow(scroll,maxPosition>0?SW_SHOW:SW_HIDE);InvalidateRect(scroll,nullptr,FALSE);
}
void scrollHelpTo(HWND w,int position){
    HWND edit=uiItem(w,settingIntro),scroll=uiItem(w,helpScroll);if(!edit||!scroll)return;SCROLLINFO info{sizeof(info),SIF_PAGE|SIF_RANGE};GetScrollInfo(scroll,SB_CTL,&info);const int lines=std::max(1,static_cast<int>(SendMessageW(edit,EM_GETLINECOUNT,0,0))),page=std::max(1,static_cast<int>(info.nPage)),current=std::max(0,static_cast<int>(SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0))),target=std::clamp(position,0,std::max(0,lines-page));SendMessageW(edit,EM_LINESCROLL,0,target-current);syncHelpScroll(w);
}
LRESULT CALLBACK helpRichEditProc(HWND edit,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    const LRESULT result=DefSubclassProc(edit,message,wp,lp);if(message==WM_MOUSEWHEEL||message==WM_KEYDOWN||message==WM_VSCROLL||message==EM_LINESCROLL)syncHelpScroll(GetParent(edit));return result;
}
void layoutHelp(HWND w){
    RECT r{};GetClientRect(w,&r);const int dpi=static_cast<int>(uiDpi(w)),p24=ui::scale(24,dpi),scrollWidth=ui::scale(12,dpi),scrollGap=ui::scale(8,dpi);
    MoveWindow(uiItem(w,settingHeader),p24+ui::scale(36,dpi),ui::scale(16,dpi),ui::scale(250,dpi),ui::scale(32,dpi),TRUE);MoveWindow(uiItem(w,settingClose),r.right-ui::scale(62,dpi),ui::scale(14,dpi),ui::scale(34,dpi),ui::scale(34,dpi),TRUE);
    const int top=ui::scale(68,dpi),bottom=ui::scale(24,dpi),scrollX=static_cast<int>(r.right)-p24-scrollWidth;MoveWindow(uiItem(w,settingIntro),p24,top,std::max(1,scrollX-scrollGap-p24),std::max(1,static_cast<int>(r.bottom)-top-bottom),TRUE);MoveWindow(uiItem(w,helpScroll),scrollX,top,scrollWidth,std::max(1,static_cast<int>(r.bottom)-top-bottom),TRUE);syncHelpScroll(w);
}
LRESULT CALLBACK helpProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    switch(m){
    case WM_NCCREATE:g->help=w;return TRUE;
    case WM_CREATE:{applyAppIcons(w);settingLabel(w,settingHeader,L"帮助与关于");SendMessageW(uiItem(w,settingHeader),WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(w,true)),TRUE);control(0,L"BUTTON",L"×",WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY,settingClose,w);HWND text=control(0,MSFTEDIT_CLASS,helpText,WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY|ES_NOHIDESEL,settingIntro,w);SendMessageW(text,EM_EXLIMITTEXT,0,maxTextBytes/sizeof(wchar_t));SendMessageW(text,EM_SETTARGETDEVICE,0,0);if(!SetWindowSubclass(text,helpRichEditProc,14,0))throw std::runtime_error("help text scroll synchronization failed");HWND scroll=control(0,L"SCROLLBAR",L"",SBS_VERT|WS_TABSTOP,helpScroll,w);SetWindowSubclass(scroll,nativeScrollProc,12,0);
        applyHelpHeadings(w);SendMessageW(text,EM_SETSEL,0,0);layoutHelp(w);return 0;}
    case WM_ERASEBKGND:{RECT r{};GetClientRect(w,&r);FillRect(reinterpret_cast<HDC>(wp),&r,g->backgroundBrush);return 1;}
    case WM_PAINT:{PAINTSTRUCT paint{};HDC dc=BeginPaint(w,&paint);FillRect(dc,&paint.rcPaint,g->backgroundBrush);drawTitleMark(dc,w);EndPaint(w,&paint);return 0;}
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:return themedControlColor(m,wp,lp);
    case WM_DRAWITEM:if(auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lp)){drawActionButton(*item);return TRUE;}break;
    case WM_SIZE:layoutHelp(w);return 0;
    case WM_DPICHANGED:{const auto* r=reinterpret_cast<const RECT*>(lp);SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);refreshFonts(w);layoutHelp(w);setTheme();return 0;}
    case WM_GETMINMAXINFO:uiWindowLimits(w,*reinterpret_cast<MINMAXINFO*>(lp),440,320);return 0;
    case WM_NCCALCSIZE:return 0;
    case WM_NCPAINT:return 0;
    case WM_NCACTIVATE:return ui::activateClientFrame(w,wp);
    case WM_DWMCOMPOSITIONCHANGED:ui::useClientFrame(w);return 0;
    case WM_NCHITTEST:return uiHitTest(w,lp,54,86);
    case WM_COMMAND:if(LOWORD(wp)==settingIntro&&HIWORD(wp)==EN_VSCROLL){syncHelpScroll(w);return 0;}if(LOWORD(wp)==settingClose&&HIWORD(wp)==BN_CLICKED){DestroyWindow(w);return 0;}break;
    case WM_VSCROLL:if(reinterpret_cast<HWND>(lp)==uiItem(w,helpScroll)){SCROLLINFO info{sizeof(info),SIF_TRACKPOS|SIF_PAGE};GetScrollInfo(uiItem(w,helpScroll),SB_CTL,&info);const int current=static_cast<int>(SendMessageW(uiItem(w,settingIntro),EM_GETFIRSTVISIBLELINE,0,0));const int page=std::max(1,static_cast<int>(info.nPage));int target=current;switch(LOWORD(wp)){case SB_LINEUP:--target;break;case SB_LINEDOWN:++target;break;case SB_PAGEUP:target-=page;break;case SB_PAGEDOWN:target+=page;break;case SB_TOP:target=0;break;case SB_BOTTOM:target=static_cast<int>(SendMessageW(uiItem(w,settingIntro),EM_GETLINECOUNT,0,0));break;case SB_THUMBTRACK:case SB_THUMBPOSITION:target=info.nTrackPos;break;default:return 0;}scrollHelpTo(w,target);return 0;}break;
    case WM_MOUSEWHEEL:{const int notches=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA;if(notches){const int current=static_cast<int>(SendMessageW(uiItem(w,settingIntro),EM_GETFIRSTVISIBLELINE,0,0));scrollHelpTo(w,current-notches*3);}return 0;}
    case WM_KEYDOWN:if(wp==VK_ESCAPE){DestroyWindow(w);return 0;}break;
    case WM_CLOSE:DestroyWindow(w);return 0;
    case WM_DESTROY:g->help=nullptr;if(g->settings&&IsWindow(g->settings)){SetForegroundWindow(g->settings);SetFocus(uiItem(g->settings,settingHelp));}return 0;
    }
    return DefWindowProcW(w,m,wp,lp);
}
void openHelp(){
    if(g->help){SetForegroundWindow(g->help);return;}if(!g->settings)return;
    RECT owner{};GetWindowRect(g->settings,&owner);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(g->settings,MONITOR_DEFAULTTONEAREST),&monitor);
    const int width=std::min(px(g->settings,700),static_cast<int>(monitor.rcWork.right-monitor.rcWork.left)),height=std::min(px(g->settings,540),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top));
    HWND help=CreateWindowExW(WS_EX_CONTROLPARENT,L"SAT.Help",L"使用帮助 · Translator",WS_POPUP|WS_THICKFRAME|WS_SYSMENU|WS_CLIPCHILDREN,std::clamp(static_cast<int>(owner.left)+px(g->settings,32),static_cast<int>(monitor.rcWork.left),static_cast<int>(monitor.rcWork.right)-width),std::clamp(static_cast<int>(owner.top)+px(g->settings,32),static_cast<int>(monitor.rcWork.top),static_cast<int>(monitor.rcWork.bottom)-height),width,height,g->settings,nullptr,g->instance,nullptr);
    if(!help)throw std::runtime_error("help window creation failed");setTheme();ShowWindow(help,testMode()&&!showTestUi()?SW_HIDE:SW_SHOW);SetFocus(uiItem(help,settingIntro));
}
void layoutSettings(HWND w){
    if(!g->settingsViewport)return;RECT client{};GetClientRect(w,&client);const int dpi=static_cast<int>(uiDpi(w));auto p=[&](int dip){return ui::scale(dip,dpi);};const auto frame=ui::settingsFrame(client.right,client.bottom,dpi);
    const bool scrollNeeded=ui::settingsPage(g->settingsTab,frame.viewport.width,frame.viewport.height,dpi,0).content>frame.viewport.height;
    SetWindowPos(g->settingsViewport,nullptr,frame.viewport.x,frame.viewport.y,std::max(0,frame.viewport.width-(scrollNeeded?p(18):0)),frame.viewport.height,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS);
    SetWindowPos(g->settingsScroll,nullptr,client.right-p(18),frame.viewport.y,p(18),frame.viewport.height,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS);
    RECT view{};GetClientRect(g->settingsViewport,&view);const auto page=ui::settingsPage(g->settingsTab,view.right,view.bottom,dpi,g->settingsScrollDip);g->settingsScrollDip=MulDiv(page.scroll,96,dpi);ShowWindow(g->settingsScroll,page.content>view.bottom?SW_SHOW:SW_HIDE);
    SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,page.content-1),static_cast<UINT>(view.bottom),page.scroll,0};SetScrollInfo(g->settingsScroll,SB_CTL,&si,TRUE);
    HDWP batch=BeginDeferWindowPos(static_cast<int>(page.items.size()));
    for(const auto& item:page.items){HWND child=uiItem(w,item.id);if(!child)continue;if(batch)batch=DeferWindowPos(batch,child,nullptr,item.rect.x,item.rect.y,item.rect.width,item.rect.height,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS);else SetWindowPos(child,nullptr,item.rect.x,item.rect.y,item.rect.width,item.rect.height,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS);}
    if(batch&&!EndDeferWindowPos(batch))throw std::runtime_error("settings batch layout failed");
    for(HWND child=GetWindow(g->settingsViewport,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){const int id=GetDlgCtrlID(child);const bool visible=std::any_of(page.items.begin(),page.items.end(),[&](const ui::Placement& item){return item.id==id;});ShowWindow(child,visible?SW_SHOW:SW_HIDE);}
    auto move=[&](int id,int x,int y,int width,int height){SetWindowPos(uiItem(w,id),nullptr,x,y,std::max(0,width),height,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS);};
    move(settingHeader,p(60),p(16),p(200),p(32));move(settingHelp,client.right-p(108),p(14),p(34),p(34));move(settingClose,client.right-p(62),p(14),p(34),p(34));
    int x=p(24);for(int tab=0;tab<5;++tab){const int width=p(tab==3?116:82);move(settingTabGeneral+tab,x,p(62),width,p(34));x+=width+p(4);}
    const bool hasErrorDetails=!g->settingsErrorDetails.empty();ShowWindow(uiItem(w,settingErrorDetails),hasErrorDetails?SW_SHOW:SW_HIDE);
    move(settingCancel,client.right-p(202),client.bottom-p(54),p(78),p(36));move(settingSave,client.right-p(112),client.bottom-p(54),p(88),p(36));move(settingErrorDetails,client.right-p(292),client.bottom-p(54),p(78),p(36));move(settingStatus,p(24),frame.footer.y+p(12),std::max(0,static_cast<int>(client.right)-p(hasErrorDetails?340:250)),p(52));
    RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
}
void scrollSettings(HWND w,int deltaDip){g->settingsScrollDip=std::max(0,g->settingsScrollDip+deltaDip);layoutSettings(w);}
bool isSettingsCheckbox(int id)noexcept{return(id>=settingTop&&id<=settingRetranslate)||id==settingAutoTranslate||(id>=settingRewrite&&id<=settingVersionHistory)||id==settingTransRole||id==settingRewriteRole||id==settingEnabled||id==settingRememberPrompts||id==settingOcrConfirmation;}
LRESULT CALLBACK settingsProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    try{
    switch(m){case WM_NCCREATE:g->settings=w;return TRUE;case WM_CREATE:{
        g->settingsTab=0;g->settingsScrollDip=0;g->settingsWheelRemainder=0;g->previewTheme=g->config.theme;
        g->settingsViewport=CreateWindowExW(WS_EX_CONTROLPARENT,L"SAT.SettingsViewport",L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,10,10,w,reinterpret_cast<HMENU>(350),g->instance,nullptr);
        if(!g->settingsViewport)throw std::runtime_error("settings viewport creation failed");g->settingsScroll=control(0,L"SCROLLBAR",L"",SBS_VERT|WS_TABSTOP,355,w);SetWindowSubclass(g->settingsScroll,nativeScrollProc,12,0);

        applyAppIcons(w);settingLabel(w,settingHeader,L"设置中心");SendMessageW(uiItem(w,settingHeader),WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(w,true)),TRUE);
        const wchar_t* tabs[]={L"常规",L"原文栏",L"译文栏",L"模型与 API",L"高级"};for(int i=0;i<5;++i)control(0,L"BUTTON",tabs[i],WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY,settingTabGeneral+i,w);
        control(0,L"BUTTON",L"?",WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY,settingHelp,w);tooltip(uiItem(w,settingHelp),L"关于、快速开始、OCR 来源、模型与 API、快捷键及故障排除");
        control(0,L"BUTTON",L"×",WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY,settingClose,w);
        control(0,L"BUTTON",L"选择 EXE",WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY,settingBrowse,w);
        auto heading=[&](int id,const wchar_t* text){HWND h=CreateWindowExW(0,L"STATIC",text,WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,10,10,w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),g->instance,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(fontForRole(w,textRole(w,id))),TRUE);};
        heading(settingGeneralTitle,L"窗口与输入");heading(settingSourceTitle,L"原文操作");heading(settingResultTitle,L"译文与工具栏");heading(settingModelTitle,L"模型与 API");heading(settingAdvancedTitle,L"会话与隐私");
        settingLabel(w,settingTargetLabel,L"目标语言");settingLabel(w,settingThemeLabel,L"主题");settingLabel(w,settingBaseUrlLabel,L"Base URL");settingLabel(w,settingModelLabel,L"模型 ID");settingLabel(w,settingApiKeyLabel,L"API Key（DPAPI 加密）");settingLabel(w,settingTimeoutLabel,L"请求超时（秒）");settingLabel(w,settingTokensLabel,L"输出 Token 上限");
        settingLabel(w,settingProfileComboLabel,L"配置方案");settingLabel(w,settingProfileNameLabel,L"方案名称");settingLabel(w,settingThinkingModeLabel,L"思考模式");
        settingLabel(w,settingVersionLimitLabel,L"会话版本上限（1–50）");settingLabel(w,settingToolbarOrderLabel,L"工具栏顺序");
        settingLabel(w,settingOcrSourceLabel,L"OCR 来源");settingLabel(w,settingOcrSource+2000,L"当前仅接入 Snipaste 适配器");
        HWND ocrSourceValue=CreateWindowExW(0,L"STATIC",L"Snipaste",WS_CHILD|WS_VISIBLE|SS_LEFT|SS_CENTERIMAGE,0,0,10,10,w,reinterpret_cast<HMENU>(settingOcrSource),g->instance,nullptr);if(!ocrSourceValue)throw std::runtime_error("OCR source value control initialization failed");SendMessageW(ocrSourceValue,WM_SETFONT,reinterpret_cast<WPARAM>(fontForRole(w,textRole(w,settingOcrSource))),TRUE);
        settingLabel(w,settingSnipastePathLabel,L"OCR 来源可执行文件路径");
        HWND snipastePath=settingEdit(w,settingSnipastePath,g->config.snipasteExePath,0,0,10);
        SendMessageW(snipastePath,EM_SETLIMITTEXT,32700,0);
        g->settingsPathTip=g->config.snipasteExePath;tooltip(snipastePath,g->settingsPathTip.c_str());
        g->settingsToolbarOrder=g->config.toolbarOrder;
        auto order=control(WS_EX_STATICEDGE,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL,settingToolbarOrder,w);
        for(int id:g->settingsToolbarOrder){auto name=getText(uiItem(g->window,id));SendMessageW(order,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));}SendMessageW(order,CB_SETCURSEL,0,0);
        control(0,L"BUTTON",L"上移",WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY,settingMoveUp,w);control(0,L"BUTTON",L"下移",WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY,settingMoveDown,w);
        settingEdit(w,settingVersionLimit,std::to_wstring(g->config.versionLimit),0,0,10,ES_NUMBER);
        auto target=CreateWindowExW(WS_EX_STATICEDGE,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,0,0,10,10,w,reinterpret_cast<HMENU>(settingTarget),g->instance,nullptr);SendMessageW(target,CB_SETITEMHEIGHT,0,px(w,30));SendMessageW(target,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),px(w,30));
        const wchar_t* names[]={L"简体中文",L"繁體中文",L"English",L"日本語",L"한국어",L"Français",L"Deutsch",L"Español"};const wchar_t* codes[]={L"zh-Hans",L"zh-Hant",L"en",L"ja",L"ko",L"fr",L"de",L"es"};
        SendMessageW(target,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(w)),TRUE);for(int i=0;i<8;++i){SendMessageW(target,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(names[i]));if(g->config.targetLanguage==codes[i])SendMessageW(target,CB_SETCURSEL,i,0);}
        auto theme=CreateWindowExW(WS_EX_STATICEDGE,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,0,0,10,10,w,reinterpret_cast<HMENU>(settingTheme),g->instance,nullptr);SendMessageW(theme,CB_SETITEMHEIGHT,0,px(w,30));SendMessageW(theme,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),px(w,30));SendMessageW(theme,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(w)),TRUE);const wchar_t* themes[]={L"跟随系统",L"浅色",L"深色"};for(int i=0;i<3;++i){SendMessageW(theme,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(themes[i]));if(i==(g->config.theme==Theme::dark?2:g->config.theme==Theme::light?1:0))SendMessageW(theme,CB_SETCURSEL,i,0);}
        g->settingsTranslationId=g->config.translationProfileId;g->settingsRewriteId=g->config.rewriteProfileId;g->settingsWorkingProfiles=g->config.profiles;if(g->settingsWorkingProfiles.empty()){g->config.syncProfilesFromActive();g->settingsTranslationId=g->config.translationProfileId;g->settingsRewriteId=g->config.rewriteProfileId;g->settingsWorkingProfiles=g->config.profiles;}g->settingsThinkingExplicit.clear();for(const auto& profile:g->settingsWorkingProfiles)g->settingsThinkingExplicit.push_back(profile.thinkingModeExplicit);
        g->currentSettingsProfileIndex=0;for(size_t i=0;i<g->settingsWorkingProfiles.size();++i){if(g->settingsWorkingProfiles[i].id==g->settingsTranslationId){g->currentSettingsProfileIndex=i;break;}}
        auto profileCombo=CreateWindowExW(WS_EX_STATICEDGE,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,0,0,10,10,w,reinterpret_cast<HMENU>(settingProfileCombo),g->instance,nullptr);SendMessageW(profileCombo,CB_SETITEMHEIGHT,0,px(w,28));SendMessageW(profileCombo,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),px(w,28));SendMessageW(profileCombo,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(w)),TRUE);
        for(const auto& p:g->settingsWorkingProfiles){std::wstring item=p.name.empty()?p.id:p.name;SendMessageW(profileCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item.c_str()));}
        SendMessageW(profileCombo,CB_SETCURSEL,static_cast<WPARAM>(g->currentSettingsProfileIndex),0);
        HWND addBtn=CreateWindowExW(0,L"BUTTON",L"新建",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY|BS_PUSHBUTTON,0,0,10,10,w,reinterpret_cast<HMENU>(settingAddProfile),g->instance,nullptr);SendMessageW(addBtn,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(w)),TRUE);
        HWND delBtn=CreateWindowExW(0,L"BUTTON",L"删除",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY|BS_PUSHBUTTON,0,0,10,10,w,reinterpret_cast<HMENU>(settingDeleteProfile),g->instance,nullptr);SendMessageW(delBtn,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(w)),TRUE);
        const auto& curProf=g->settingsWorkingProfiles[g->currentSettingsProfileIndex];
        settingEdit(w,settingProfileName,curProf.name,0,0,10);
        settingEdit(w,settingBaseUrl,curProf.baseUrl,0,0,10);settingEdit(w,settingModel,curProf.model,0,0,10);settingEdit(w,settingApiKey,curProf.apiKey,0,0,10,ES_PASSWORD);settingEdit(w,settingTimeout,std::to_wstring(curProf.timeoutSeconds),0,0,10,ES_NUMBER);settingEdit(w,settingTokens,std::to_wstring(curProf.maxOutputTokens),0,0,10,ES_NUMBER);
        HWND thinking=CreateWindowExW(WS_EX_STATICEDGE,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,0,0,10,10,w,reinterpret_cast<HMENU>(settingThinkingMode),g->instance,nullptr);if(!thinking)throw std::runtime_error("thinking mode combo initialization failed");SendMessageW(thinking,CB_SETITEMHEIGHT,0,px(w,30));SendMessageW(thinking,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),px(w,30));SendMessageW(thinking,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(w)),TRUE);const wchar_t* thinkingOptions[]={L"自动（服务商默认）",L"关闭",L"开启"};for(const auto* option:thinkingOptions)SendMessageW(thinking,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(option));setThinkingModeSelection(w,curProf.thinkingMode);
        auto check=[&](int id,const wchar_t* t,bool value){HWND h=CreateWindowExW(0,L"BUTTON",t,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY,0,0,10,10,w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),g->instance,nullptr);if(!SetWindowSubclass(h,settingsCheckboxProc,1,0))throw std::runtime_error("settings checkbox initialization failed");SendMessageW(h,BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(w)),TRUE);HWND label=CreateWindowExW(0,L"STATIC",t,WS_CHILD|WS_VISIBLE|SS_LEFT|SS_NOTIFY,0,0,10,10,w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id+1000)),g->instance,nullptr);if(!label)throw std::runtime_error("settings checkbox label initialization failed");SendMessageW(label,WM_SETFONT,reinterpret_cast<WPARAM>(g->settingsFonts.body),TRUE);};
        check(settingTop,L"浮窗置顶",g->config.alwaysOnTop);check(settingRemember,L"记忆窗口位置",g->config.rememberPosition);check(settingAutoTranslate,L"自动翻译",g->config.autoStart);check(settingSourceCopy,L"显示复制原文",g->config.showSourceCopy);check(settingUnlock,L"显示解锁原文",g->config.showUnlock);check(settingRestore,L"显示恢复原文",g->config.showRestore);check(settingResultCopy,L"显示复制译文",g->config.showResultCopy);check(settingRetranslate,L"显示重新翻译",g->config.showRetranslate);
        check(settingRewrite,L"显示 AI 改写",g->config.showRewrite);check(settingDeAi,L"显示自然化表达",g->config.showDeAi);check(settingCustomPrompt,L"显示自定义指令",g->config.showCustomPrompt);check(settingVersionHistory,L"显示版本历史",g->config.showVersionHistory);
        check(settingTransRole,L"用于日常翻译",curProf.id==g->settingsTranslationId);
        check(settingRewriteRole,L"用于 AI 改写与自然化表达",curProf.id==g->settingsRewriteId);
        check(settingEnabled,L"启用当前模型配置",curProf.enabled);
        check(settingRememberPrompts,L"本次运行记住最近 5 条自定义指令",g->config.rememberPrompts);check(settingOcrConfirmation,L"截图翻译候选确认",g->config.confirmOcrCandidates);
        HWND save=CreateWindowExW(0,L"BUTTON",L"保存",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY|BS_DEFPUSHBUTTON,0,0,10,10,w,reinterpret_cast<HMENU>(settingSave),g->instance,nullptr);SendMessageW(save,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(w)),TRUE);HWND cancel=CreateWindowExW(0,L"BUTTON",L"取消",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY|BS_PUSHBUTTON,0,0,10,10,w,reinterpret_cast<HMENU>(settingCancel),g->instance,nullptr);SendMessageW(cancel,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(w)),TRUE);HWND errorDetails=CreateWindowExW(0,L"BUTTON",L"查看详情",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY|BS_PUSHBUTTON,0,0,10,10,w,reinterpret_cast<HMENU>(settingErrorDetails),g->instance,nullptr);SendMessageW(errorDetails,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(w)),TRUE);
        HWND test=CreateWindowExW(0,L"BUTTON",L"测试连接",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY|BS_PUSHBUTTON,0,0,10,10,w,reinterpret_cast<HMENU>(settingTestButton),g->instance,nullptr);SendMessageW(test,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(w)),TRUE);
        HWND statusControl=CreateWindowExW(0,L"STATIC",g->config.warning.c_str(),WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,10,10,w,reinterpret_cast<HMENU>(settingStatus),g->instance,nullptr);SendMessageW(statusControl,WM_SETFONT,reinterpret_cast<WPARAM>(g->settingsFonts.note),TRUE);
        const std::pair<int,const wchar_t*> descriptions[]={
            {settingTop,L"让翻译窗口保持在其他窗口之上"},{settingRemember,L"下次启动恢复上次窗口位置"},{settingAutoTranslate,L"原文就绪后开始翻译；开启候选确认时先核对"},
            {settingSourceCopy,L"在原文标题旁显示复制按钮"},{settingUnlock,L"解锁后在整宽区域编辑，Ctrl+Enter 提交"},{settingRestore,L"编辑时恢复当前会话的初始原文"},
            {settingResultCopy,L"在标题与工具栏显示译文复制入口"},{settingRetranslate,L"手动重新翻译，Ctrl+R 同样可用"},{settingRewrite,L"对完整译文选择五种改写风格"},{settingDeAi,L"减少翻译腔和模板化措辞"},{settingCustomPrompt,L"选择原文或译文作为自定义指令的处理范围"},{settingVersionHistory,L"查看并恢复当前会话的完整译文"},
            {settingTransRole,L"翻译操作使用当前模型配置"},{settingRewriteRole,L"AI 改写、自然化表达和自定义指令使用此配置"},{settingEnabled,L"关闭后不会自动改用其他服务"},{settingRememberPrompts,L"仅存于内存；退出后清除"},{settingOcrConfirmation,L"开启时先核对文字；关闭时按自动翻译设置处理"},
            {settingTarget,L"新会话使用的目标语言"},{settingTheme,L"立即预览深浅主题，保存后生效"},{settingOcrSource,L"当前仅接入 Snipaste 适配器"},{settingSnipastePath,L"完整 EXE 路径，无引号或参数。"},
            {settingProfileCombo,L"各方案独立保存服务地址、模型、凭据、思考模式和用途"},{settingProfileName,L"给当前模型方案一个易于识别的名称"},{settingBaseUrl,L"OpenAI Chat Completions 兼容服务地址"},{settingModel,L"填写服务商提供的准确模型 ID"},{settingThinkingMode,L"每个配置方案独立保存；其他兼容接口仅支持自动模式"},{settingApiKey,L"在本机使用 DPAPI 加密保存"},{settingTimeout,L"允许 5–180 秒；连接测试最多 30 秒"},{settingTokens,L"允许 1–32768；思考 Token 也计入输出上限"},{settingVersionLimit,L"仅保留当前会话的 1–50 个完整版本"},{settingToolbarOrder,L"调整操作顺序；复制和解锁位于栏标题旁"}};
        for(const auto& entry:descriptions)settingLabel(w,entry.first+2000,entry.second);
        updateThinkingModeHint(w,curProf.baseUrl,curProf.model,curProf.thinkingMode);
        for(HWND child=GetWindow(w,GW_CHILD);child;){HWND next=GetWindow(child,GW_HWNDNEXT);const int id=GetDlgCtrlID(child);if(child!=g->settingsViewport&&child!=g->settingsScroll&&id!=settingSave&&id!=settingCancel&&id!=settingErrorDetails&&id!=settingStatus&&id!=settingHeader&&id!=settingHelp&&id!=settingClose&&!(id>=settingTabGeneral&&id<=settingTabAdvanced))SetParent(child,g->settingsViewport);child=next;}
        setTheme();layoutSettings(w);return 0;}
    case WM_ERASEBKGND:{RECT r{};GetClientRect(w,&r);FillRect(reinterpret_cast<HDC>(wp),&r,g->backgroundBrush?g->backgroundBrush:GetSysColorBrush(COLOR_WINDOW));return 1;}
    case WM_DRAWITEM:{auto*item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(!item)break;
        if(item->CtlType==ODT_BUTTON&&isSettingsCheckbox(item->CtlID)){drawSettingsCheckbox(*item);return TRUE;}
        if(item->CtlType==ODT_BUTTON){drawActionButton(*item);return TRUE;}
        if(item->CtlType==ODT_BUTTON&&(item->CtlID==settingSave||item->CtlID==settingCancel||item->CtlID==settingTestButton||item->CtlID==settingAddProfile||item->CtlID==settingDeleteProfile)){drawActionButton(*item);return TRUE;}
        if(item->CtlType==ODT_COMBOBOX){drawThemeComboItem(*item);return TRUE;}
        break;}
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:return themedControlColor(m,wp,lp);
    case WM_SETTINGCHANGE:case WM_SYSCOLORCHANGE:case WM_THEMECHANGED:setTheme();return 0;
    case WM_NCCALCSIZE:return 0;
    case WM_NCPAINT:return 0;
    case WM_NCACTIVATE:return ui::activateClientFrame(w,wp);
    case WM_DWMCOMPOSITIONCHANGED:ui::useClientFrame(w);return 0;
    case WM_NCHITTEST:return uiHitTest(w,lp,54,124);
    case WM_PAINT:{PAINTSTRUCT paint{};HDC dc=BeginPaint(w,&paint);FillRect(dc,&paint.rcPaint,g->backgroundBrush);RECT r{};GetClientRect(w,&r);HPEN pen=CreatePen(PS_SOLID,1,g->borderColor);HGDIOBJ old=SelectObject(dc,pen);const auto frame=ui::settingsFrame(r.right,r.bottom,static_cast<int>(uiDpi(w)));MoveToEx(dc,px(w,24),frame.header.height-px(w,4),nullptr);LineTo(dc,r.right-px(w,24),frame.header.height-px(w,4));MoveToEx(dc,px(w,24),frame.footer.y,nullptr);LineTo(dc,r.right-px(w,24),frame.footer.y);SelectObject(dc,old);DeleteObject(pen);drawTitleMark(dc,w);EndPaint(w,&paint);return 0;}
    case WM_SIZE:layoutSettings(w);return 0;case WM_TIMER:if(wp==taskReaperTimerId){reapRetiredTasks(true);return 0;}break;
    case WM_DPICHANGED:{const RECT*r=reinterpret_cast<const RECT*>(lp);SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);refreshFonts(w);layoutSettings(w);return 0;}
    case WM_GETMINMAXINFO:uiWindowLimits(w,*reinterpret_cast<MINMAXINFO*>(lp),520,420);return 0;
    case WM_VSCROLL:{SCROLLINFO si{sizeof(si),SIF_ALL};GetScrollInfo(g->settingsScroll,SB_CTL,&si);int position=si.nPos;switch(LOWORD(wp)){case SB_LINEUP:position-=MulDiv(32,static_cast<int>(uiDpi(w)),96);break;case SB_LINEDOWN:position+=MulDiv(32,static_cast<int>(uiDpi(w)),96);break;case SB_PAGEUP:position-=static_cast<int>(si.nPage);break;case SB_PAGEDOWN:position+=static_cast<int>(si.nPage);break;case SB_THUMBTRACK:case SB_THUMBPOSITION:position=si.nTrackPos;break;case SB_TOP:position=0;break;case SB_BOTTOM:position=si.nMax-static_cast<int>(si.nPage)+1;break;default:return 0;}const int maxPos=std::max(0,si.nMax-static_cast<int>(si.nPage)+1);position=std::clamp(position,0,maxPos);g->settingsScrollDip=MulDiv(position,96,static_cast<int>(uiDpi(w)));layoutSettings(w);return 0;}
    case WM_MOUSEWHEEL:{g->settingsWheelRemainder+=GET_WHEEL_DELTA_WPARAM(wp);const int steps=g->settingsWheelRemainder/WHEEL_DELTA;g->settingsWheelRemainder%=WHEEL_DELTA;if(steps)scrollSettings(w,-steps*48);return 0;}
    case WM_COMMAND:{
        const int actionId=LOWORD(wp);
        if(actionId==settingSnipastePath&&HIWORD(wp)==EN_CHANGE){g->settingsPathTip=getText(uiItem(w,settingSnipastePath));if(g->tooltip){TOOLINFOW tool{sizeof(tool)};tool.hwnd=w;tool.uId=reinterpret_cast<UINT_PTR>(uiItem(w,settingSnipastePath));tool.lpszText=g->settingsPathTip.data();SendMessageW(g->tooltip,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&tool));}}
        if(HIWORD(wp)==EN_CHANGE&&(actionId==settingBaseUrl||actionId==settingModel)){
            const auto idx=g->currentSettingsProfileIndex;
            if(idx<g->settingsThinkingExplicit.size()&&!g->settingsThinkingExplicit[idx])setThinkingModeSelection(w,defaultThinkingMode(trimCopy(getText(uiItem(w,settingBaseUrl))),trimCopy(getText(uiItem(w,settingModel)))));
            updateThinkingModeHint(w,trimCopy(getText(uiItem(w,settingBaseUrl))),trimCopy(getText(uiItem(w,settingModel))),selectedThinkingMode(w));
        }
        if(actionId==settingHelp&&HIWORD(wp)==BN_CLICKED){openHelp();return 0;}
        if(actionId>=settingTabGeneral&&actionId<=settingTabAdvanced){if(HIWORD(wp)==BN_CLICKED){g->settingsTab=actionId-settingTabGeneral;g->settingsScrollDip=0;layoutSettings(w);}return 0;}
        if(actionId==settingClose&&HIWORD(wp)==BN_CLICKED){DestroyWindow(w);return 0;}
        if(actionId==settingBrowse&&HIWORD(wp)==BN_CLICKED){wchar_t path[32768]{};const auto current=getText(uiItem(w,settingSnipastePath));if(current.size()<std::size(path))wcscpy_s(path,current.c_str());OPENFILENAMEW file{sizeof(file)};file.hwndOwner=w;file.lpstrFilter=L"EXE 文件\0*.exe\0所有文件\0*.*\0";file.lpstrFile=path;file.nMaxFile=static_cast<DWORD>(std::size(path));file.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(GetOpenFileNameW(&file))uiSetText(w,settingSnipastePath,path);return 0;}
        if(actionId==settingTheme&&HIWORD(wp)==CBN_SELCHANGE){const auto selected=uiMessage(w,settingTheme,CB_GETCURSEL,0,0);g->previewTheme=selected==2?Theme::dark:selected==1?Theme::light:Theme::system;setTheme();return 0;}
        if(actionId==settingThinkingMode&&HIWORD(wp)==CBN_SELCHANGE){if(g->currentSettingsProfileIndex<g->settingsThinkingExplicit.size())g->settingsThinkingExplicit[g->currentSettingsProfileIndex]=true;updateThinkingModeHint(w,trimCopy(getText(uiItem(w,settingBaseUrl))),trimCopy(getText(uiItem(w,settingModel))),selectedThinkingMode(w));}
        if((HIWORD(wp)==EN_SETFOCUS||HIWORD(wp)==BN_SETFOCUS||HIWORD(wp)==CBN_SETFOCUS)&&lp&&GetParent(reinterpret_cast<HWND>(lp))==g->settingsViewport){RECT r{},v{};GetWindowRect(reinterpret_cast<HWND>(lp),&r);MapWindowPoints(nullptr,g->settingsViewport,reinterpret_cast<POINT*>(&r),2);GetClientRect(g->settingsViewport,&v);if(r.top<0||r.bottom>v.bottom){const int delta=r.top<0?r.top:r.bottom-v.bottom;g->settingsScrollDip=std::max(0,g->settingsScrollDip+MulDiv(delta,96,static_cast<int>(uiDpi(w))));layoutSettings(w);}}
        if((actionId==settingMoveUp||actionId==settingMoveDown||actionId==settingAddProfile||actionId==settingDeleteProfile||
            actionId==settingSave||actionId==settingCancel||actionId==settingTestButton)&&HIWORD(wp)!=BN_CLICKED)return 0;
        if(LOWORD(wp)==settingMoveUp||LOWORD(wp)==settingMoveDown){int i=static_cast<int>(uiMessage(w,settingToolbarOrder,CB_GETCURSEL,0,0));const int j=i+(LOWORD(wp)==settingMoveUp?-1:1);if(i>=0&&j>=0&&j<static_cast<int>(g->settingsToolbarOrder.size())){std::swap(g->settingsToolbarOrder[i],g->settingsToolbarOrder[j]);uiMessage(w,settingToolbarOrder,CB_RESETCONTENT,0,0);for(int id:g->settingsToolbarOrder){auto name=getText(uiItem(g->window,id));uiMessage(w,settingToolbarOrder,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));}uiMessage(w,settingToolbarOrder,CB_SETCURSEL,j,0);}return 0;}
        auto syncCurrentProfileFields=[&]{
            if(g->settingsWorkingProfiles.empty())return false;
            if(g->currentSettingsProfileIndex>=g->settingsWorkingProfiles.size())g->currentSettingsProfileIndex=0;
            auto& cur=g->settingsWorkingProfiles[g->currentSettingsProfileIndex];
            if(g->settingsThinkingExplicit.size()<g->settingsWorkingProfiles.size())g->settingsThinkingExplicit.resize(g->settingsWorkingProfiles.size(),true);
            cur.name=trimCopy(getText(uiItem(w,settingProfileName)));
            if(cur.name.empty())cur.name=cur.id;
            cur.baseUrl=trimCopy(getText(uiItem(w,settingBaseUrl)));
            cur.model=trimCopy(getText(uiItem(w,settingModel)));
            cur.thinkingMode=g->settingsThinkingExplicit[g->currentSettingsProfileIndex]?selectedThinkingMode(w):defaultThinkingMode(cur.baseUrl,cur.model);
            cur.thinkingModeExplicit=g->settingsThinkingExplicit[g->currentSettingsProfileIndex];
            setThinkingModeSelection(w,cur.thinkingMode);updateThinkingModeHint(w,cur.baseUrl,cur.model,cur.thinkingMode);
            std::wstring newKey=getText(uiItem(w,settingApiKey));
            cur.reuseCredential=cur.reuseCredential&&newKey==cur.apiKey;
            clearSecret(cur.apiKey);cur.apiKey=std::move(newKey);
            unsigned t=60,tok=2048;
            if(!parseUnsignedSetting(trimCopy(getText(uiItem(w,settingTimeout))),5,180,t)){uiSetText(w,settingStatus,L"超时必须为 5–180 的整数。");return false;}cur.timeoutSeconds=t;
            if(!parseUnsignedSetting(trimCopy(getText(uiItem(w,settingTokens))),1,32768,tok)){uiSetText(w,settingStatus,L"Token 必须为 1–32768 的整数。");return false;}cur.maxOutputTokens=tok;
            if(uiMessage(w,settingTransRole,BM_GETCHECK,0,0)==BST_CHECKED)g->settingsTranslationId=cur.id;
            if(uiMessage(w,settingRewriteRole,BM_GETCHECK,0,0)==BST_CHECKED)g->settingsRewriteId=cur.id;
            cur.enabled=uiMessage(w,settingEnabled,BM_GETCHECK,0,0)==BST_CHECKED;
            return true;
        };
        auto loadProfileFields=[&](size_t idx){
            if(idx>=g->settingsWorkingProfiles.size())return;
            g->currentSettingsProfileIndex=idx;
            const auto& cur=g->settingsWorkingProfiles[idx];
            uiSetText(w,settingProfileName,cur.name.c_str());
            uiSetText(w,settingBaseUrl,cur.baseUrl.c_str());
            uiSetText(w,settingModel,cur.model.c_str());
            uiSetText(w,settingApiKey,cur.apiKey.c_str());
            uiSetText(w,settingTimeout,std::to_wstring(cur.timeoutSeconds).c_str());
            uiSetText(w,settingTokens,std::to_wstring(cur.maxOutputTokens).c_str());
            setThinkingModeSelection(w,cur.thinkingMode);
            updateThinkingModeHint(w,cur.baseUrl,cur.model,cur.thinkingMode);
            SendMessageW(uiItem(w,settingTransRole),BM_SETCHECK,(cur.id==g->settingsTranslationId)?BST_CHECKED:BST_UNCHECKED,0);
            SendMessageW(uiItem(w,settingRewriteRole),BM_SETCHECK,(cur.id==g->settingsRewriteId)?BST_CHECKED:BST_UNCHECKED,0);
            SendMessageW(uiItem(w,settingEnabled),BM_SETCHECK,cur.enabled?BST_CHECKED:BST_UNCHECKED,0);
        };
        auto refreshProfileCombo=[&]{
            HWND combo=uiItem(w,settingProfileCombo);
            SendMessageW(combo,CB_RESETCONTENT,0,0);
            for(const auto& p:g->settingsWorkingProfiles){std::wstring item=p.name.empty()?p.id:p.name;SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item.c_str()));}
            SendMessageW(combo,CB_SETCURSEL,static_cast<WPARAM>(g->currentSettingsProfileIndex),0);
        };
        if(LOWORD(wp)==settingProfileCombo&&HIWORD(wp)==CBN_SELCHANGE){
            int newIdx=static_cast<int>(uiMessage(w,settingProfileCombo,CB_GETCURSEL,0,0));
            if(newIdx>=0&&static_cast<size_t>(newIdx)<g->settingsWorkingProfiles.size()){
                if(!syncCurrentProfileFields())return 0;
                loadProfileFields(static_cast<size_t>(newIdx));
            }
            return 0;
        }
        if(LOWORD(wp)==settingAddProfile){
            if(!syncCurrentProfileFields())return 0;
            ProviderProfile newProf;
            static unsigned seq=1;
            newProf.id=L"profile_"+std::to_wstring(GetTickCount64()&0xFFFF)+L"_"+std::to_wstring(++seq);
            newProf.name=L"新方案 "+std::to_wstring(g->settingsWorkingProfiles.size()+1);
            g->settingsWorkingProfiles.push_back(std::move(newProf));
            g->settingsThinkingExplicit.push_back(false);
            g->currentSettingsProfileIndex=g->settingsWorkingProfiles.size()-1;
            refreshProfileCombo();
            loadProfileFields(g->currentSettingsProfileIndex);
            return 0;
        }
        if(LOWORD(wp)==settingDeleteProfile){
            if(g->settingsWorkingProfiles.size()<=1){
                uiSetText(w,settingStatus,L"至少保留一个配置方案。");
                return 0;
            }
            if(!syncCurrentProfileFields())return 0;
            const auto delId=g->settingsWorkingProfiles[g->currentSettingsProfileIndex].id;
            clearSecret(g->settingsWorkingProfiles[g->currentSettingsProfileIndex].apiKey);
            g->settingsWorkingProfiles.erase(g->settingsWorkingProfiles.begin()+g->currentSettingsProfileIndex);
            if(g->currentSettingsProfileIndex<g->settingsThinkingExplicit.size())g->settingsThinkingExplicit.erase(g->settingsThinkingExplicit.begin()+g->currentSettingsProfileIndex);
            if(g->currentSettingsProfileIndex>=g->settingsWorkingProfiles.size())g->currentSettingsProfileIndex=g->settingsWorkingProfiles.size()-1;
            if(g->settingsTranslationId==delId)g->settingsTranslationId=g->settingsWorkingProfiles[g->currentSettingsProfileIndex].id;
            if(g->settingsRewriteId==delId)g->settingsRewriteId=g->settingsWorkingProfiles[g->currentSettingsProfileIndex].id;
            refreshProfileCombo();
            loadProfileFields(g->currentSettingsProfileIndex);
            return 0;
        }
        if(g->activeTestId&&((HIWORD(wp)==EN_CHANGE&&(LOWORD(wp)==settingBaseUrl||LOWORD(wp)==settingModel||LOWORD(wp)==settingApiKey||LOWORD(wp)==settingTimeout))||(HIWORD(wp)==CBN_SELCHANGE&&(LOWORD(wp)==settingTarget||LOWORD(wp)==settingThinkingMode)))){g->activeTestId=0;stopTestRequest();uiSetText(w,settingStatus,L"API 配置已更改，请重新测试连接。");return 0;}
        if(HIWORD(wp)==BN_CLICKED&&isSettingsCheckbox(LOWORD(wp))){HWND checkbox=uiItem(w,LOWORD(wp));SendMessageW(checkbox,BM_SETCHECK,SendMessageW(checkbox,BM_GETCHECK,0,0)==BST_CHECKED?BST_UNCHECKED:BST_CHECKED,0);return 0;}
        if(HIWORD(wp)==STN_CLICKED&&isSettingsCheckbox(LOWORD(wp)-1000)){HWND checkbox=uiItem(w,LOWORD(wp)-1000);SendMessageW(checkbox,BM_SETCHECK,SendMessageW(checkbox,BM_GETCHECK,0,0)==BST_CHECKED?BST_UNCHECKED:BST_CHECKED,0);return 0;}
        if(LOWORD(wp)==settingErrorDetails&&g->settingsErrorDetails.size()){showErrorDetails(w,g->settingsErrorDetails);return 0;}
        if(LOWORD(wp)==settingCancel){DestroyWindow(w);return 0;}
        if(LOWORD(wp)==settingTestButton){
        try{stopTestRequest();g->settingsErrorDetails.clear();ShowWindow(uiItem(w,settingErrorDetails),SW_HIDE);uiSetText(w,settingTestButton,L"测试连接");const int ti=static_cast<int>(uiMessage(w,settingTarget,CB_GETCURSEL,0,0));if(ti<0||ti>7)throw std::runtime_error("select a target language");const wchar_t*codes[]={L"zh-Hans",L"zh-Hant",L"en",L"ja",L"ko",L"fr",L"de",L"es"};
            TranslationRequest request;SecretWiper requestSecret{request.apiKey};request.baseUrl=trimCopy(getText(uiItem(w,settingBaseUrl)));request.model=trimCopy(getText(uiItem(w,settingModel)));request.apiKey=getText(uiItem(w,settingApiKey));request.thinkingMode=selectedThinkingMode(w);if(g->currentSettingsProfileIndex<g->settingsThinkingExplicit.size()&&!g->settingsThinkingExplicit[g->currentSettingsProfileIndex])request.thinkingMode=defaultThinkingMode(request.baseUrl,request.model);request.text=L"Hello. Return only this sentence translated.";request.targetLanguage=codes[ti];if(!parseUnsignedSetting(trimCopy(getText(uiItem(w,settingTimeout))),5,180,request.timeoutSeconds))throw std::runtime_error("invalid timeout");request.maxOutputTokens=256;request.maxNetworkRequests=1;request.timeoutSeconds=std::min(request.timeoutSeconds,30u);request.requestId=++g->requestSerial;request.revision=g->revision;request.testConnection=true;
            std::wstring endpointError;if(!validateEndpoint(request.baseUrl,endpointError))throw std::runtime_error("invalid endpoint");g->activeTestId=request.requestId;std::weak_ptr<UiEventQueue> weak=g->uiQueue;g->connectionTask=TranslationTask::start(std::move(request),[weak](TranslationEvent event){postTranslationEvent(weak,std::move(event));},[weak]{postTaskFinished(weak);});g->connectionTestActive=true;uiSetText(w,settingStatus,L"正在测试连接；此操作会消耗少量 API Token。");
        }catch(const std::exception&e){std::wstring message;try{message=json::decode(e.what());}catch(...){message=L"连接测试无法启动。";}uiSetText(w,settingStatus,message.c_str());}return 0;}
        if(LOWORD(wp)==settingSave){
        if(!syncCurrentProfileFields())return 0;
        Config candidate=g->config;ConfigSecretWiper candidateKeys{candidate};std::wstring error;try{const int ti=static_cast<int>(uiMessage(w,settingTarget,CB_GETCURSEL,0,0));const int themes=static_cast<int>(uiMessage(w,settingTheme,CB_GETCURSEL,0,0));if(ti<0||ti>7||themes<0||themes>2)throw std::runtime_error("invalid choice");const wchar_t*codes[]={L"zh-Hans",L"zh-Hant",L"en",L"ja",L"ko",L"fr",L"de",L"es"};candidate.targetLanguage=codes[ti];candidate.theme=themes==2?Theme::dark:themes==1?Theme::light:Theme::system;
            for(auto& profile:candidate.profiles)clearSecret(profile.apiKey);
            candidate.profiles=g->settingsWorkingProfiles;
            candidate.translationProfileId=g->settingsTranslationId;
            candidate.rewriteProfileId=g->settingsRewriteId;
            candidate.toolbarOrder=g->settingsToolbarOrder;candidate.rememberPrompts=uiMessage(w,settingRememberPrompts,BM_GETCHECK,0,0)==BST_CHECKED;candidate.confirmOcrCandidates=uiMessage(w,settingOcrConfirmation,BM_GETCHECK,0,0)==BST_CHECKED;
            if(!parseUnsignedSetting(trimCopy(getText(uiItem(w,settingVersionLimit))),1,50,candidate.versionLimit)){error=L"版本上限必须为 1–50。";throw std::runtime_error("invalid history limit");}
            if(!candidate.profiles.empty()){
                auto* transProf=candidate.findProfile(candidate.translationProfileId);
                if(!transProf)transProf=&candidate.profiles.front();
                candidate.baseUrl=transProf->baseUrl;candidate.model=transProf->model;clearSecret(candidate.apiKey);candidate.apiKey=transProf->apiKey;candidate.credentialRef=transProf->credentialRef;candidate.timeoutSeconds=transProf->timeoutSeconds;candidate.maxOutputTokens=transProf->maxOutputTokens;candidate.reuseCredential=transProf->reuseCredential;
            }
            if(candidate.baseUrl.size()>2048||candidate.model.size()>256||candidate.apiKey.size()>4096||candidate.timeoutSeconds<5||candidate.timeoutSeconds>180||candidate.maxOutputTokens<1||candidate.maxOutputTokens>32768)throw std::runtime_error("settings out of range");if(!candidate.baseUrl.empty()&&!validateEndpoint(candidate.baseUrl,error))throw std::runtime_error("invalid Base URL");
            candidate.alwaysOnTop=uiMessage(w,settingTop,BM_GETCHECK,0,0)==BST_CHECKED;candidate.rememberPosition=uiMessage(w,settingRemember,BM_GETCHECK,0,0)==BST_CHECKED;candidate.autoStart=uiMessage(w,settingAutoTranslate,BM_GETCHECK,0,0)==BST_CHECKED;candidate.showSourceCopy=uiMessage(w,settingSourceCopy,BM_GETCHECK,0,0)==BST_CHECKED;candidate.showUnlock=uiMessage(w,settingUnlock,BM_GETCHECK,0,0)==BST_CHECKED;candidate.showRestore=uiMessage(w,settingRestore,BM_GETCHECK,0,0)==BST_CHECKED;candidate.showResultCopy=uiMessage(w,settingResultCopy,BM_GETCHECK,0,0)==BST_CHECKED;candidate.showRetranslate=uiMessage(w,settingRetranslate,BM_GETCHECK,0,0)==BST_CHECKED;
            candidate.showRewrite=uiMessage(w,settingRewrite,BM_GETCHECK,0,0)==BST_CHECKED;candidate.showDeAi=uiMessage(w,settingDeAi,BM_GETCHECK,0,0)==BST_CHECKED;candidate.showCustomPrompt=uiMessage(w,settingCustomPrompt,BM_GETCHECK,0,0)==BST_CHECKED;candidate.showVersionHistory=uiMessage(w,settingVersionHistory,BM_GETCHECK,0,0)==BST_CHECKED;
            candidate.snipasteExePath=trimCopy(getText(uiItem(w,settingSnipastePath)));
            if(!candidate.apiKey.empty()&&candidate.baseUrl.empty())throw std::runtime_error("configure Base URL before storing an API key");if(!saveConfig(candidate,error))throw std::runtime_error("save failed");const bool targetChanged=g->language!=candidate.targetLanguage;
            if(candidate.snipasteExePath!=g->config.snipasteExePath&&g->ocrAdapter&&g->ocrAdapter->isCapturing())cancelOcrCapture();
            cancelOperation();clearConfigSecrets(g->config);g->config=std::move(candidate);g->language=g->config.targetLanguage;
            if(targetChanged){++g->revision;g->activeRequestId=0;g->activeSourceId=0;g->requestActive=false;stopMainRequest();invalidateCompletedAssociation();g->result.clear();if(g->window)setText(uiItem(g->window,resultEdit),L"");}
            if(!g->config.rememberPrompts){g->recentPrompts.clear();if(g->customPromptWnd){uiMessage(g->customPromptWnd,promptHistoryCombo,CB_RESETCONTENT,0,0);}}
            while(g->versions.size()>g->config.versionLimit){auto it=g->versions.begin();if(it->versionId==g->currentVersionId)++it;g->versions.erase(it);}
            if(g->window){uiMessage(g->window,targetCombo,CB_SETCURSEL,ti,0);SetWindowPos(g->window,g->config.alwaysOnTop?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);setTheme();updateButtons();layoutMain();if(!g->source.empty()&&!g->config.baseUrl.empty()&&!g->config.model.empty())status(L"设置已保存。新文本将按自动翻译设置处理。");}
            if(!g->config.warning.empty()){SetWindowTextW(uiItem(w,settingCancel),L"关闭");uiSetText(w,settingStatus,g->config.warning.c_str());return 0;}DestroyWindow(w);return 0;
        }catch(...){const std::wstring message=error.empty()?L"设置无效，请检查字段和范围。":error;uiSetText(w,settingStatus,message.c_str());return 0;}}
        break;
    }
    case WM_CLOSE:DestroyWindow(w);return 0;case WM_DESTROY:if(g->help)DestroyWindow(g->help);g->activeTestId=0;stopTestRequest();if(g->taskReaperWindow==w){KillTimer(w,taskReaperTimerId);g->taskReaperWindow=nullptr;}for(auto& profile:g->settingsWorkingProfiles)clearSecret(profile.apiKey);g->settingsWorkingProfiles.clear();g->settingsThinkingExplicit.clear();g->settingsViewport=nullptr;g->settingsScroll=nullptr;g->settings=nullptr;g->previewTheme.reset();setTheme();scheduleTaskReaper();if(g->window)SetForegroundWindow(g->window);else PostQuitMessage(0);return 0;}
    return DefWindowProcW(w,m,wp,lp);
    }catch(...){if(g&&g->settings&&IsWindow(g->settings))uiSetText(g->settings,settingStatus,L"操作失败，请检查配置并重试。");return 0;}
}
void openSettings(){
    if(g->settings){if(!testMode()||showTestUi()){ShowWindow(g->settings,SW_SHOW);SetForegroundWindow(g->settings);}return;}
    const DWORD style=WS_POPUP|WS_SYSMENU|WS_THICKFRAME|WS_CLIPCHILDREN;const HMONITOR monitor=MonitorFromWindow(g->window,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{sizeof(mi)};if(!GetMonitorInfoW(monitor,&mi)){status(L"无法读取设置窗所在显示器的工作区。");return;}const UINT bootstrapDpi=GetDpiForSystem();RECT bootstrap{0,0,MulDiv(850,static_cast<int>(bootstrapDpi),96),MulDiv(650,static_cast<int>(bootstrapDpi),96)};
    HWND h=CreateWindowExW(WS_EX_CONTROLPARENT,L"SAT.Settings",L"设置 · Translator",style,mi.rcWork.left,mi.rcWork.top,bootstrap.right-bootstrap.left,bootstrap.bottom-bootstrap.top,g->window,nullptr,g->instance,nullptr);if(!h){status(L"设置窗口无法创建。");return;}const UINT dpi=uiDpi(h);RECT outer{0,0,MulDiv(850,static_cast<int>(dpi),96),MulDiv(650,static_cast<int>(dpi),96)};const int workWidth=static_cast<int>(mi.rcWork.right-mi.rcWork.left),workHeight=static_cast<int>(mi.rcWork.bottom-mi.rcWork.top),width=std::min(static_cast<int>(outer.right-outer.left),workWidth),height=std::min(static_cast<int>(outer.bottom-outer.top),workHeight);const int x=static_cast<int>(mi.rcWork.left)+(workWidth-width)/2,y=static_cast<int>(mi.rcWork.top)+(workHeight-height)/2;if(!SetWindowPos(h,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE)){DestroyWindow(h);status(L"设置窗口无法定位。");return;}
    SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(fontFor(h)),TRUE);ShowWindow(h,testMode()&&!showTestUi()?SW_HIDE:SW_SHOW);UpdateWindow(h);if(!testMode()||showTestUi())SetForegroundWindow(h);
}
#if defined(SAT_ENABLE_TEST_HOOKS)
void pumpPostedUiMessages(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)throw std::runtime_error("unexpected quit during UI event test");TranslateMessage(&message);DispatchMessageW(&message);}}
#include "ui_visual_tests.hpp"
void testUiReworkInteractions(){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    openSettings();HWND w=g->settings;const Theme theme=g->config.theme;const bool ocrConfirmation=g->config.confirmOcrCandidates;const auto revision=g->revision;const auto profileCount=g->config.profiles.size();
    const std::wstring draft=L"草稿 / English / 🌏";uiSetText(w,settingProfileName,draft.c_str());
    ShowWindow(w,SW_SHOWNOACTIVATE);
    for(int cycle=0;cycle<50;++cycle){
        SendMessageW(w,WM_COMMAND,MAKEWPARAM(settingTabGeneral+cycle%5,BN_CLICKED),0);
        RECT view{};GetClientRect(g->settingsViewport,&view);const auto page=ui::settingsPage(g->settingsTab,view.right,view.bottom,static_cast<int>(uiDpi(w)),g->settingsScrollDip);
        std::vector<RECT> actual;
        for(const auto& placement:page.items){RECT rect{};require(GetWindowRect(uiItem(w,placement.id),&rect)!=FALSE,"active settings control disappeared");MapWindowPoints(nullptr,g->settingsViewport,reinterpret_cast<POINT*>(&rect),2);require(rect.left>=0&&rect.right<=view.right,"active field exceeds viewport width");for(const auto& other:actual){RECT overlap{};require(!IntersectRect(&overlap,&rect,&other),"real HWND rectangles overlap");}actual.push_back(rect);}
        require(getText(uiItem(w,settingProfileName))==draft,"tab switch discarded Provider draft");
    }
    SendMessageW(uiItem(w,settingOcrConfirmation),BM_SETCHECK,ocrConfirmation?BST_UNCHECKED:BST_CHECKED,0);
    uiMessage(w,settingTarget,CB_SHOWDROPDOWN,TRUE,0);require(closeAnyDroppedCombo()&&g->settings==w&&uiMessage(w,settingTarget,CB_GETDROPPEDSTATE,0,0)==FALSE,"Escape did not dismiss combo independently of settings");require(nativePageNavigation(uiItem(w,settingTimeout))&&nativePageNavigation(uiItem(w,settingProfileCombo)),"native form key navigation intercepted");
    uiMessage(w,settingTheme,CB_SETCURSEL,2,0);SendMessageW(w,WM_COMMAND,MAKEWPARAM(settingTheme,CBN_SELCHANGE),reinterpret_cast<LPARAM>(uiItem(w,settingTheme)));require(g->config.theme==theme&&g->backgroundColor==RGB(14,15,18),"theme preview committed config or failed to refresh");
    const auto profiles=g->settingsWorkingProfiles.size();openHelp();ShowWindow(g->help,SW_SHOWNOACTIVATE);HWND helpTextControl=uiItem(g->help,settingIntro),helpScrollControl=uiItem(g->help,helpScroll);require(getText(helpTextControl).find(L"按“自动翻译”设置处理")!=std::wstring::npos&&getText(helpTextControl).find(L"目标语言后的“翻译”")!=std::wstring::npos,"help omitted the configured OCR policy and manual Translate action");require(helpScrollControl&&(GetWindowLongPtrW(helpTextControl,GWL_STYLE)&WS_VSCROLL)==0,"help retained an unthemed RichEdit scrollbar");SendMessageW(helpTextControl,EM_LINESCROLL,0,100);syncHelpScroll(g->help);SCROLLINFO helpScrollInfo{sizeof(helpScrollInfo),SIF_POS};GetScrollInfo(helpScrollControl,SB_CTL,&helpScrollInfo);require(helpScrollInfo.nPos>0,"help scrolling did not synchronize the themed scrollbar");scrollHelpTo(g->help,0);require(SendMessageW(helpTextControl,EM_GETFIRSTVISIBLELINE,0,0)==0,"help scrollbar did not return to the first line");SendMessageW(g->help,WM_KEYDOWN,VK_ESCAPE,0);require(!g->help&&GetFocus()==uiItem(w,settingHelp),"help close did not restore question-mark focus");
    require(g->revision==revision&&!g->connectionTestActive&&g->settingsWorkingProfiles.size()==profiles&&getText(uiItem(w,settingProfileName))==draft,"help changed business state or draft");
    DestroyWindow(w);require(g->config.theme==theme&&g->config.confirmOcrCandidates==ocrConfirmation&&g->config.profiles.size()==profileCount&&!g->previewTheme,"cancel polluted committed config or OCR confirmation preference");
    // Warm common control caches once, then measure repeated settings/help lifetimes.
    openSettings();openHelp();DestroyWindow(g->help);DestroyWindow(g->settings);
    const DWORD handles=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS),gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);DWORD kernelHandles=0;GetProcessHandleCount(GetCurrentProcess(),&kernelHandles);const auto tools=SendMessageW(g->tooltip,TTM_GETTOOLCOUNT,0,0);
    for(int i=0;i<20;++i){openSettings();openHelp();DestroyWindow(g->help);DestroyWindow(g->settings);}
    DWORD afterHandles=0;GetProcessHandleCount(GetCurrentProcess(),&afterHandles);const DWORD afterUser=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS),afterGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    require(afterUser<=handles+1&&afterGdi<=gdi+1&&afterHandles<=kernelHandles+2,"settings/help cycles leaked resources");require(SendMessageW(g->tooltip,TTM_GETTOOLCOUNT,0,0)==tools,"settings tooltip registrations leaked");
    std::ofstream evidence(g->config.folder/L"ui-rework-checks.json",std::ios::binary);evidence<<"{\"actual_resize_cycles\":100,\"actual_scroll_cycles\":50,\"actual_tab_cycles\":50,\"settings_help_cycles\":20,\"user_objects_before\":"<<handles<<",\"user_objects_after\":"<<afterUser<<",\"gdi_before\":"<<gdi<<",\"gdi_after\":"<<afterGdi<<",\"handles_before\":"<<kernelHandles<<",\"handles_after\":"<<afterHandles<<",\"focus_restore\":true,\"tooltip_count_stable\":true,\"human_acceptance\":\"UNVERIFIED\"}";
}
void testSettingsRelayout(){
    auto require=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    openSettings();HWND w=g->settings,view=g->settingsViewport;require(w&&view,"settings viewport missing");
    const DWORD gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int i=0;i<100;++i){
        g->settingsTab=i%5;SetWindowPos(w,nullptr,0,0,px(w,620+i%5*55),px(w,420+i%7*30),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        RECT a{},b{},v{},f{};GetWindowRect(uiItem(w,settingBaseUrl),&a);layoutSettings(w);GetWindowRect(uiItem(w,settingBaseUrl),&b);
        require(EqualRect(&a,&b)!=FALSE,"settings layout is not idempotent");GetWindowRect(view,&v);GetWindowRect(uiItem(w,settingSave),&f);require(v.bottom<=f.top,"settings content overlaps footer");
    }
    for(int i=0;i<50;++i){SendMessageW(w,WM_VSCROLL,MAKEWPARAM(i%2?SB_TOP:SB_BOTTOM,0),0);SCROLLINFO si{sizeof(si),SIF_ALL};GetScrollInfo(g->settingsScroll,SB_CTL,&si);require(si.nPos>=0&&si.nPos<=std::max(0,si.nMax-static_cast<int>(si.nPage)+1),"settings scroll outside range");}
    require(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdiBefore+2,"settings relayout leaked GDI objects");DestroyWindow(w);
}
void testInputSessions(){
    auto cancelledBeforeAccept=std::make_shared<InputCancelMarker>();cancelledBeforeAccept->cancelPending();if(cancelledBeforeAccept->tryAccept()||!cancelledBeforeAccept->isCancelled())throw std::runtime_error("pending cancellation did not win before acceptance");
    auto acceptedBeforeCancel=std::make_shared<InputCancelMarker>();if(!acceptedBeforeCancel->tryAccept())throw std::runtime_error("pending input could not be accepted");acceptedBeforeCancel->cancelPending();if(acceptedBeforeCancel->isCancelled())throw std::runtime_error("late pending cancellation revoked an accepted input");
    const auto receivedAt=InputClock::now();Incoming initial;initial.source=InputSource::mockText;initial.text=L"Session 初始文本";applyIncoming(std::move(initial));
    if(g->source!=L"Session 初始文本"||g->inputSession.id==0||g->inputSession.source!=InputSource::mockText||g->inputSession.textRevision!=g->revision||g->inputSession.submittedAt<receivedAt||g->inputSession.cancelled()||g->inputSession.cancelMarker->state.load(std::memory_order_acquire)!=InputCancelMarker::State::accepted)throw std::runtime_error("initial input did not enter an accepted timestamped source session");
    const uint64_t acceptedId=g->inputSession.id;auto acceptedMarker=g->inputSession.cancelMarker;const auto acceptedTime=g->inputSession.submittedAt;
    Incoming cancelled;cancelled.source=InputSource::textFile;cancelled.text=L"must not replace";cancelled.cancelMarker=std::make_shared<InputCancelMarker>();cancelled.cancelMarker->cancelPending();applyIncoming(std::move(cancelled));
    if(g->source!=L"Session 初始文本"||g->inputSession.id!=acceptedId||acceptedMarker->isCancelled())throw std::runtime_error("cancelled input replaced a live source session");
    g->editing=true;Incoming queued;queued.source=InputSource::clipboardOnce;queued.text=L"cancel queued";if(!queueIncoming(std::move(queued))||g->pending.empty())throw std::runtime_error("input adapter did not enqueue a pending source session");
    if(!g->pending.back().confirmReplacement)throw std::runtime_error("input received during editing would silently replace a submitted draft");g->editing=false;
    auto queuedMarker=g->pending.back().cancelMarker;if(!queuedMarker||g->pending.back().sourceId<=acceptedId||g->pending.back().submittedAt==InputClock::time_point{})throw std::runtime_error("pending input metadata was incomplete");queuedMarker->cancelPending();SendMessageW(g->window,msgProcessPending,0,0);
    if(!g->pending.empty()||g->source!=L"Session 初始文本"||g->inputSession.id!=acceptedId)throw std::runtime_error("cancelled pending input was applied");
    Incoming replacement;replacement.source=InputSource::textFile;replacement.text=L"Session 替换文本";applyIncoming(std::move(replacement));
    if(!acceptedMarker->isCancelled()||g->inputSession.id<=acceptedId||g->inputSession.submittedAt<acceptedTime||g->inputSession.textRevision!=g->revision||g->inputSession.cancelled()||g->source!=L"Session 替换文本")throw std::runtime_error("source replacement did not cancel and supersede the previous session");
    const auto replacedMarker=g->inputSession.cancelMarker;acceptEditedSource(L"Manual edit session");updatePanes();const uint64_t manualEditId=g->inputSession.id;const auto manualEditMarker=g->inputSession.cancelMarker;
    if(!replacedMarker->isCancelled()||manualEditId<=acceptedId||g->inputSession.source!=InputSource::manualEdit||g->inputSession.textRevision!=g->revision||g->inputSession.cancelled()||manualEditMarker->state.load(std::memory_order_acquire)!=InputCancelMarker::State::accepted)throw std::runtime_error("manual edit did not establish a fresh accepted input session");
    SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(unlockButton,BN_CLICKED),0);SetWindowTextW(uiItem(g->window,sourceEdit),L"unsubmitted draft");SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(restoreButton,BN_CLICKED),0);
    if(g->inputSession.id!=manualEditId||manualEditMarker->isCancelled()||!g->editing)throw std::runtime_error("restoring text during edit changed the submitted source session");
    SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(cancelEditButton,BN_CLICKED),0);if(g->source!=L"Manual edit session"||g->inputSession.id!=manualEditId||g->inputSession.cancelled())throw std::runtime_error("cancelling a draft did not restore its matching source session");
}
void testOcrButtonNotifications(){
    g->ocrAdapter=std::make_unique<MockOcrAdapter>(OcrSessionStatus::captured,L"must not arrive",MAXDWORD);
    SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(captureButton,BN_SETFOCUS),0);
    SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(captureButton,BN_KILLFOCUS),0);
    if(g->ocrAdapter->isCapturing())throw std::runtime_error("capture focus notification started OCR");
    SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(captureButton,BN_CLICKED),0);
    const auto session=g->ocrAdapter->sessionId();
    if(!g->ocrAdapter->isCapturing())throw std::runtime_error("first OCR click was cancelled");
    SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(captureButton,BN_SETFOCUS),0);
    SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(captureButton,BN_CLICKED),0);
    if(!g->ocrAdapter->isCapturing()||session!=g->ocrAdapter->sessionId())throw std::runtime_error("duplicate OCR request cancelled or restarted session");
    const auto source=g->source;
    SendMessageW(g->window,WM_KEYDOWN,VK_ESCAPE,0);
    SendMessageW(g->window,WM_TIMER,ocrPollTimerId,0);
    if(!IsWindow(g->window)||g->ocrAdapter->isCapturing()||g->source!=source)throw std::runtime_error("OCR Esc/late timer changed source or destroyed window");
    SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(captureButton,BN_CLICKED),0);
    if(!g->ocrAdapter->isCapturing()||g->ocrAdapter->sessionId()<=session)throw std::runtime_error("OCR did not restart after Esc");
    cancelOcrCapture();g->ocrAdapter.reset();
}
void testNextStageOcrState(){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const bool originalVisibility=IsWindowVisible(g->window)!=FALSE;ShowWindow(g->window,SW_SHOWNOACTIVATE);
    class CandidateAdapter final:public IOcrAdapter {
    public:
        bool active=false,current=true,retryNext=false;uint64_t id=0;unsigned starts=0,polls=0;
        OcrAdapterKind kind()const noexcept override{return OcrAdapterKind::forwardSnipaste;}
        bool startCapture(DWORD,std::wstring&)override{active=true;++id;++starts;return true;}
        void cancelCapture()override{active=false;++id;}
        OcrResult poll()override{++polls;if(retryNext){retryNext=false;return {OcrSessionStatus::waiting,{},L"test clipboard busy",0,true};}return active?OcrResult{OcrSessionStatus::candidate,std::wstring(159,L'A')+L"🌏 tail",{},42}:OcrResult{};}
        bool isCapturing()const noexcept override{return active;}
        uint64_t sessionId()const noexcept override{return id;}
        bool isCandidateCurrent(DWORD sequence)const override{return current&&sequence==42;}
    };
    Config savedConfig=g->config;g->config.autoStart=false;g->config.confirmOcrCandidates=true;
    applyIncoming({false,false,false,InputSource::mockText,L"current source"});
    auto adapter=std::make_unique<CandidateAdapter>();auto* candidate=adapter.get();g->ocrAdapter=std::move(adapter);
    startOcrCapture();const auto firstGeneration=g->ocrGeneration;
    require(g->captureSessionActive&&g->clipboardListenerRegistered&&!IsWindowVisible(g->window),"capture did not hide the translator and register its temporary clipboard listener");
    SendMessageW(g->window,WM_CLIPBOARDUPDATE,0,0);
    RECT candidatePanelRect{};GetWindowRect(g->ocrConfirmWnd,&candidatePanelRect);
    require(g->ocrConfirmWnd&&IsWindowVisible(g->ocrConfirmWnd)&&GetParent(g->ocrConfirmWnd)==nullptr&&!IsWindowVisible(g->window)&&g->ocrConfirming&&candidatePanelRect.right-candidatePanelRect.left>=400&&candidatePanelRect.bottom-candidatePanelRect.top>=120&&getText(uiItem(g->ocrConfirmWnd,ocrCandidatePreview))==std::wstring(159,L'A')+L"…","candidate confirmation did not remain separate from the hidden translation window or split a surrogate pair");
    const auto starts=candidate->starts,polls=candidate->polls;startOcrCapture();pollOcrSession();
    require(candidate->polls==polls&&candidate->starts==starts,"open candidate surface reentered or restarted capture");
    SendMessageW(g->ocrConfirmWnd,WM_COMMAND,MAKEWPARAM(ocrCandidateIgnore,BN_CLICKED),0);
    require(candidate->isCapturing()&&g->ocrGeneration==firstGeneration&&g->source==L"current source"&&!g->ocrConfirmWnd,"ignoring candidate discarded capture or changed source");
    pollOcrSession();
    require(g->ocrConfirmWnd&&candidate->isCapturing(),"continuous capture did not show a later candidate");
    candidate->current=false;SendMessageW(g->ocrConfirmWnd,WM_COMMAND,MAKEWPARAM(ocrCandidateUse,BN_CLICKED),0);
    require(candidate->isCapturing()&&g->source==L"current source"&&!g->ocrConfirmWnd,"competing clipboard update accepted a stale candidate");
    candidate->current=true;pollOcrSession();require(g->ocrConfirmWnd,"fresh candidate was not offered after a competing clipboard update");
    SendMessageW(g->ocrConfirmWnd,WM_KEYDOWN,VK_ESCAPE,0);
    require(g->source==L"current source"&&!candidate->isCapturing()&&!g->ocrConfirmWnd,"Escape did not cancel the active OCR candidate session");
    startOcrCapture();pollOcrSession();require(g->ocrConfirmWnd,"candidate confirmation surface was not recreated for a new session");
    SendMessageW(g->ocrConfirmWnd,WM_COMMAND,MAKEWPARAM(ocrCandidateUse,BN_CLICKED),0);
    require(g->source==std::wstring(159,L'A')+L"🌏 tail"&&!candidate->isCapturing()&&g->inputSession.source==InputSource::clipboardOcrCandidate&&IsWindowVisible(g->window)&&!g->clipboardListenerRegistered,"confirmed candidate did not commit via the unified input path and reveal only after confirmation");
    applyIncoming({false,false,false,InputSource::mockText,L"privacy guard source"});
    if(g->config.profiles.empty())g->config.syncProfilesFromActive();
    require(!g->config.profiles.empty(),"test could not create a local-only translation profile");
    g->config.profiles.front().baseUrl=L"http://127.0.0.1:1/v1";g->config.profiles.front().model=L"local-privacy-guard-test";clearSecret(g->config.profiles.front().apiKey);g->config.profiles.front().apiKey=L"local-test-only";g->config.profiles.front().enabled=true;g->config.translationProfileId=g->config.profiles.front().id;g->config.autoStart=true;g->config.confirmOcrCandidates=false;
    g->ocrAdapter=std::make_unique<CandidateAdapter>();candidate=static_cast<CandidateAdapter*>(g->ocrAdapter.get());startOcrCapture();pollOcrSession();
    require(!g->ocrConfirmWnd&&g->source==std::wstring(159,L'A')+L"🌏 tail"&&g->inputSession.source==InputSource::clipboardOcrCandidate&&g->requestActive&&g->activeSourceId==g->inputSession.id&&IsWindowVisible(g->window),"confirmation-off auto-translate did not reveal the source and start its matching request");
    cancelOperation();g->config.autoStart=false;g->ocrAdapter=std::make_unique<CandidateAdapter>();startOcrCapture();pollOcrSession();
    require(!g->requestActive&&IsWindowVisible(g->window)&&IsWindowEnabled(uiItem(g->window,mainTranslateButton))&&g->status.find(L"点击“翻译”")!=std::wstring::npos,"manual OCR mode did not reveal a usable translation action");
    clearConfigSecrets(g->config);g->config=std::move(savedConfig);
    g->ocrAdapter=std::make_unique<MockOcrAdapter>(OcrSessionStatus::captured,L"late OCR",100);
    startOcrCapture();applyIncoming({false,false,false,InputSource::mockText,L"manual replacement"});pollOcrSession();
    require(g->source==L"manual replacement"&&!g->ocrAdapter->isCapturing(),"manual replacement left old capture active");
    startOcrCapture();SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(unlockButton,BN_CLICKED),0);
    require(!g->ocrAdapter->isCapturing()&&g->editing,"unlock did not invalidate capture");
    startOcrCapture();require(!g->ocrAdapter->isCapturing(),"capture started while source editing");
    SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(cancelEditButton,BN_CLICKED),0);
    g->config.confirmOcrCandidates=true;ShowWindow(g->window,SW_SHOWNOACTIVATE);g->ocrAdapter=std::make_unique<CandidateAdapter>();candidate=static_cast<CandidateAdapter*>(g->ocrAdapter.get());candidate->retryNext=true;startOcrCapture();SendMessageW(g->window,WM_CLIPBOARDUPDATE,0,0);
    require(g->clipboardRetryPending&&!g->ocrConfirmWnd,"busy clipboard read was not marked for a bounded retry");SendMessageW(g->window,WM_TIMER,ocrPollTimerId,0);
    require(g->ocrConfirmWnd&&!g->clipboardRetryPending&&!IsWindowVisible(g->window),"clipboard contention retry did not deliver a candidate while keeping the translator hidden");SendMessageW(g->ocrConfirmWnd,WM_KEYDOWN,VK_ESCAPE,0);
    require(!candidate->isCapturing()&&IsWindowVisible(g->window)&&!g->clipboardListenerRegistered,"Escape from candidate confirmation did not release the capture session");g->config.confirmOcrCandidates=savedConfig.confirmOcrCandidates;
    ShowWindow(g->window,SW_SHOWNOACTIVATE);g->ocrAdapter=std::make_unique<CandidateAdapter>();candidate=static_cast<CandidateAdapter*>(g->ocrAdapter.get());startOcrCapture();g->ocrDeadline=GetTickCount64()-1;SendMessageW(g->window,WM_TIMER,ocrPollTimerId,0);
    require(!candidate->isCapturing()&&IsWindowVisible(g->window)&&!g->clipboardListenerRegistered&&g->status.find(L"超时")!=std::wstring::npos,"capture timeout did not release the listener and restore a previously visible window");
    g->ocrAdapter.reset();
    if(!originalVisibility)ShowWindow(g->window,SW_HIDE);
}
void testRichEditPlaceholderState(){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};HWND edit=uiItem(g->window,sourceEdit),other=uiItem(g->window,targetCombo);const bool wasVisible=IsWindowVisible(g->window)!=FALSE,wasEditing=g->editing;const LONG_PTR oldStyle=GetWindowLongPtrW(edit,GWL_STYLE);const std::wstring oldSource=getText(edit),oldModel=g->source,oldResult=g->result;const auto oldFocus=GetFocus();
    ShowWindow(g->window,SW_SHOWNOACTIVATE);g->editing=true;SendMessageW(edit,EM_SETREADONLY,FALSE,0);SetWindowTextW(edit,L"");SetFocus(other);require(richPaneHintVisible(edit),"empty RichEdit did not expose its placeholder when unfocused");
    SetFocus(edit);require(!richPaneHintVisible(edit),"focused empty RichEdit placeholder covered the caret");SendMessageW(edit,WM_IME_STARTCOMPOSITION,0,0);require(imeComposing(edit)&&!richPaneHintVisible(edit),"IME composition was covered by the empty-state hint");SendMessageW(edit,WM_IME_ENDCOMPOSITION,0,0);require(!imeComposing(edit),"RichEdit IME composition state did not clear");
    SetWindowTextW(edit,L"Manual 中文");require(!richPaneHintVisible(edit),"typed RichEdit text retained its placeholder");SendMessageW(edit,EM_SETSEL,1,7);DWORD start=0,end=0;SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));require(start==1&&end==7&&!richPaneHintVisible(edit),"placeholder synchronization changed a real selection");
    SetWindowTextW(edit,L"");SendMessageW(edit,EM_SETSEL,0,0);const wchar_t pasted[]=L"粘贴 🌏";SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(pasted));require(getText(edit)==pasted&&!richPaneHintVisible(edit),"RichEdit paste insertion did not hide the placeholder");
    SendMessageW(edit,EM_UNDO,0,0);require(getText(edit).empty(),"RichEdit undo did not restore the empty state");SetFocus(other);require(richPaneHintVisible(edit),"placeholder did not return after undo removed the last character");SendMessageW(edit,EM_REDO,0,0);require(getText(edit)==pasted&&!richPaneHintVisible(edit),"RichEdit redo did not hide the placeholder");
    SetFocus(edit);SendMessageW(edit,EM_SETSEL,0,static_cast<WPARAM>(-1));SendMessageW(edit,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(L""));require(getText(edit).empty(),"RichEdit delete did not empty the source");SetFocus(other);require(richPaneHintVisible(edit),"placeholder did not return after deleting all source text");
    g->source=L"OCR fill state";updatePanes();require(!richPaneHintVisible(edit),"programmatic OCR-style pane fill left the empty placeholder visible");g->source.clear();updatePanes();SetFocus(other);require(richPaneHintVisible(edit),"programmatic source restore to empty did not restore the placeholder");
    g->source=oldModel;g->result=oldResult;g->editing=wasEditing;updatePanes();SetWindowTextW(edit,oldSource.c_str());SendMessageW(edit,EM_SETREADONLY,(oldStyle&ES_READONLY)!=0,0);if(oldFocus&&IsWindow(oldFocus))SetFocus(oldFocus);if(!wasVisible)ShowWindow(g->window,SW_HIDE);InvalidateRect(edit,nullptr,FALSE);
}
void testNextStageInteraction(){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto revision=g->revision;
    for(int id:{unlockButton,translateButton,mainTranslateButton,requestCancelButton,customPromptButton,rewriteButton,deAiButton,versionButton,moreButton,settingsButton}){
        SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(id,BN_SETFOCUS),0);
        SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(id,BN_KILLFOCUS),0);
    }
    require(!g->editing&&!g->settings&&!childVisible(g->customPromptWnd)&&g->revision==revision,"main button focus performed action");
    for(int id:g->config.toolbarOrder){wchar_t text[512]{};TOOLINFOW info{sizeof(info)};info.hwnd=g->window;info.uId=reinterpret_cast<UINT_PTR>(uiItem(g->window,id));info.lpszText=text;require(SendMessageW(g->tooltip,TTM_GETTOOLINFOW,0,reinterpret_cast<LPARAM>(&info))!=0&&*text,"toolbar action missing tooltip");}
    showCustomPromptPanel();
    auto prompt=g->customPromptWnd;setText(uiItem(prompt,promptTextEdit),L"synthetic instruction");
    SendMessageW(prompt,WM_COMMAND,MAKEWPARAM(promptScopeSource,BN_CLICKED),0);require(SendMessageW(uiItem(prompt,promptScopeSource),BM_GETCHECK,0,0)==BST_CHECKED&&SendMessageW(uiItem(prompt,promptScopeBoth),BM_GETCHECK,0,0)==BST_UNCHECKED,"prompt scope click did not preserve exclusive selection");
    require((SendMessageW(uiItem(prompt,promptScopeSource),WM_GETDLGCODE,0,0)&DLGC_WANTARROWS)!=0,"dialog navigation intercepted prompt scope arrows");
    SendMessageW(uiItem(prompt,promptScopeSource),WM_KEYDOWN,VK_RIGHT,0);require(SendMessageW(uiItem(prompt,promptScopeBoth),BM_GETCHECK,0,0)==BST_CHECKED,"prompt scope arrow navigation did not wrap to the first option");
    require(GetWindow(g->window,GW_CHILD)==prompt,"custom prompt is behind the original/result panes");
    require((GetWindowLongPtrW(prompt,GWL_STYLE)&WS_CLIPCHILDREN)!=0&&(GetWindowLongPtrW(uiItem(g->window,sourceEdit),GWL_STYLE)&WS_CLIPSIBLINGS)!=0&&(GetWindowLongPtrW(uiItem(g->window,resultEdit),GWL_STYLE)&WS_CLIPSIBLINGS)!=0,"prompt and RichEdit repaint clipping is missing");
    for(int id:{promptExecuteBtn,promptCancelBtn}){HWND action=uiItem(prompt,id);SendMessageW(action,BM_SETSTYLE,BS_DEFPUSHBUTTON,TRUE);require((GetWindowLongPtrW(action,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW,"prompt default/focus updates broke button drawing");}
    const auto historyCount=g->recentPrompts.size();
    SendMessageW(prompt,WM_COMMAND,MAKEWPARAM(promptExecuteBtn,BN_SETFOCUS),0);
    SendMessageW(prompt,WM_COMMAND,MAKEWPARAM(promptCancelBtn,BN_KILLFOCUS),0);
    require(childVisible(prompt)&&g->recentPrompts.size()==historyCount&&g->revision==revision,"prompt focus executed or dismissed panel");
    HWND edit=uiItem(prompt,promptTextEdit);
    SendMessageW(edit,WM_IME_STARTCOMPOSITION,0,0);
    require(imeComposing(edit)&&deferImeKey(WM_KEYDOWN,VK_RETURN,imeComposing(edit)),"prompt IME composition not protected");
    SendMessageW(edit,WM_KEYDOWN,VK_ESCAPE,0);
    require(childVisible(prompt),"IME Escape dismissed prompt panel");
    SendMessageW(edit,WM_IME_ENDCOMPOSITION,0,0);
    require(!imeComposing(edit),"prompt IME state stuck after end");
    SendMessageW(prompt,WM_COMMAND,MAKEWPARAM(promptCancelBtn,BN_CLICKED),0);
    SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(unlockButton,BN_CLICKED),0);
    edit=uiItem(g->window,sourceEdit);SendMessageW(edit,WM_IME_STARTCOMPOSITION,0,0);
    require(imeComposing(edit),"source IME composition not tracked");
    SendMessageW(edit,WM_IME_ENDCOMPOSITION,0,0);require(!imeComposing(edit),"source IME state stuck");
    SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(cancelEditButton,BN_CLICKED),0);
    openSettings();const auto count=g->settingsWorkingProfiles.size();const auto order=g->settingsToolbarOrder;
    for(int id:{settingSave,settingCancel,settingTestButton,settingAddProfile,settingDeleteProfile,settingMoveUp,settingMoveDown}){
        SendMessageW(g->settings,WM_COMMAND,MAKEWPARAM(id,BN_SETFOCUS),0);
        SendMessageW(g->settings,WM_COMMAND,MAKEWPARAM(id,BN_KILLFOCUS),0);
    }
    require(g->settings&&IsWindow(g->settings)&&g->settingsWorkingProfiles.size()==count&&g->settingsToolbarOrder==order&&!g->connectionTestActive,"settings button focus saved/tested/modified configuration");
    DestroyWindow(g->settings);
}
void testTopTranslationSubmission(){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};Config saved=g->config;
    g->config.autoStart=false;g->config.baseUrl.clear();g->config.model.clear();for(auto& profile:g->config.profiles){profile.baseUrl.clear();profile.model.clear();}
    applyIncoming({false,false,false,InputSource::mockText,L""});setText(uiItem(g->window,sourceEdit),L"fresh typed source");
    require(IsWindowEnabled(uiItem(g->window,mainTranslateButton)),"typing in the empty source did not enable Translate");
    SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(mainTranslateButton,BN_CLICKED),0);
    require(g->source==L"fresh typed source"&&g->rawSource==g->source&&g->inputSession.source==InputSource::manualEdit&&!g->requestActive,"Translate ignored a fresh source draft or bypassed missing configuration");
    const auto session=g->inputSession.id;SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(unlockButton,BN_CLICKED),0);setText(uiItem(g->window,sourceEdit),L"edited source");
    SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(mainTranslateButton,BN_CLICKED),0);
    require(!g->editing&&g->source==L"edited source"&&g->inputSession.id>session&&g->rawSource==L"fresh typed source","top Translate did not commit the edited input session");
    SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(unlockButton,BN_CLICKED),0);setText(uiItem(g->window,sourceEdit),L"");
    require(!IsWindowEnabled(uiItem(g->window,mainTranslateButton)),"empty source did not disable Translate");SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(mainTranslateButton,BN_CLICKED),0);
    require(g->editing&&g->source==L"edited source"&&!g->requestActive,"empty draft discarded the committed source or started a request");
    SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(cancelEditButton,BN_CLICKED),0);clearConfigSecrets(g->config);g->config=std::move(saved);
}
void testUiEventQueue(){
    std::weak_ptr<UiEventQueue> queue=g->uiQueue;HWND result=uiItem(g->window,resultEdit);
    if(!SendMessageW(g->window,WM_GETICON,ICON_SMALL,0)||!SendMessageW(g->window,WM_GETICON,ICON_BIG,0)||!GetClassLongPtrW(g->window,GCLP_HICONSM))throw std::runtime_error("application icon was not assigned to the live window and Alt+Tab class");
    g->activeRequestId=++g->requestSerial;++g->revision;const uint64_t currentId=g->activeRequestId,currentRevision=g->revision;g->requestActive=true;g->result=L"current:";setText(result,g->result);status(L"当前请求进行中。");updateButtons();
    g->activeSourceId=g->inputSession.id;postTranslationEvent(queue,{TranslationEvent::Kind::delta,currentId,currentRevision,false,L"译文",g->activeSourceId});pumpPostedUiMessages();
    if(g->result!=L"current:译文"||getText(result)!=g->result||!g->requestActive)throw std::runtime_error("current asynchronous UI event was not applied");
    postTranslationEvent(queue,{TranslationEvent::Kind::delta,currentId,currentRevision,false,L"过期来源",g->activeSourceId+1});pumpPostedUiMessages();if(g->result!=L"current:译文"||!g->requestActive)throw std::runtime_error("translation event from another input session changed the result");
    std::mutex gateMutex;std::condition_variable gate;bool released=false;std::exception_ptr producerFailure;
    std::thread oldRequest([&]{std::unique_lock lock(gateMutex);gate.wait(lock,[&]{return released;});lock.unlock();try{
        postTranslationEvent(queue,{TranslationEvent::Kind::delta,currentId-1,currentRevision-1,false,L"过期译文",g->activeSourceId});
        postTranslationEvent(queue,{TranslationEvent::Kind::complete,currentId-1,currentRevision-1,false,{},g->activeSourceId});
        postTranslationEvent(queue,{TranslationEvent::Kind::error,currentId-1,currentRevision-1,false,L"过期错误",g->activeSourceId});
    }catch(...){producerFailure=std::current_exception();}});
    {std::lock_guard lock(gateMutex);released=true;}gate.notify_one();oldRequest.join();if(producerFailure)std::rethrow_exception(producerFailure);pumpPostedUiMessages();
    if(g->result!=L"current:译文"||getText(result)!=g->result||!g->requestActive||g->status!=L"当前请求进行中。")throw std::runtime_error("late request events changed the current UI state");
    postTranslationEvent(queue,{TranslationEvent::Kind::complete,currentId,currentRevision,false,{},g->activeSourceId});pumpPostedUiMessages();
    if(g->requestActive||g->status!=L"翻译完成。")throw std::runtime_error("current terminal UI event was not applied");

    const auto completed=g->lastCompletedResult;const auto versionCount=g->versions.size(),versionSerial=g->versionSerial;
    g->requestActive=true;g->activeRequestId=++g->requestSerial;++g->revision;const uint64_t failureId=g->activeRequestId,failureRevision=g->revision;g->result=completed+L"部分输出";setText(result,g->result);g->activeSourceId=g->inputSession.id;
    TranslationEvent failure{TranslationEvent::Kind::error,failureId,failureRevision,false,translationErrorMessage(TranslationErrorKind::outputTruncated),g->activeSourceId};failure.errorKind=TranslationErrorKind::outputTruncated;failure.errorCode=L"finish_reason=length";failure.maxOutputTokens=64;failure.thinkingMode=ThinkingMode::enabled;failure.phase=TranslationPhase::body;failure.responseIsStream=true;
    postTranslationEvent(queue,std::move(failure));pumpPostedUiMessages();
    if(g->requestActive||g->result!=completed||getText(result)!=completed||g->versions.size()!=versionCount||g->versionSerial!=versionSerial)throw std::runtime_error("truncated output replaced the last complete result or entered version history");
    if(!g->retryAvailable||!childVisible(uiItem(g->window,retryButton))||!childVisible(uiItem(g->window,errorDetailsButton))||g->lastErrorDetails.find(L"finish_reason=length")==std::wstring::npos||g->lastErrorDetails.find(L"输出 Token 上限：64")==std::wstring::npos||g->lastErrorDetails.find(L"思考模式：开启")==std::wstring::npos||g->lastErrorDetails.find(L"SSE")==std::wstring::npos)throw std::runtime_error("truncation recovery actions or safe model/token details were missing");
    if(g->lastErrorDetails.find(L"部分输出")!=std::wstring::npos||g->lastErrorDetails.find(L"Authorization")!=std::wstring::npos)throw std::runtime_error("error details exposed partial text or authorization metadata");
    g->retryAvailable=false;g->lastErrorDetails.clear();updateButtons();status(L"测试状态已恢复。");
    openSettings();if(!g->settings)throw std::runtime_error("settings window could not be created for stale connection test");
    const uint64_t staleTestId=++g->requestSerial;g->activeTestId=staleTestId;HWND oldSettings=g->settings;DestroyWindow(oldSettings);if(g->settings)throw std::runtime_error("settings window did not close during stale connection test");
    openSettings();if(!g->settings)throw std::runtime_error("settings window could not be reopened for stale connection test");
    g->activeTestId=++g->requestSerial;uiSetText(g->settings,settingStatus,L"新连接测试仍在进行。");
    postTranslationEvent(queue,{TranslationEvent::Kind::complete,staleTestId,g->revision,true,{}});pumpPostedUiMessages();
    wchar_t statusText[128]{};uiGetText(g->settings,settingStatus,statusText,static_cast<int>(std::size(statusText)));
    if(std::wstring(statusText)!=L"新连接测试仍在进行。")throw std::runtime_error("late connection-test event changed the reopened settings window");
    DestroyWindow(g->settings);if(g->settings)throw std::runtime_error("settings window did not close after UI event test");
    g->activeTestId=0;g->activeRequestId=0;g->requestActive=false;g->result.clear();setText(result,L"");updateButtons();
}
void testDiagnosticReport(){
    Config sample;sample.folder=L"diagnostic-fixture";sample.model=L"mock-model";sample.baseUrl=L"https://api.example.com/v1";sample.apiKey=L"diagnostic-private-key";
    struct SecretGuard{Config& config;~SecretGuard(){clearSecret(config.apiKey);}}secretGuard{sample};
    bool hasError=true;const std::wstring report=formatDiagnosticReport(sample,true,hasError);
    if(hasError||report.find(L"不发送网络请求")==std::wstring::npos||report.find(L"API 地址：格式有效（未连接验证）")==std::wstring::npos||report.find(L"diagnostic-private-key")!=std::wstring::npos)throw std::runtime_error("diagnostic report validation failed or exposed a credential");
    sample.damagedConfig=true;sample.warning=L"配置损坏测试";const std::wstring damaged=formatDiagnosticReport(sample,true,hasError);
    if(!hasError||damaged.find(sample.warning)==std::wstring::npos||damaged.find(L"diagnostic-private-key")!=std::wstring::npos)throw std::runtime_error("diagnostic damage state or credential redaction failed");
}
void testP1ReviewState(){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    std::weak_ptr<UiEventQueue> queue=g->uiQueue;
    g->versions.clear();g->config.autoStart=false;
    applyIncoming({false,false,false,InputSource::mockText,L"原文 A"});
    auto complete=[&](const std::wstring& text){
        g->activeRequestId=++g->requestSerial;g->activeSourceId=g->inputSession.id;g->requestActive=true;
        g->result=text;setText(uiItem(g->window,resultEdit),text);
        TranslationEvent event{TranslationEvent::Kind::complete,g->activeRequestId,g->revision,false,{},g->activeSourceId};
        postTranslationEvent(queue,event);pumpPostedUiMessages();
        postTranslationEvent(queue,event);pumpPostedUiMessages();
    };
    complete(L"译文 A");
    require(g->versions.size()==1,"duplicate terminal event added duplicate version");
    const auto first=g->versions.front().versionId;
    g->requestActive=true;g->activeRequestId=++g->requestSerial;const auto oldId=g->activeRequestId,oldRevision=g->revision;
    SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(unlockButton,BN_CLICKED),0);
    require(!g->requestActive,"editing did not cancel active operation");
    postTranslationEvent(queue,{TranslationEvent::Kind::delta,oldId,oldRevision,false,L"迟到",g->inputSession.id});pumpPostedUiMessages();
    require(g->result==L"译文 A","late delta reached editing session");
    SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(cancelEditButton,BN_CLICKED),0);
    applyIncoming({false,false,false,InputSource::mockText,L"原文 B"});g->language=L"en";
    g->ocrAdapter=std::make_unique<MockOcrAdapter>(OcrSessionStatus::captured,L"stale after restore",100);
    startOcrCapture();
    restoreVersion(first);
    require(!g->ocrAdapter->isCapturing(),"version restore left stale OCR capture active");g->ocrAdapter.reset();
    require(g->source==L"原文 A"&&g->result==L"译文 A"&&g->language==L"zh-Hans","version restore lost source/language association");
    require(g->lastCompletedResult==g->result&&!g->requestActive,"restore lost completed recovery snapshot");
    for(int i=0;i<15;++i)complete(L"版本 "+std::to_wstring(i));
    require(g->versions.size()==10&&g->versions.back().versionId==g->currentVersionId,"real version capacity/current pointer failed");
    applyIncoming({false,false,false,InputSource::mockText,L"新的原文"});
    require(g->lastCompletedResult.empty()&&g->currentVersionId==0,"new source retained old recovery snapshot");
    Incoming queuedA{false,false,false,InputSource::mockText,L"queued A"},queuedB{false,false,false,InputSource::mockText,L"queued B"};prepareIncoming(queuedA);prepareIncoming(queuedB);const auto firstSourceId=queuedA.sourceId,secondSourceId=queuedB.sourceId;
    applyIncoming(std::move(queuedA));require(g->sourceRevision==firstSourceId,"queued source revision used later queued identifier");applyIncoming(std::move(queuedB));require(g->sourceRevision==secondSourceId&&firstSourceId!=secondSourceId,"queued sources shared a revision");
    for(int i=0;i<20;++i){g->activeRequestId=++g->requestSerial;g->activeSourceId=g->inputSession.id;g->requestActive=true;const auto id=g->activeRequestId,rev=g->revision;
        uiMessage(g->window,targetCombo,CB_SETCURSEL,i%2?0:2,0);SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(targetCombo,CBN_SELCHANGE),0);
        postTranslationEvent(queue,{TranslationEvent::Kind::delta,id,rev,false,L"stale rewrite",g->inputSession.id});postTranslationEvent(queue,{TranslationEvent::Kind::complete,id,rev,false,{},g->inputSession.id});pumpPostedUiMessages();
        require(g->result.empty()&&g->versions.size()==10&&!g->requestActive,"rapid language/cancel interleaving admitted stale completion");}
    RECT outer{};GetWindowRect(g->window,&outer);SetWindowPos(g->window,nullptr,0,0,420,460,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    require(childVisible(uiItem(g->window,settingsButton))&&(g->overflowActions.empty()||childVisible(uiItem(g->window,moreButton))),"narrow single-row toolbar lost overflow/settings access");
    RECT actualClient{};GetClientRect(g->window,&actualClient);std::vector<RECT> buttons;
    for(int id:g->config.toolbarOrder){HWND h=uiItem(g->window,id);if(!childVisible(h))continue;RECT r{};GetWindowRect(h,&r);MapWindowPoints(nullptr,g->window,reinterpret_cast<POINT*>(&r),2);require(r.left>=0&&r.right<=actualClient.right&&r.bottom<=actualClient.bottom,"toolbar escaped narrow client");for(const auto& other:buttons){RECT hit{};require(!IntersectRect(&hit,&r,&other),"single-row toolbar buttons overlap");}buttons.push_back(r);}
    SetWindowPos(g->window,nullptr,0,0,outer.right-outer.left,outer.bottom-outer.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    showCustomPromptPanel();RECT panel{},client{};GetWindowRect(g->customPromptWnd,&panel);GetClientRect(g->customPromptWnd,&client);
    require(client.right>=360&&client.bottom>=150,"first prompt open was not laid out");
    SendMessageW(g->customPromptWnd,WM_COMMAND,MAKEWPARAM(promptCancelBtn,BN_CLICKED),0);
    g->versions.clear();g->result.clear();updatePanes();updateButtons();layoutMain();
}
#endif
void beginTranslate(){
    if(g->editing){status(L"请先提交或取消原文编辑。");return;}if(trimCopy(g->source).empty()){status(L"原文为空，尚未发送请求。");return;}if(!validText(g->source)){status(L"原文含不受支持的控制字符，未发送请求。");return;}if(g->inputSession.id==0||g->inputSession.cancelled()){status(L"原文输入会话已失效，请重新粘贴或输入。");return;}
    const auto* profile=g->config.getProfileForRole(L"translate");
    if(!profile||!profile->enabled||profile->baseUrl.empty()||profile->model.empty()||profile->apiKey.empty()){status(L"请配置并启用绑定的翻译模型和 API Key。");return;}
    std::wstring baseUrl=profile->baseUrl,model=profile->model,apiKey=profile->apiKey;SecretWiper localKey{apiKey};
    unsigned timeout=profile->timeoutSeconds,tokens=profile->maxOutputTokens;const auto thinkingMode=profile->thinkingMode;std::wstring profileId=profile->id;
    std::wstring endpointError;if(!validateEndpoint(baseUrl,endpointError)){status(endpointError);return;}
    g->retryTargetLanguage=g->language;cancelOperation();g->activeRequestId=++g->requestSerial;g->activeSourceId=g->inputSession.id;g->activeOpType=AiOperationType::translate;g->activeRewriteStyle=RewriteStyle::natural;g->activeCustomScope=CustomPromptScope::sourceAndResult;g->activeCustomPrompt.clear();g->retryAvailable=false;g->lastErrorDetails.clear();g->result.clear();updatePanes();
    TranslationRequest request;SecretWiper requestSecret{request.apiKey};
    request.opType=AiOperationType::translate;request.modelProfileId=std::move(profileId);
    request.baseUrl=std::move(baseUrl);request.model=std::move(model);request.apiKey=std::move(apiKey);
    request.text=g->source;request.targetLanguage=g->language;request.timeoutSeconds=timeout;request.maxOutputTokens=tokens;request.thinkingMode=thinkingMode;
    request.requestId=g->activeRequestId;request.revision=g->revision;request.sourceSessionId=g->activeSourceId;
    std::weak_ptr<UiEventQueue> weak=g->uiQueue;try{g->task=TranslationTask::start(std::move(request),[weak](TranslationEvent event){postTranslationEvent(weak,std::move(event));},[weak]{postTaskFinished(weak);});g->requestActive=true;updateButtons();status(L"正在连接模型…");}
    catch(const std::exception&){g->requestActive=false;g->activeRequestId=0;recoverCompleted(true);status(L"无法启动翻译请求，请检查模型与 API 配置。");updateButtons();layoutMain();}
}
void retryLastOperation(){
    if(!g||!g->retryAvailable||g->requestActive)return;
    const auto type=g->activeOpType;const auto style=g->activeRewriteStyle;const auto scope=g->activeCustomScope;const std::wstring prompt=g->activeCustomPrompt;
    if(type==AiOperationType::translate){if(!g->retryTargetLanguage.empty())selectTargetLanguage(g->retryTargetLanguage);beginTranslate();if(!g->requestActive){recoverCompleted(true);updateButtons();layoutMain();}}
    else beginAiOperation(type,style,prompt,scope);
}

enum class InputKind{empty,clipboard,textFile,mockText,settings,diagnose,snipasteOcr,mockOcr,mockOcrCandidate};struct Args{InputKind kind=InputKind::empty;std::wstring value;};
std::wstring readInputFile(const std::wstring& path){if(path.empty()||path.size()>32760)throw std::runtime_error("invalid text file path");HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("text file cannot be opened");LARGE_INTEGER n{};if(!GetFileSizeEx(h,&n)||n.QuadPart<0||static_cast<uint64_t>(n.QuadPart)>maxTextBytes){CloseHandle(h);throw std::runtime_error("text file exceeds 1 MiB limit");}std::string b(static_cast<size_t>(n.QuadPart),'\0');DWORD got=0;if(!b.empty()&&(!ReadFile(h,b.data(),static_cast<DWORD>(b.size()),&got,nullptr)||got!=b.size())){CloseHandle(h);throw std::runtime_error("text file read failed");}CloseHandle(h);if(b.rfind("\xef\xbb\xbf",0)==0)b.erase(0,3);auto w=json::decode(b);if(!validText(w))throw std::runtime_error("text file contains invalid control characters");return w;}
std::wstring readClipboardOnce(){if(!OpenClipboard(nullptr))throw std::runtime_error("clipboard is busy");struct Close{~Close(){CloseClipboard();}}close;if(!IsClipboardFormatAvailable(CF_UNICODETEXT))throw std::runtime_error("clipboard has no Unicode text");HANDLE h=GetClipboardData(CF_UNICODETEXT);if(!h)throw std::runtime_error("clipboard text is unavailable");const SIZE_T n=GlobalSize(h);if(n<sizeof(wchar_t)||n>maxTextBytes||n%sizeof(wchar_t))throw std::runtime_error("clipboard text size is invalid");const wchar_t*p=static_cast<const wchar_t*>(GlobalLock(h));if(!p)throw std::runtime_error("clipboard text cannot be locked");struct Unlock{HANDLE h;~Unlock(){GlobalUnlock(h);}}unlock{h};const wchar_t*end=std::find(p,p+n/sizeof(wchar_t),L'\0');if(end==p+n/sizeof(wchar_t))throw std::runtime_error("clipboard text is not terminated");return {p,end};}
Args parseArgs(int argc,wchar_t**argv){if(argc==1)return {};if(argc==2&&std::wstring(argv[1])==L"--settings")return {InputKind::settings,{}};if(argc==2&&std::wstring(argv[1])==L"--diagnose")return {InputKind::diagnose,{}};
#if defined(SAT_ENABLE_TEST_HOOKS)
    if(testMode()&&argc==2&&std::wstring(argv[1])==L"--self-test-ui")return {};
#endif
    if(argc==2&&std::wstring(argv[1])==L"--from-snipaste-ocr")throw std::runtime_error("HOLD: Snipaste OCR-to-exec provenance is unverified. Use manual paste or --snip with candidate confirmation.");
    if(argc==2&&std::wstring(argv[1])==L"--snip")return {InputKind::snipasteOcr,{}};
#if defined(SAT_ENABLE_TEST_HOOKS)
    if(testMode()&&argc==3&&std::wstring(argv[1])==L"--mock-ocr"){std::wstring s=argv[2];if(!validText(s)||s.empty())throw std::runtime_error("mock ocr text is empty or invalid");return {InputKind::mockOcr,std::move(s)};}
    if(testMode()&&argc==3&&std::wstring(argv[1])==L"--mock-ocr-candidate"){std::wstring s=argv[2];if(!validText(s)||s.empty())throw std::runtime_error("mock OCR candidate text is empty or invalid");return {InputKind::mockOcrCandidate,std::move(s)};}
    if(testMode()&&argc==3&&std::wstring(argv[1])==L"--mock-text"){std::wstring s=argv[2];if(!validText(s)||s.empty())throw std::runtime_error("mock text is empty or invalid");return {InputKind::mockText,std::move(s)};}
#endif
    if(argc==2&&std::wstring(argv[1])==L"--clipboard-text"){auto s=readClipboardOnce();if(s.empty())throw std::runtime_error("clipboard has no usable text");if(!validText(s))throw std::runtime_error("clipboard contains invalid control characters");return {InputKind::clipboard,std::move(s)};}
    if(argc==3&&std::wstring(argv[1])==L"--text-file"){auto s=readInputFile(argv[2]);if(s.empty())throw std::runtime_error("text file is empty");return {InputKind::textFile,std::move(s)};}
    if(argc>=2&&std::wstring(argv[1])==L"--image")throw std::runtime_error("本轮未实现图片 OCR。请使用粘贴文本入口。");
    throw std::runtime_error("参数无效。支持：无参数、--clipboard-text、--snip、--from-snipaste-ocr、--text-file <path>、--settings、--diagnose。");}
std::wstring formatDiagnosticReport(const Config& config,bool configExists,bool& hasError){
    hasError=config.damagedConfig||!config.warning.empty();
    std::wstring report=L"诊断报告（不发送网络请求；首次使用时可能创建配置目录）\r\n\r\n配置目录："+config.folder.wstring()+L"\r\n配置文件：";
    if(!configExists&&config.warning.empty())report+=L"尚未保存（首次使用）";
    else if(config.damagedConfig)report+=L"无法读取或凭据无法解密；原文件已保留";
    else if(!config.warning.empty())report+=L"不可读；已使用安全默认值，原文件未修改";
    else report+=L"可读取";
    report+=L"\r\n模型："+(config.model.empty()?std::wstring(L"未配置"):std::wstring(L"已配置"));
    report+=L"\r\nAPI Key："+(config.apiKey.empty()?std::wstring(L"未配置"):std::wstring(L"已通过当前 Windows 用户 DPAPI 保存"));
    if(config.baseUrl.empty())report+=L"\r\nAPI 地址：未配置";
    else{std::wstring endpointError;if(validateEndpoint(config.baseUrl,endpointError))report+=L"\r\nAPI 地址：格式有效（未连接验证）";else{report+=L"\r\nAPI 地址："+endpointError;hasError=true;}}
    if(!config.warning.empty()&&config.damagedConfig)report+=L"\r\n\r\n"+config.warning;
    return report;
}
int runDiagnostics(){
    Config config=loadConfig(false);
    ConfigSecretWiper secretGuard{config};
    const auto configPath=config.folder/L"config.json";
    const bool configExists=GetFileAttributesW(configPath.c_str())!=INVALID_FILE_ATTRIBUTES;
    bool hasError=false;const std::wstring report=formatDiagnosticReport(config,configExists,hasError);clearConfigSecrets(config);
    MessageBoxW(nullptr,report.c_str(),L"OCR AI Translator · 诊断",MB_OK|(hasError?MB_ICONWARNING:MB_ICONINFORMATION));
    return hasError?2:0;
}
bool forwardInstance(const Args&a){
    struct ScopedHandle{HANDLE value{};explicit ScopedHandle(HANDLE h=nullptr):value(h){}~ScopedHandle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}ScopedHandle(const ScopedHandle&)=delete;ScopedHandle&operator=(const ScopedHandle&)=delete;};
    const auto mutexName=instanceName(false),readyName=instanceName(true);ScopedHandle mutex(OpenMutexW(SYNCHRONIZE,FALSE,mutexName.c_str()));if(!mutex.value)return false;
    ScopedHandle ready(OpenEventW(SYNCHRONIZE,FALSE,readyName.c_str()));if(!ready.value||WaitForSingleObject(ready.value,2000)!=WAIT_OBJECT_0)throw std::runtime_error("existing Translator did not become ready");HWND target=FindWindowW(mainWindowClass(),nullptr);if(!target)throw std::runtime_error("existing Translator window is unavailable");
    std::wstring payload=a.kind==InputKind::settings?L"SAT/1:settings\n":a.kind==InputKind::empty?L"SAT/1:show\n":a.kind==InputKind::snipasteOcr?L"SAT/1:snip\n":L"SAT/1:text\n"+a.value;
#if defined(SAT_ENABLE_TEST_HOOKS)
    if(a.kind==InputKind::mockOcr)payload=L"SAT/1:mock_ocr\n"+a.value;
#endif
    if(payload.size()>maxTextBytes/sizeof(wchar_t)+64||(a.kind!=InputKind::empty&&a.kind!=InputKind::settings&&a.kind!=InputKind::snipasteOcr&&!validText(a.value)))throw std::runtime_error("IPC payload invalid");payload.push_back(L'\0');COPYDATASTRUCT cds{copyDataTag,static_cast<DWORD>(payload.size()*sizeof(wchar_t)),payload.data()};DWORD_PTR response=0;
    if(!SendMessageTimeoutW(target,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&cds),SMTO_ABORTIFHUNG|SMTO_BLOCK,1500,&response)||!response)throw std::runtime_error("existing Translator did not accept the input");if(a.kind!=InputKind::snipasteOcr&&(!testMode()||showTestUi())){ShowWindow(target,SW_RESTORE);SetForegroundWindow(target);}return true;
}
void registerWindowClass(HINSTANCE inst,const wchar_t* name, WNDPROC proc){WNDCLASSEXW c{sizeof(c)};c.lpfnWndProc=proc;c.hInstance=inst;c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hbrBackground=GetSysColorBrush(COLOR_WINDOW);c.hIcon=loadAppIcon(GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON));c.hIconSm=loadAppIcon(GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON));c.lpszClassName=name;if(!RegisterClassExW(&c)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("window class registration failed");}
HWND createMain(HINSTANCE inst){
    Config&c=g->config;POINT cursor{};GetCursorPos(&cursor);const HMONITOR monitor=MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{sizeof(mi)};if(!GetMonitorInfoW(monitor,&mi))throw std::runtime_error("display work area unavailable");const DWORD style=WS_POPUP|WS_THICKFRAME|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_CLIPCHILDREN;const UINT bootstrapDpi=GetDpiForSystem();RECT bootstrap{0,0,MulDiv(c.widthDip,static_cast<int>(bootstrapDpi),96),MulDiv(c.heightDip,static_cast<int>(bootstrapDpi),96)};
    HWND hWnd=CreateWindowExW(0,mainWindowClass(),mainWindowTitle(),style,mi.rcWork.left,mi.rcWork.top,bootstrap.right-bootstrap.left,bootstrap.bottom-bootstrap.top,nullptr,nullptr,inst,nullptr);if(!hWnd)throw std::runtime_error("main window creation failed");if(!SetWindowTextW(hWnd,mainWindowTitle())){DestroyWindow(hWnd);throw std::runtime_error("main window title could not be set");}const UINT dpi=uiDpi(hWnd);RECT outer{0,0,MulDiv(c.widthDip,static_cast<int>(dpi),96),MulDiv(c.heightDip,static_cast<int>(dpi),96)};const int workWidth=static_cast<int>(mi.rcWork.right-mi.rcWork.left),workHeight=static_cast<int>(mi.rcWork.bottom-mi.rcWork.top);const int width=std::min(static_cast<int>(outer.right-outer.left),workWidth),height=std::min(static_cast<int>(outer.bottom-outer.top),workHeight);int x=static_cast<int>(mi.rcWork.left)+(workWidth-width)/2,y=static_cast<int>(mi.rcWork.top)+(workHeight-height)/2;if(c.rememberPosition){x=static_cast<int>(mi.rcWork.left)+MulDiv(c.xDip,static_cast<int>(dpi),96);y=static_cast<int>(mi.rcWork.top)+MulDiv(c.yDip,static_cast<int>(dpi),96);}x=std::clamp(x,static_cast<int>(mi.rcWork.left),static_cast<int>(mi.rcWork.right)-width);y=std::clamp(y,static_cast<int>(mi.rcWork.top),static_cast<int>(mi.rcWork.bottom)-height);if(!SetWindowPos(hWnd,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE)){DestroyWindow(hWnd);throw std::runtime_error("main window placement failed");}
    g->windowPlaced=true;rememberNormalGeometry(hWnd);
    if(g->startHiddenForCapture)SetWindowPos(hWnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_HIDEWINDOW);
    else ShowWindow(hWnd,testMode()&&!showTestUi()?SW_HIDE:SW_SHOWNORMAL);
    UpdateWindow(hWnd);if(c.alwaysOnTop)SetWindowPos(hWnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);traceCapture("main_created",g->startHiddenForCapture?"hidden_capture":"normal_start");return hWnd;
}
#include "stability_ui_tests.hpp"
int runGui(HINSTANCE inst,const Args&a,bool smoke=false,bool stability=false){
    App app;app.instance=inst;app.config=loadConfig();app.language=app.config.targetLanguage;app.startHiddenForCapture=a.kind==InputKind::snipasteOcr;app.startupSnipPending=app.startHiddenForCapture;g=&app;
    std::optional<Incoming> startupInput;if(a.kind==InputKind::textFile||a.kind==InputKind::mockText||a.kind==InputKind::clipboard){Incoming incoming;incoming.source=a.kind==InputKind::clipboard?InputSource::clipboardOnce:a.kind==InputKind::textFile?InputSource::textFile:InputSource::mockText;incoming.text=a.value;prepareIncoming(incoming);startupInput=std::move(incoming);}
    (void)smoke;
    const auto mutexName=instanceName(false),readyName=instanceName(true);app.ready=CreateEventW(nullptr,TRUE,FALSE,readyName.c_str());if(!app.ready)throw std::runtime_error("single-instance ready event creation failed");app.mutex=CreateMutexW(nullptr,TRUE,mutexName.c_str());if(!app.mutex)throw std::runtime_error("single-instance mutex creation failed");if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(app.mutex);app.mutex=nullptr;CloseHandle(app.ready);app.ready=nullptr;
#if defined(SAT_ENABLE_TEST_HOOKS)
        if(smoke)throw std::runtime_error("UI self-test requires an exclusive instance");
#endif
        if(forwardInstance(a))return 0;throw std::runtime_error("another Translator instance is running");}
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&ic);app.richEditModule=LoadLibraryExW(L"Msftedit.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!app.richEditModule)throw std::runtime_error("RichEdit initialization failed");
    registerWindowClass(inst,mainWindowClass(),mainProc);registerWindowClass(inst,L"SAT.Help",helpProc);registerWindowClass(inst,L"SAT.SettingsViewport",settingsViewportProc);registerWindowClass(inst,L"SAT.Settings",settingsProc);registerWindowClass(inst,L"SAT.CustomPrompt",customPromptProc);registerWindowClass(inst,L"SAT.OcrConfirm",ocrConfirmProc);auto main=createMain(inst);if(startupInput)applyIncoming(std::move(*startupInput));if(a.kind==InputKind::settings)openSettings();if(!app.source.empty()){updatePanes();updateButtons();if(!startupInput){if(app.config.baseUrl.empty())status(L"原文已就绪。请在设置中配置模型后开始翻译。");else if(!app.config.autoStart)status(L"原文已就绪。点击“重新翻译”发送请求。");}}else status(app.config.warning.empty()?L"按 Ctrl+V 粘贴要翻译的文本。可在设置的 ? 查看使用帮助。":app.config.warning);
    (void)main;
#if defined(SAT_ENABLE_TEST_HOOKS)
    if(a.kind==InputKind::mockOcr){app.ocrAdapter=std::make_unique<MockOcrAdapter>(OcrSessionStatus::captured,a.value,1);startOcrCapture();}
    else if(a.kind==InputKind::mockOcrCandidate){app.ocrAdapter=std::make_unique<MockOcrAdapter>(OcrSessionStatus::candidate,a.value,1);startOcrCapture();}
    else
#endif
    if(a.kind==InputKind::snipasteOcr){startOcrCapture();}
    if(stability){
#if defined(SAT_ENABLE_TEST_HOOKS)
        testFinalStability();DestroyWindow(main);
#endif
    }
#if defined(SAT_ENABLE_TEST_HOOKS)
    if(smoke){
        testSettingsRelayout();
        testVisualHardening();
        testFrameTypography();
        testUiReworkInteractions();
        testDiagnosticReport();
        testInputSessions();
        testOcrButtonNotifications();
        testNextStageOcrState();
        testNextStageInteraction();
        testTopTranslationSubmission();
        testRichEditPlaceholderState();
        testUiEventQueue();
        testP1ReviewState();
        if(getText(main)!=mainWindowTitle())throw std::runtime_error("main window title was not preserved");
        RECT statusRect{},settingsRect{},intersection{};if(!GetWindowRect(uiItem(main,statusLabel),&statusRect)||!GetWindowRect(uiItem(main,settingsButton),&settingsRect)||IntersectRect(&intersection,&statusRect,&settingsRect))throw std::runtime_error("status text overlaps the settings button");HWND resultCopy=uiItem(main,resultCopyButton);RECT resultCopyRect{};if(childVisible(resultCopy)&&(!GetWindowRect(resultCopy,&resultCopyRect)||IntersectRect(&intersection,&statusRect,&resultCopyRect)))throw std::runtime_error("status text overlaps the result copy button");
        SetWindowTextW(uiItem(main,sourceEdit),L"Hello\r\n世界 🌏");const auto v=getText(uiItem(main,sourceEdit));if(v!=L"Hello\r\n世界 🌏")throw std::runtime_error("RichEdit Unicode roundtrip failed");std::wstring seed;for(unsigned i=0;i<120;++i)seed+=L"line "+std::to_wstring(i)+L" 中文\r\n";HWND result=uiItem(main,resultEdit);setText(result,seed);SendMessageW(result,EM_SETSEL,10,20);POINT requestedScroll{0,180};SendMessageW(result,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&requestedScroll));POINT beforeScroll{};SendMessageW(result,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&beforeScroll));DWORD beforeStart=0,beforeEnd=0;SendMessageW(result,EM_GETSEL,reinterpret_cast<WPARAM>(&beforeStart),reinterpret_cast<LPARAM>(&beforeEnd));const std::wstring addition=L"尾部追加\r\n";appendRichEditPreservingView(result,addition);POINT afterScroll{};SendMessageW(result,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&afterScroll));DWORD afterStart=0,afterEnd=0;SendMessageW(result,EM_GETSEL,reinterpret_cast<WPARAM>(&afterStart),reinterpret_cast<LPARAM>(&afterEnd));if(getText(result)!=seed+addition||beforeStart!=afterStart||beforeEnd!=afterEnd||beforeScroll.x!=afterScroll.x||beforeScroll.y!=afterScroll.y||(GetWindowLongPtrW(result,GWL_STYLE)&ES_READONLY)==0)throw std::runtime_error("RichEdit streaming append changed text, selection, scroll position, or read-only state");SendMessageW(main,WM_COMMAND,MAKEWPARAM(unlockButton,BN_CLICKED),0);if(!app.editing)throw std::runtime_error("edit mode transition failed");SendMessageW(main,WM_COMMAND,MAKEWPARAM(cancelEditButton,BN_CLICKED),0);DestroyWindow(main);}
#endif
    MSG msg{};BOOL rc;while((rc=GetMessageW(&msg,nullptr,0,0))>0){
        if(deferImeKey(msg.message,static_cast<unsigned>(msg.wParam),imeComposing(msg.hwnd))){TranslateMessage(&msg);DispatchMessageW(&msg);continue;}
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){if(closeAnyDroppedCombo())continue;HWND f=GetFocus();HWND root=GetAncestor(f,GA_ROOT);if(root==app.help){SendMessageW(root,WM_CLOSE,0,0);continue;}if(root==app.settings){SendMessageW(root,WM_CLOSE,0,0);continue;}if(root==app.ocrConfirmWnd){SendMessageW(root,WM_KEYDOWN,VK_ESCAPE,0);continue;}if(root==app.window){SendMessageW(app.window,WM_KEYDOWN,VK_ESCAPE,0);continue;}}
        if(app.settings&&msg.hwnd&&GetAncestor(msg.hwnd,GA_ROOT)==app.settings){if(msg.message==WM_MOUSEWHEEL){SendMessageW(app.settings,WM_MOUSEWHEEL,msg.wParam,msg.lParam);continue;}if(msg.message==WM_KEYDOWN&&(msg.wParam==VK_PRIOR||msg.wParam==VK_NEXT)&&!nativePageNavigation(msg.hwnd)){SendMessageW(app.settings,WM_VSCROLL,MAKEWPARAM(msg.wParam==VK_PRIOR?SB_PAGEUP:SB_PAGEDOWN,0),0);continue;}}
        const bool promptFocused=app.customPromptWnd&&childVisible(app.customPromptWnd)&&msg.hwnd&&(msg.hwnd==app.customPromptWnd||IsChild(app.customPromptWnd,msg.hwnd));
        if(promptFocused&&msg.message==WM_KEYDOWN&&msg.wParam==VK_RETURN&&(GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(app.customPromptWnd,WM_COMMAND,MAKEWPARAM(promptExecuteBtn,BN_CLICKED),0);continue;}
        if(promptFocused&&IsDialogMessageW(app.customPromptWnd,&msg))continue;
        if(msg.message==WM_KEYDOWN&&!promptFocused){const auto action=mapMainAccelerator(static_cast<unsigned>(msg.wParam),(GetKeyState(VK_CONTROL)&0x8000)!=0,(GetKeyState(VK_SHIFT)&0x8000)!=0,app.editing,GetAncestor(GetFocus(),GA_ROOT)==app.window);if(action==MainAccelerator::retranslate){SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(translateButton,BN_CLICKED),0);continue;}if(action==MainAccelerator::submitEdit){SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(translateButton,BN_CLICKED),0);continue;}if(action==MainAccelerator::paste){try{auto pasted=readClipboardOnce();if(!pasted.empty()&&validText(pasted))applyIncoming({false,false,false,InputSource::manualPaste,std::move(pasted)});else status(L"剪贴板没有可用文本。");}catch(...){status(L"读取剪贴板失败。");}continue;}if(action==MainAccelerator::copyResult){status(app.result.empty()?L"当前没有可复制的译文。":copyClipboard(app.result)?L"当前译文已复制。":L"复制译文失败；剪贴板暂不可用。");continue;}}
        if(app.ocrConfirmWnd&&msg.hwnd&&(msg.hwnd==app.ocrConfirmWnd||IsChild(app.ocrConfirmWnd,msg.hwnd))&&IsDialogMessageW(app.ocrConfirmWnd,&msg))continue;
        if(app.help&&msg.hwnd&&GetAncestor(msg.hwnd,GA_ROOT)==app.help&&IsDialogMessageW(app.help,&msg))continue;
        if(app.settings&&msg.hwnd&&GetAncestor(msg.hwnd,GA_ROOT)==app.settings&&IsDialogMessageW(app.settings,&msg))continue;
        if(app.window&&msg.hwnd&&GetAncestor(msg.hwnd,GA_ROOT)==app.window&&IsDialogMessageW(app.window,&msg))continue;
        TranslateMessage(&msg);DispatchMessageW(&msg);}
    if(rc==-1)throw std::runtime_error("message loop failed");return 0;
}
}
using namespace sat;
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);const bool uninit=SUCCEEDED(com);bool selfTest=false;
    try{SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);int argc=0;wchar_t**argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)throw std::runtime_error("command line parse failed");
        if(argc>=2&&(std::wstring(argv[1])==L"--provider-check"||std::wstring(argv[1])==L"--provider-check-online")){
            const bool online=std::wstring(argv[1])==L"--provider-check-online";const bool valid=argc==2||(argc==3&&(std::wstring(argv[2])==L"translation"||std::wstring(argv[2])==L"rewrite"));const bool rewrite=argc==3&&std::wstring(argv[2])==L"rewrite";LocalFree(argv);
            int rc=2;if(valid)rc=runProviderDiagnostic(online,rewrite,testMode());else{constexpr char error[]="{\"status\":\"FAILED\",\"reason\":\"usage_provider_check_optional_translation_or_rewrite\",\"network_requests\":0}\n";DWORD written=0;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),error,sizeof(error)-1,&written,nullptr);}
            if(uninit)CoUninitialize();return rc;
        }
        bool stability=false;
#if defined(SAT_ENABLE_TEST_HOOKS)
        selfTest=testMode()&&argc==2&&std::wstring(argv[1])==L"--self-test-ui";
        stability=testMode()&&argc==2&&std::wstring(argv[1])==L"--stability-test-ui";selfTest=selfTest||stability;
#endif
        Args a;try{if(!stability)a=parseArgs(argc,argv);}catch(...){LocalFree(argv);throw;}LocalFree(argv);if(a.kind==InputKind::diagnose){const int rc=runDiagnostics();if(uninit)CoUninitialize();return rc;}
        const int rc=runGui(instance,a,selfTest&&!stability,stability);if(uninit)CoUninitialize();return rc;
    }catch(const std::exception&e){std::wstring message;try{message=json::decode(e.what());}catch(...){message=L"应用启动失败。";}if(selfTest&&testMode()){OutputDebugStringW(message.c_str());const auto bytes=json::encode(message)+"\n";DWORD written=0;WriteFile(GetStdHandle(STD_ERROR_HANDLE),bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr);}else MessageBoxW(nullptr,message.c_str(),mainWindowTitle(),MB_OK|MB_ICONERROR);if(uninit)CoUninitialize();return 2;}
}
