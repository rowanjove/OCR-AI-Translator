#pragma once
int stabilityProcessTests(const std::wstring& exe){
    const auto folder=requireTestDirectory(L"process-test-config");
    SseResponseServer server({{200,"complete Chinese"},{503,"PRIVATE_ERROR"},{200,"complete English"},{503,"PRIVATE_ERROR"},{200,"settings retry Japanese"}});
    Child app=launch(exe,L"--mock-text \"stable source\"");
    click(app.window,109);HWND settings=FindWindowW(L"SAT.Settings",nullptr);check(settings!=nullptr,"stress settings missing");
    check(setRemoteText(settings,302,server.url())&&setRemoteText(settings,303,L"stability-model")&&setRemoteText(settings,304,L"stability-fixture-key"),"stress provider fields failed");
    check(setRemoteText(settings,343,(std::filesystem::path(exe).parent_path()/L"SnipasteLaunchFixture.exe").wstring()),"stress OCR fixture path failed");
    testMessage(settings,344,CB_SETCURSEL,0,0);testMessage(settings,324,BM_SETCHECK,BST_UNCHECKED,0);testMessage(settings,300,CB_SETCURSEL,0,0);click(settings,314);check(!IsWindow(settings),"stress settings save failed");
    auto await=[&](auto condition,const char* message){const auto until=GetTickCount64()+5000;while(GetTickCount64()<until){if(condition())return;Sleep(5);}throw std::runtime_error(message);};
    auto result=[&](const wchar_t* value){await([&]{return textOf(testItem(app.window,101))==value&&textOf(testItem(app.window,110))==L"翻译完成。";},"stress streamed result mismatch");};
    auto failure=[&]{await([&]{return textOf(testItem(app.window,110)).find(L"HTTP 503")!=std::wstring::npos;},"stress HTTP error missing");};
    click(app.window,129);result(L"complete Chinese");
    testMessage(app.window,102,CB_SETCURSEL,2,0);SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(102,CBN_SELCHANGE),0);failure();
    check(textOf(testItem(app.window,101))==L"complete Chinese"&&testMessage(app.window,102,CB_GETCURSEL,0,0)==0&&IsWindowEnabled(testItem(app.window,119)),"real HTTP error did not restore Chinese result, language control and version association");
    click(app.window,127);result(L"complete English");check(server.requests().size()==3&&server.requests()[2].find("Translate the supplied text into en.")!=std::string::npos,"Retry forgot the failed English target");
    click(app.window,109);settings=FindWindowW(L"SAT.Settings",nullptr);testMessage(settings,300,CB_SETCURSEL,3,0);click(settings,314);check(!IsWindow(settings),"settings target save failed");click(app.window,129);failure();
    check(textOf(testItem(app.window,101))==L"complete English"&&testMessage(app.window,102,CB_GETCURSEL,0,0)==2,"settings target failure did not restore previous English association");
    click(app.window,127);result(L"settings retry Japanese");check(server.requests().size()==5&&server.requests()[4].find("Translate the supplied text into ja.")!=std::string::npos,"settings Retry forgot the failed Japanese target");
    SendMessageW(app.window,WM_CLOSE,0,0);check(app.waitExit()&&app.exitCode()==0,"stress translation process did not close");app.window=nullptr;
    for(int i=0;i<50;++i){
        Child child=launch(exe,L"");ShowWindow(child.window,SW_SHOWNOACTIVATE);check(IsWindowVisible(child.window),"stress HWND did not show");ShowWindow(child.window,SW_HIDE);check(!IsWindowVisible(child.window),"stress HWND did not hide");ShowWindow(child.window,SW_SHOWNOACTIVATE);check(IsWindowVisible(child.window),"stress HWND did not restore");
        SendMessageW(child.window,WM_CLOSE,0,0);check(child.waitExit()&&child.exitCode()==0,"stress process close hung or failed");child.window=nullptr;
    }
    std::cout<<"stability_process: PASS (5 real WinHTTP loopback requests; failed target restore/Retry, settings restore/Retry; 50 native window create/show/hide/restore/close)\n";
    return 0;
}
