#pragma once
void testStabilityCore(){
    WSADATA data{};check(WSAStartup(MAKEWORD(2,2),&data)==0,"stress Winsock startup failed");
    struct WinsockCleanup{~WinsockCleanup(){WSACleanup();}}winsock;
    for(bool stream:{false,true}){
        const std::string response=stream?"data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"mock-key\"}]}\n\ndata: [DONE]\n\n":"{\"choices\":[{\"message\":{\"content\":\"partial\"},\"finish_reason\":\"mock-key\"}]}";
        MockServer server({{200,stream?"text/event-stream":"application/json",response}});Events events;
        auto task=sat::TranslationTask::start(requestFor(server),[&](sat::TranslationEvent event){events.add(std::move(event));});check(events.waitTerminal(),"untrusted finish reason did not terminate");task->join();
        bool error=false;for(const auto& event:events.snapshot())if(event.kind==sat::TranslationEvent::Kind::error){error=true;check(event.errorCode.find(L"mock-key")==std::wstring::npos&&event.errorCode==L"finish_reason=unknown","untrusted Provider finish_reason leaked the request Key through error details");}
        check(error,"unknown finish reason was accepted as success");
    }
    const std::string wire="data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"hidden\",\"content\":\"hello 中文 🌏\"}}]}\r\n\r\ndata: [DONE]\r\n\r\n";
    for(size_t split=0;split<=wire.size();++split){std::wstring output;sat::SseParser parser([&](std::wstring_view s){output+=s;});parser.feed(std::string_view(wire).substr(0,split));parser.feed(std::string_view(wire).substr(split));parser.finish();check(parser.done()&&output==L"hello 中文 🌏","SSE split point corrupted content or exposed reasoning");}
    for(int i=0;i<100;++i){
        const bool cancelled=i%2==0;
        MockServer server({{200,"text/event-stream",wire,cancelled}});Events events;
        auto request=requestFor(server);request.requestId=static_cast<uint64_t>(i)+1000;request.sourceSessionId=static_cast<uint64_t>(i)+2000;
        auto task=sat::TranslationTask::start(request,[&](sat::TranslationEvent event){events.add(std::move(event));});
        check(server.waitRequests(1),"stress request did not reach the controlled server");
        if(cancelled){task->cancel();check(events.waitTerminal(),"held request did not cancel before releasing the response");server.release();}
        else check(events.waitTerminal(),"stress request did not finish");
        task->join();size_t terminals=0;std::wstring output;
        for(const auto& event:events.snapshot()){
            check(event.requestId==request.requestId&&event.sourceSessionId==request.sourceSessionId,"stress request crossed session identity");
            if(event.kind==sat::TranslationEvent::Kind::delta)output+=event.text;
            else{++terminals;check(event.kind==(cancelled?sat::TranslationEvent::Kind::cancelled:sat::TranslationEvent::Kind::complete),"cancellation admitted a late success or success changed terminal kind");}
        }
        check(terminals==1&&task->finished(),"stress request had duplicate terminals or an unfinished worker");
        check(cancelled?output.empty():output==L"hello 中文 🌏","stress request output mismatch");
    }
    std::cout<<"stability_network: PASS (100 real WinHTTP loopback requests; 50 gated cancellation/late response, 50 success; "<<wire.size()+1<<" SSE split positions)\n";
}
