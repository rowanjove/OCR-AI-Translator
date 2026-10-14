#pragma once
#if defined(SAT_ENABLE_TEST_HOOKS)
json::Value snapshotStabilityBrushes(){
    json::Value::Object snapshot;
    if(GetEnvironmentVariableW(L"SAT_TEST_GDI_TRACE",nullptr,0)==0)return json::Value(std::move(snapshot));
    using Query=LONG(NTAPI*)(HANDLE,ULONG,PVOID,ULONG,PULONG);
    using VersionQuery=LONG(WINAPI*)(POSVERSIONINFOW);
    const auto versionAddress=GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion");VersionQuery versionQuery{};
    static_assert(sizeof(versionQuery)==sizeof(versionAddress));std::memcpy(&versionQuery,&versionAddress,sizeof(versionQuery));
    OSVERSIONINFOW version{sizeof(version)};
    if(!versionQuery||versionQuery(&version)<0||version.dwBuildNumber!=19045){snapshot["error"]="Diagnostic layout unsupported on this OS build";return json::Value(std::move(snapshot));}
    const auto address=GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationProcess");Query query{};
    static_assert(sizeof(query)==sizeof(address));std::memcpy(&query,&address,sizeof(query));
    ULONG_PTR basic[6]{};ULONG needed=0;
    if(!query||query(GetCurrentProcess(),0,basic,sizeof(basic),&needed)<0){snapshot["error"]="PEB query failed";return json::Value(std::move(snapshot));}
    // Optional Win10 x64 diagnostic only; never used to decide test success.
    // Layout reference: System Informer phnt/ntgdi.h and ntpebteb.h.
    struct Entry{ULONG_PTR object;USHORT pid,count,unique;BYTE type,flags;ULONG_PTR user;};
    static_assert(sizeof(Entry)==24);
    ULONG_PTR table=0;SIZE_T bytes=0;
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(basic[1]+0xF8),&table,sizeof(table),&bytes)||bytes!=sizeof(table)||!table){snapshot["error"]="GDI table unavailable";return json::Value(std::move(snapshot));}
    std::vector<Entry> entries(65535);
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(table),entries.data(),entries.size()*sizeof(Entry),&bytes)||bytes!=entries.size()*sizeof(Entry)){snapshot["error"]="GDI table read failed";return json::Value(std::move(snapshot));}
    json::Value::Array brushes;
    for(size_t index=0;index<entries.size();++index){
        const auto& entry=entries[index];if(entry.pid!=static_cast<USHORT>(GetCurrentProcessId())||entry.type!=16)continue;
        const ULONG_PTR value=(static_cast<ULONG_PTR>(entry.unique)<<16)|index;
        if(((value>>16)&127)!=16)continue;
        LOGBRUSH brush{};const int result=GetObjectW(reinterpret_cast<HBRUSH>(value),sizeof(brush),&brush);
        json::Value::Object item;item["handle"]=static_cast<double>(value);item["attributes_valid"]=result==sizeof(brush);item["style"]=static_cast<double>(brush.lbStyle);item["color"]=static_cast<double>(brush.lbColor);
        item["app_background"]=reinterpret_cast<ULONG_PTR>(g->backgroundBrush)==value;item["app_control"]=reinterpret_cast<ULONG_PTR>(g->controlBrush)==value;brushes.emplace_back(std::move(item));
    }
    snapshot["brushes"]=std::move(brushes);return json::Value(std::move(snapshot));
}
void testFinalStability(){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const bool oldAuto=g->config.autoStart;const auto oldSnipastePath=g->config.snipasteExePath;g->config.autoStart=false;g->config.snipasteExePath=(g->config.folder/L"unavailable-Snipaste.exe").wstring();
    auto emit=[&](TranslationEvent::Kind kind,const std::wstring& text=L""){
        postTranslationEvent(g->uiQueue,{kind,g->activeRequestId,g->revision,false,text,g->inputSession.id});
        processNetworkEvents();
    };
    auto start=[&]{g->retryTargetLanguage=g->language;g->activeRequestId=++g->requestSerial;g->activeSourceId=g->inputSession.id;g->requestActive=true;g->result.clear();updatePanes();};
    applyIncoming({false,false,false,InputSource::mockText,L"old failed source"});start();emit(TranslationEvent::Kind::error,L"old source failure");
    g->activeOpType=AiOperationType::custom;g->activeCustomScope=CustomPromptScope::sourceOnly;g->activeCustomPrompt=L"old source instruction";
    applyIncoming({false,false,false,InputSource::mockText,L"new source after failure"});
    require(!g->retryAvailable&&g->lastErrorDetails.empty()&&g->retryTargetLanguage.empty()&&g->activeCustomPrompt.empty(),"new source retained Retry/error state from the previous source operation");
    g->language=L"zh-Hans";g->config.targetLanguage=g->language;
    applyIncoming({false,false,false,InputSource::mockText,L"stability source"});
    start();emit(TranslationEvent::Kind::delta,L"complete Chinese result");emit(TranslationEvent::Kind::complete);
    const auto savedVersion=g->currentVersionId;
    uiMessage(g->window,targetCombo,CB_SETCURSEL,2,0);SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(targetCombo,CBN_SELCHANGE),0);
    // Model the real error ordering after changing the target language.
    start();emit(TranslationEvent::Kind::delta,L"partial English");emit(TranslationEvent::Kind::error,L"fixture failure");
    require(g->result==L"complete Chinese result"&&g->language==L"zh-Hans"&&completedVersion(),"failed target-language request restored text under the wrong language or lost its version association");
    require(g->currentVersionId==savedVersion,"failure did not restore the previous complete version");
    require(g->retryTargetLanguage==L"en"&&uiMessage(g->window,targetCombo,CB_GETCURSEL,0,0)==0,"failure lost the retry language or did not restore the language control");
    selectTargetLanguage(L"ja");invalidateCompletedAssociation();start();emit(TranslationEvent::Kind::delta,L"partial Japanese");SendMessageW(g->window,WM_COMMAND,MAKEWPARAM(requestCancelButton,BN_CLICKED),0);
    require(g->language==L"zh-Hans"&&g->result==L"complete Chinese result"&&completedVersion(),"explicit cancellation did not restore the complete language/version association");
    selectTargetLanguage(L"en");invalidateCompletedAssociation();start();emit(TranslationEvent::Kind::cancelled);
    require(g->language==L"zh-Hans"&&completedVersion(),"worker cancellation did not restore the previous complete version");
    selectTargetLanguage(L"en");invalidateCompletedAssociation();start();emit(TranslationEvent::Kind::delta,std::wstring(maxTextBytes/sizeof(wchar_t)+1,L'x'));
    require(g->language==L"zh-Hans"&&completedVersion()&&g->result==L"complete Chinese result","output-limit cancellation did not restore the previous complete association");
    start();emit(TranslationEvent::Kind::error,L"old source failure");g->activeOpType=AiOperationType::custom;g->activeCustomScope=CustomPromptScope::sourceOnly;g->activeCustomPrompt=L"old source instruction";
    applyIncoming({false,false,false,InputSource::mockText,L"new source after failure"});
    require(!g->retryAvailable&&g->lastErrorDetails.empty()&&g->retryTargetLanguage.empty(),"new source retained Retry/error state from the previous source operation");
    start();emit(TranslationEvent::Kind::delta,L"new complete result");emit(TranslationEvent::Kind::complete);
    start();emit(TranslationEvent::Kind::error,L"failed before editing");acceptEditedSource(L"edited source after failure");
    require(!g->retryAvailable&&g->lastErrorDetails.empty()&&g->retryTargetLanguage.empty()&&g->activeCustomPrompt.empty(),"edited source retained a previous operation's Retry state");
    start();emit(TranslationEvent::Kind::delta,L"edited complete result");emit(TranslationEvent::Kind::complete);
    const Theme originalTheme=g->config.theme;
    for(int i=0;i<100;++i){
        start();const auto id=g->activeRequestId,revision=g->revision,sourceId=g->inputSession.id;
        emit(TranslationEvent::Kind::delta,L"temporary output");
        if(i%3==0){cancelOperation();}
        else if(i%3==1){emit(TranslationEvent::Kind::error,L"fixture error");}
        else{emit(TranslationEvent::Kind::complete);}
        const auto count=g->versions.size();const auto result=g->result;
        postTranslationEvent(g->uiQueue,{TranslationEvent::Kind::delta,id,revision,false,L"late output",sourceId});
        postTranslationEvent(g->uiQueue,{TranslationEvent::Kind::complete,id,revision,false,{},sourceId});processNetworkEvents();
        require(g->result==result&&g->versions.size()==count&&!g->requestActive,"late response changed the completed/cancelled request");
        require(completedVersion()!=nullptr,"request terminal state lost the complete recovery version");
    }
    for(int i=0;i<100;++i){
        const auto source=g->source;const auto session=g->inputSession.id;
        const auto state=i%3==0?OcrSessionStatus::captured:i%3==1?OcrSessionStatus::timeout:OcrSessionStatus::cancelled;
        g->ocrAdapter=std::make_unique<MockOcrAdapter>(state,L"OCR fixture "+std::to_wstring(i),0);
        ShowWindow(g->window,SW_SHOWNOACTIVATE);startOcrCapture();pollOcrSession();
        require(!g->captureSessionActive&&!g->clipboardListenerRegistered&&!g->ocrConfirming,"OCR terminal state retained a listener or active session");
        if(state==OcrSessionStatus::captured)require(g->source==L"OCR fixture "+std::to_wstring(i)&&g->inputSession.id!=session,"OCR session did not commit an isolated source");
        else require(g->source==source&&g->inputSession.id==session,"OCR cancellation/timeout changed the source");
        const auto finalSource=g->source;pollOcrSession();require(g->source==finalSource,"late OCR polling revived a finished session");
    }
    g->ocrAdapter.reset();
    // Warm the same native windows and fonts before measuring repeated operations.
    openSettings();openHelp();DestroyWindow(g->settings);setTheme();
    for(int i=0;i<10;++i){openSettings();uiMessage(g->settings,settingTheme,CB_SETCURSEL,i%2?1:2,0);SendMessageW(g->settings,WM_COMMAND,MAKEWPARAM(settingTheme,CBN_SELCHANGE),0);SendMessageW(g->settings,WM_COMMAND,MAKEWPARAM(i%2?settingSave:settingCancel,BN_CLICKED),0);require(!g->settings,"settings warmup did not close");if(i%2){auto loaded=loadConfig();require(!loaded.damagedConfig,"settings warmup reload failed");clearConfigSecrets(loaded);}pumpPostedUiMessages();}
    DWORD handlesBeforeRenderWarmup=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBeforeRenderWarmup);
    auto resourceSnapshot=[](int cycle,const char* phase){
        DWORD handles=0;GetProcessHandleCount(GetCurrentProcess(),&handles);json::Value::Object sample;
        sample["cycle"]=static_cast<double>(cycle);sample["phase"]=phase;
        sample["gdi"]=static_cast<double>(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS));
        sample["user"]=static_cast<double>(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS));
        sample["handles"]=static_cast<double>(handles);return json::Value(std::move(sample));
    };
    json::Value::Array renderWarmupSamples,renderSamples;
    auto quiesce=[&]{
        SetFocus(nullptr);if(g->tooltip)SendMessageW(g->tooltip,TTM_POP,0,0);g->config.theme=originalTheme;
        // Hidden native controls may retain cached brushes until they repaint.
        // Compare identical painted state, not just identical theme and geometry.
        SetWindowPos(g->window,nullptr,0,0,850,380,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        setTheme();RedrawWindow(g->window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);pumpPostedUiMessages();
        ShowWindow(g->window,SW_HIDE);pumpPostedUiMessages();
    };
    quiesce();
    const DWORD gdiBeforeSettings=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),userBeforeSettings=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    auto brushesBefore=snapshotStabilityBrushes();
    DWORD handlesBeforeSettings=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBeforeSettings);
    json::Value::Array settingsSamples,settingsStageSamples;
    auto recordSettingsStage=[&](int cycle,const char* phase){DWORD handles=0;GetProcessHandleCount(GetCurrentProcess(),&handles);json::Value::Object sample;sample["cycle"]=static_cast<double>(cycle);sample["phase"]=phase;sample["gdi"]=static_cast<double>(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS));sample["user"]=static_cast<double>(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS));sample["handles"]=static_cast<double>(handles);settingsStageSamples.emplace_back(std::move(sample));};
    for(int i=0;i<50;++i){
        if(i<2)recordSettingsStage(i+1,"before_open");
        openSettings();require(g->settings&&IsWindow(g->settings),"settings creation failed in stress loop");
        if(i<2)recordSettingsStage(i+1,"after_open");
        uiMessage(g->settings,settingTheme,CB_SETCURSEL,i%2?1:2,0);SendMessageW(g->settings,WM_COMMAND,MAKEWPARAM(settingTheme,CBN_SELCHANGE),0);
        if(i<2)recordSettingsStage(i+1,"after_theme_preview");
        const auto liveTheme=g->config.theme;
        if(i%2==0){SendMessageW(g->settings,WM_COMMAND,MAKEWPARAM(settingCancel,BN_CLICKED),0);require(g->config.theme==liveTheme,"settings cancellation committed its preview");}
        else{SendMessageW(g->settings,WM_COMMAND,MAKEWPARAM(settingSave,BN_CLICKED),0);require(!g->settings,"settings save failed in stress loop");if(i<2)recordSettingsStage(i+1,"after_save_close");auto loaded=loadConfig();require(!loaded.damagedConfig&&loaded.theme==g->config.theme&&loaded.translationProfileId==g->config.translationProfileId,"settings reload differs from committed data");clearConfigSecrets(loaded);if(i<2)recordSettingsStage(i+1,"after_reload");}
        if(i%2==0&&i<2)recordSettingsStage(i+1,"after_cancel_close");
        require(!g->settings&&!g->help,"settings/help window survived close");
        pumpPostedUiMessages();
        if(i<2)recordSettingsStage(i+1,"after_pump");
        if(i==0||i==1||i==9||i==24||i==49){DWORD handles=0;GetProcessHandleCount(GetCurrentProcess(),&handles);json::Value::Object sample;sample["cycle"]=static_cast<double>(i+1);sample["gdi"]=static_cast<double>(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS));sample["user"]=static_cast<double>(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS));sample["handles"]=static_cast<double>(handles);settingsSamples.emplace_back(std::move(sample));}
    }
    const DWORD gdiSettings=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),userSettings=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    DWORD handlesAfterSettings=0;GetProcessHandleCount(GetCurrentProcess(),&handlesAfterSettings);
    if(gdiSettings>gdiBeforeSettings||userSettings>userBeforeSettings||handlesAfterSettings>handlesBeforeSettings){
        const std::string counts="settings_resource_counts handles="+std::to_string(handlesBeforeSettings)+"->"+std::to_string(handlesAfterSettings)+" gdi="+std::to_string(gdiBeforeSettings)+"->"+std::to_string(gdiSettings)+" user="+std::to_string(userBeforeSettings)+"->"+std::to_string(userSettings);
        const std::string diagnostic=counts+" samples="+json::stringify(json::Value(std::move(settingsSamples)))+" stages="+json::stringify(json::Value(std::move(settingsStageSamples)));
        require(false,diagnostic.c_str());
    }
    // Re-prime rendering after the settings stress loop, which commits theme changes.
    // Cover the same full operation sequence as the measured block before taking its baseline.
    DWORD lastObservedHandles=0;GetProcessHandleCount(GetCurrentProcess(),&lastObservedHandles);json::Value::Array handleTransitions;
    auto recordHandleTransition=[&](int cycle,const char* phase){DWORD current=0;GetProcessHandleCount(GetCurrentProcess(),&current);if(current!=lastObservedHandles){json::Value::Object sample;sample["cycle"]=static_cast<double>(cycle);sample["phase"]=phase;sample["from"]=static_cast<double>(lastObservedHandles);sample["to"]=static_cast<double>(current);handleTransitions.emplace_back(std::move(sample));lastObservedHandles=current;}};
    for(int i=0;i<100;++i){g->config.theme=i%2?Theme::dark:Theme::light;setTheme();recordHandleTransition(i+1,"warmup_after_theme");SetWindowPos(g->window,nullptr,0,0,700+i%5*35,410+i%4*20,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);recordHandleTransition(i+1,"warmup_after_resize");ShowWindow(g->window,i%2?SW_HIDE:SW_SHOWNOACTIVATE);UpdateWindow(g->window);pumpPostedUiMessages();renderWarmupSamples.emplace_back(resourceSnapshot(i+1,"warmup"));}
    quiesce();
    const DWORD gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),userBefore=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    DWORD handlesBefore=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);lastObservedHandles=handlesBefore;
    brushesBefore=snapshotStabilityBrushes();
    // Keep recording phase transitions during the measured window without changing the gate.
    auto recordMeasuredHandleTransition=[&](int cycle,const char* phase){DWORD current=0;GetProcessHandleCount(GetCurrentProcess(),&current);if(current!=lastObservedHandles){json::Value::Object sample;sample["cycle"]=static_cast<double>(cycle);sample["phase"]=phase;sample["from"]=static_cast<double>(lastObservedHandles);sample["to"]=static_cast<double>(current);handleTransitions.emplace_back(std::move(sample));lastObservedHandles=current;}};
    for(int i=0;i<100;++i){g->config.theme=i%2?Theme::dark:Theme::light;setTheme();recordMeasuredHandleTransition(i+1,"after_theme");SetWindowPos(g->window,nullptr,0,0,700+i%5*35,410+i%4*20,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);recordMeasuredHandleTransition(i+1,"after_resize");ShowWindow(g->window,i%2?SW_HIDE:SW_SHOWNOACTIVATE);recordMeasuredHandleTransition(i+1,"after_visibility");UpdateWindow(g->window);recordMeasuredHandleTransition(i+1,"after_update");pumpPostedUiMessages();recordMeasuredHandleTransition(i+1,"after_messages");renderSamples.emplace_back(resourceSnapshot(i+1,"measured"));}
    quiesce();
    const DWORD gdiAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),userAfter=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    auto brushesAfter=snapshotStabilityBrushes();
    DWORD handlesAfter=0;GetProcessHandleCount(GetCurrentProcess(),&handlesAfter);
    json::Value::Object metrics;metrics["ocr_sessions"]=100.0;metrics["request_event_sequences"]=100.0;metrics["settings_cycles"]=50.0;metrics["theme_resize_cycles"]=100.0;
    metrics["gdi_before"]=static_cast<double>(gdiBefore);metrics["gdi_after"]=static_cast<double>(gdiAfter);metrics["user_before"]=static_cast<double>(userBefore);metrics["user_after"]=static_cast<double>(userAfter);metrics["handles_before"]=static_cast<double>(handlesBefore);metrics["handles_after"]=static_cast<double>(handlesAfter);
    metrics["gdi_before_settings"]=static_cast<double>(gdiBeforeSettings);metrics["gdi_after_settings"]=static_cast<double>(gdiSettings);metrics["user_before_settings"]=static_cast<double>(userBeforeSettings);metrics["user_after_settings"]=static_cast<double>(userSettings);metrics["handles_before_settings"]=static_cast<double>(handlesBeforeSettings);metrics["handles_after_settings"]=static_cast<double>(handlesAfterSettings);
    metrics["settings_samples"]=std::move(settingsSamples);metrics["handle_transitions"]=std::move(handleTransitions);
    metrics["render_warmup_samples"]=std::move(renderWarmupSamples);metrics["render_samples"]=std::move(renderSamples);
    metrics["brushes_before"]=std::move(brushesBefore);metrics["brushes_after"]=std::move(brushesAfter);
    metrics["theme_brush_delete_failures"]=static_cast<double>(g->failedThemeBrushDeletes);
    metrics["settings_warmup_cycles"]=10.0;
    metrics["render_warmup_cycles"]=100.0;metrics["handles_before_render_warmup"]=static_cast<double>(handlesBeforeRenderWarmup);
    std::ofstream report(g->config.folder/L"gui-stability.json",std::ios::binary);report<<json::stringify(json::Value(std::move(metrics)));require(static_cast<bool>(report),"GUI stress evidence could not be written");
    require(g->failedThemeBrushDeletes==0,"theme replacement failed to release an owned brush");
    require(gdiAfter<=gdiBefore&&userAfter<=userBefore&&handlesAfter<=handlesBefore,"repeated GUI operations leaked GDI/USER/kernel handles");
    g->config.autoStart=oldAuto;g->config.snipasteExePath=oldSnipastePath;
}
#endif
