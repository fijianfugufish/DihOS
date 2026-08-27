#include "browser.h"

static int scheme_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
}

static int hex_value(char c)
{
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}

int browser_url_unwrap_navigation(const char *url, char *out, uint32_t cap)
{
    const char *encoded=0;uint32_t n=0u;
    if(!url||!out||cap<8u)return -1;
    for(uint32_t i=0u;url[i];++i){
        if((i==0u||url[i-1u]=='?'||url[i-1u]=='&')&&url[i]=='u'&&url[i+1u]=='d'&&url[i+2u]=='d'&&url[i+3u]=='g'&&url[i+4u]=='='){encoded=url+i+5u;break;}
    }
    if(!encoded||(!b_starts(url,"https://duckduckgo.com/")&&!b_starts(url,"https://html.duckduckgo.com/")&&!b_starts(url,"//duckduckgo.com/"))){b_copy(out,cap,url);return b_strlen(url)+1u<cap?0:-1;}
    while(*encoded&&*encoded!='&'){
        unsigned char c=(unsigned char)*encoded++;
        if(c=='%'&&encoded[0]&&encoded[1]){int a=hex_value(encoded[0]),b=hex_value(encoded[1]);if(a>=0&&b>=0){c=(unsigned char)(a*16+b);encoded+=2;}}
        else if(c=='+')c=' ';
        if(n+1u>=cap)return -1;out[n++]=(char)c;
    }
    out[n]=0;
    if(b_starts(out,"https://en.wikipedia.org/")){
        char mobile[DIHSCOVER_URL_CAP];const char old_prefix[]="https://en.wikipedia.org/",new_prefix[]="https://en.m.wikipedia.org/";uint32_t p=0u;for(uint32_t i=0u;new_prefix[i]&&p+1u<sizeof(mobile);++i)mobile[p++]=new_prefix[i];for(uint32_t i=(uint32_t)sizeof(old_prefix)-1u;out[i]&&p+1u<sizeof(mobile);++i)mobile[p++]=out[i];mobile[p]=0;b_copy(out,cap,mobile);
    }
    return (b_starts(out,"http://")||b_starts(out,"https://"))?0:-1;
}

int browser_url_normalize(const char *input, char *out, uint32_t cap)
{
    uint32_t start = 0u, end, n = 0u;
    if (!input || !out || cap < 16u) return -1;
    end = b_strlen(input);
    while (start < end && (input[start] == ' ' || input[start] == '\t')) ++start;
    while (end > start && (input[end - 1u] == ' ' || input[end - 1u] == '\t')) --end;
    if (end == start) return -1;
    if (!b_starts(input + start, "http://") && !b_starts(input + start, "https://")) {
        int looks_like_host = 0;
        for (uint32_t i = start; i < end; ++i)
            if (input[i] == '.' || input[i] == ':' || input[i] == '/') { looks_like_host = 1; break; }
        if (!looks_like_host) {
            const char search[] = "https://html.duckduckgo.com/html/?q=";
            static const char hex[] = "0123456789ABCDEF";
            for (uint32_t i = 0u; search[i]; ++i) { if (n + 1u >= cap) return -1; out[n++] = search[i]; }
            for (uint32_t i = start; i < end; ++i) {
                unsigned char c = (unsigned char)input[i];
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
                    if (n + 1u >= cap) return -1; out[n++] = (char)c;
                } else {
                    if (n + 3u >= cap) return -1; out[n++] = '%'; out[n++] = hex[c >> 4]; out[n++] = hex[c & 15u];
                }
            }
            out[n] = 0;
            return 0;
        }
    }
    if (!b_starts(input + start, "http://") && !b_starts(input + start, "https://")) {
        const char prefix[] = "https://";
        for (uint32_t i = 0u; prefix[i] && n + 1u < cap; ++i) out[n++] = prefix[i];
    }
    for (uint32_t i = start; i < end && n + 1u < cap; ++i) out[n++] = input[i];
    out[n] = 0;
    return n + 1u < cap ? 0 : -1;
}

