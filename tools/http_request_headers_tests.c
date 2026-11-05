#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "net/http_request_headers.h"
int main(void)
{
    const char *valid="Accept: text/html\r\nUser-Agent: DihOS\r\nX-Empty:\r\n";
    assert(!dihos_http_validate_request_headers(0,0));
    assert(!dihos_http_validate_request_headers(valid,(uint32_t)strlen(valid)));
    assert(dihos_http_request_has_header(valid,(uint32_t)strlen(valid),"accept"));
    assert(dihos_http_request_has_header(valid,(uint32_t)strlen(valid),"user-agent"));
    assert(!dihos_http_request_has_header(valid,(uint32_t)strlen(valid),"accept-language"));
    /* Every nonempty partial line must fail; a prefix ending after a complete
     * field is a separate valid request header block. */
    for(unsigned n=1;n<strlen(valid);++n)
        assert(!dihos_http_validate_request_headers(valid,n)==(valid[n-1]=='\n'));
    const char *bad[]={"Host: other\r\n","hOsT: other\r\n","Transfer-Encoding: chunked\r\n",
        "Proxy-Connection: keep-alive\r\n","Expect: 100-continue\r\n","Accept-Encoding: gzip\r\n",
        "X: a\r\nx: b\r\n","X: a\r\n\r\n","X: a\nInjected: yes\r\n","X: a\rX: b\r\n",
        "X: a\177b\r\n","X: a\001b\r\n"," X: folded\r\n",": empty\r\n","X Bad: a\r\n"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i)assert(dihos_http_validate_request_headers(bad[i],(uint32_t)strlen(bad[i]))<0);
    char maximum[DIHOS_HTTP_REQUEST_HEADERS_MAX];memset(maximum,'a',sizeof(maximum));maximum[0]='X';maximum[1]=':';
    maximum[sizeof(maximum)-2]='\r';maximum[sizeof(maximum)-1]='\n';
    assert(!dihos_http_validate_request_headers(maximum,sizeof(maximum)));
    assert(dihos_http_validate_request_headers(maximum,sizeof(maximum)+1)==-22);
    const char withNul[]="X: a\0b\r\n";
    assert(dihos_http_validate_request_headers(withNul,sizeof(withNul)-1)==-22);
    /* Deterministic malformed byte corpus under ASan/UBSan. Use an exact heap
     * allocation per input so reads beyond a truncated block are detected. */
    unsigned random=0x12bc42;
    for(unsigned n=0;n<10000;++n){
        random=random*1664525u+1013904223u;
        unsigned size=random%2049;
        char *input=malloc(size?size:1);assert(input);
        for(unsigned i=0;i<size;++i){random=random*1664525u+1013904223u;input[i]=(char)(random>>24);}
        int rc=dihos_http_validate_request_headers(input,size);
        if(!rc)(void)dihos_http_request_has_header(input,size,"accept");
        free(input);
    }
    puts("PASS: bounded request fields, truncation, reserved headers, duplicate names, exact limit and 10000 malformed blocks");
}
