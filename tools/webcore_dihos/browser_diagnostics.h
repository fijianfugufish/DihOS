#pragma once
#include <array>
#include <cstddef>
#include <cstdio>
#include <string_view>

namespace DihosNavigation {
// Renderer-local, bounded snapshots. No cookies, headers or response bodies.
struct BrowserDiagnostics {
    std::array<char,512> error{},warning{},request{};
    unsigned errors{},warnings{},failures{};
    static void capture(std::array<char,512>& out,std::string_view text){
        size_t n=text.size()<out.size()-1?text.size():out.size()-1;
        for(size_t i=0;i<n;++i){unsigned char c=text[i];out[i]=c<32 || c==127?' ':c;}
        out[n]=0;
    }
    void console(bool isError,std::string_view text){
        auto& count=isError?errors:warnings;if(count<0xffffffffu)++count;
        capture(isError?error:warning,text);
    }
    void failed(std::string_view url,int code,std::string_view reason){
        if(failures<0xffffffffu)++failures;
        char line[512];std::snprintf(line,sizeof(line),"Request error %d: %.*s | %.*s",code,
            int(reason.size()<240?reason.size():240),reason.data(),int(url.size()<220?url.size():220),url.data());
        capture(request,line);
    }
    const char* summary() const {
        return error[0]?error.data():request[0]?request.data():warning[0]?warning.data():"No script warnings or failed requests recorded";
    }
};
inline BrowserDiagnostics browserDiagnostics;
}