int browser_url_resolve(const char *base, const char *relative, char *out, uint32_t cap)
{
    char raw[DIHSCOVER_URL_CAP];
    uint32_t i = 0u, authority, base_end, n = 0u, path, read, write;
    if (!base || !relative || !out || cap < 8u) return -1;
    if (!relative[0]) { b_copy(out,cap,base); return b_strlen(base)+1u<cap?0:-1; }
    if (b_starts(relative,"data:") || b_starts(relative,"blob:") || b_starts(relative,"javascript:")) return -1;
    while (scheme_char(relative[i])) ++i;
    if (i && relative[i] == ':' && relative[i + 1u] == '/' && relative[i + 2u] == '/') {
        b_copy(out, cap, relative);
        return b_strlen(relative) + 1u < cap ? 0 : -1;
    }
    i = 0u;
    while (scheme_char(base[i])) ++i;
    if (!i || base[i] != ':' || base[i + 1u] != '/' || base[i + 2u] != '/') return -1;
    authority = i + 3u;
    while (base[authority] && base[authority] != '/') ++authority;
    if (relative[0] == '/' && relative[1] == '/') {
        for (uint32_t k = 0u; k <= i && n + 1u < sizeof(raw); ++k) raw[n++] = base[k];
        for (uint32_t k = 0u; relative[k] && n + 1u < sizeof(raw); ++k) raw[n++] = relative[k];
    } else if (relative[0] == '?' || relative[0] == '#') {
        base_end=0u;
        while(base[base_end] && base[base_end]!='#' && !(relative[0]=='?'&&base[base_end]=='?'))++base_end;
        for(uint32_t k=0u;k<base_end&&n+1u<sizeof(raw);++k)raw[n++]=base[k];
        for(uint32_t k=0u;relative[k]&&n+1u<sizeof(raw);++k)raw[n++]=relative[k];
    } else {
        base_end = relative[0] == '/' ? authority : b_strlen(base);
        while(base_end>authority&&(base[base_end-1u]=='#'||base[base_end-1u]=='?'))--base_end;
        for(uint32_t k=authority;k<base_end;++k)if(base[k]=='?'||base[k]=='#'){base_end=k;break;}
        if (relative[0] != '/') while (base_end > authority && base[base_end - 1u] != '/') --base_end;
        for (uint32_t k = 0u; k < base_end && n + 1u < sizeof(raw); ++k) raw[n++] = base[k];
        for (uint32_t k = 0u; relative[k] && n + 1u < sizeof(raw); ++k) raw[n++] = relative[k];
    }
    raw[n] = 0;
    if(n+1u>=sizeof(raw))return -1;

    path=authority;
    while(raw[path]&&raw[path]!='/')++path;
    if(!raw[path]){b_copy(out,cap,raw);return n+1u<cap?0:-1;}
    for(write=0u;write<=path&&write+1u<cap;++write)out[write]=raw[write];
    read=path+1u;
    while(raw[read]&&raw[read]!='?'&&raw[read]!='#'){
        uint32_t seg=read;
        while(raw[read]&&raw[read]!='/'&&raw[read]!='?'&&raw[read]!='#')++read;
        uint32_t len=read-seg;
        if(len==1u&&raw[seg]=='.'){}
        else if(len==2u&&raw[seg]=='.'&&raw[seg+1u]=='.'){
            if(write>path+1u){--write;while(write>path+1u&&out[write-1u]!='/')--write;}
        }else{
            for(uint32_t k=0u;k<len&&write+1u<cap;++k)out[write++]=raw[seg+k];
            if(raw[read]=='/'&&write+1u<cap)out[write++]='/';
        }
        if(raw[read]=='/')++read;
    }
    while(raw[read]&&write+1u<cap)out[write++]=raw[read++];
    out[write]=0;
    return raw[read]? -1:0;
}
