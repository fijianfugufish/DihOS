#pragma once
#include <string>
#include <string_view>
namespace DihosNavigation {
inline std::string escapeErrorHTML(std::string_view value){
    std::string out;
    for(char c:value){switch(c){
    case '&':out+="&amp;";break;case '<':out+="&lt;";break;
    case '>':out+="&gt;";break;case '"':out+="&quot;";break;
    case '\'':out+="&#39;";break;default:out+=c;
    }}return out;
}
inline std::string errorPage(std::string_view url,std::string_view reason,int code){
    // No external resources or scripts; untrusted URLs and error text are
    // escaped separately for both text and the retry attribute.
    auto safeURL=escapeErrorHTML(url.substr(0,768));
    auto target=(url.starts_with("https://") || url.starts_with("http://"))?safeURL:std::string("about:blank");
    return "<!doctype html><meta charset=utf-8><meta name=referrer content=no-referrer>"
        "<meta http-equiv=Content-Security-Policy content=\"default-src 'none'; style-src 'unsafe-inline'\">"
        "<title>Unable to load page</title><style>body{font:16px sans-serif;background:#f4f6f8;color:#202830;margin:0;padding:40px}"
        "main{max-width:620px;margin:40px auto;background:white;border:1px solid #ccd3da;border-radius:12px;padding:32px}"
        "p{line-height:1.6;overflow-wrap:anywhere}a{display:inline-block;padding:10px 20px;background:#2459b7;color:white;border-radius:6px;text-decoration:none}"
        "small{color:#56616d}</style><main><h1>Unable to load this page</h1><p>"+safeURL+"</p><p>"+
        escapeErrorHTML(reason.empty()?"The network request failed.":reason.substr(0,512))+
        "</p><p><small>Network error "+std::to_string(code)+
        ". Check the connection, or try again.</small></p><a href=\""+target+"\">Try again</a>"
        "<p><small>You can also use Back or enter another address above.</small></p></main>";
}
}
