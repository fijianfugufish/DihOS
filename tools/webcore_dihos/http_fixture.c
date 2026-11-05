#include "sacx_user_api.h"
#include "sacx_user_network.h"
#include "sacx_user_http_request.h"
#ifndef HTTP_FIXTURE_URL
#define HTTP_FIXTURE_URL "http://example.com/"
#endif
static const char url[]=HTTP_FIXTURE_URL;
static unsigned char data[4096];
static sacx_user_http_info info;
static void check(int ok,const char *message){if(!ok)sacx_user_exit(167,message);}
__attribute__((noreturn)) void _start(void){
    sacx_user_info abi;check(!sacx_user_call(SACX_USER_QUERY,(uintptr_t)&abi,sizeof(abi),0),"HTTP: ABI query failed");
    int64_t id=sacx_user_http_start_manual(url,sizeof(url)-1,30000);
    if(!(abi.capabilities&SACX_USER_CAP_NETWORK)){
        check(id==-1,"HTTP: missing grant was bypassed");
        check(sacx_user_http_start_request(url,sizeof(url)-1,0,0,30000)==-1,"HTTP: request descriptor bypassed network grant");
        sacx_user_exit(105,"SACX HTTP: network denied without explicit launch grant OK");
    }
    check(id>0,"HTTP: request admission failed");
    check(!sacx_user_http_close(id),"HTTP: close legacy manual request");
    check(!!(abi.operations&(1ull<<SACX_USER_HTTP_REQUEST)),"HTTP: request descriptor ABI missing");
    check(sacx_user_call(SACX_USER_HTTP_REQUEST,0,sizeof(sacx_user_http_request),0)==-14,"HTTP: protected descriptor accepted");
    sacx_user_http_request invalid={(uintptr_t)url,0,sizeof(url)-1,1,30000,0};
    check(sacx_user_call(SACX_USER_HTTP_REQUEST,(uintptr_t)&invalid,sizeof(invalid),0)==-14,"HTTP: protected request fields accepted");
    char fields[]="X-DihOS-Test: retained\r\nAccept: text/html\r\n";
    id=sacx_user_http_start_request(url,sizeof(url)-1,fields,sizeof(fields)-1,30000);
    check(id>0,"HTTP: owned request fields admission failed");
    for(unsigned i=0;i<sizeof(fields);++i)fields[i]='x';
    check((abi.operations&((1ull<<31)|(1ull<<32)))==((1ull<<31)|(1ull<<32)),"HTTP: manual/header ABI missing");
    check(sacx_user_http_poll(id,(sacx_user_http_info*)(uintptr_t)&_start)==-14,"HTTP: protected metadata output accepted");
    check(sacx_user_http_read(id,(void*)(uintptr_t)&_start,16)==-14,"HTTP: protected body output accepted");
    check(sacx_user_http_headers(id,(void*)(uintptr_t)&_start,16,0)==-14,"HTTP: protected header output accepted");
    int64_t began=sacx_user_call(SACX_USER_MONOTONIC_MS,0,0,0);unsigned received=0,header_bytes=0;
    while(1){
        check(!sacx_user_http_poll(id,&info),"HTTP: poll failed");
        if(info.http_status && !header_bytes){
            unsigned offset=0;
            for(;;){
                int count=sacx_user_http_headers(id,data,sizeof(data),offset);
                check(count>=0,"HTTP: header read failed");if(!count)break;
                if(!offset)check(count>=8 && data[0]=='H' && data[1]=='T' && data[7]>='0',"HTTP: status header missing");
                offset+=(unsigned)count;check(offset<=SACX_USER_HTTP_HEADERS_MAX,"HTTP: header bound exceeded");
            }
            header_bytes=offset;check(header_bytes>=12,"HTTP: empty header block");
            check(sacx_user_http_headers(id,data,1,0xffffffffu)==-22,"HTTP: invalid header offset accepted");
        }
        int bytes=sacx_user_http_read(id,data,sizeof(data));
        if(bytes>0){received+=(unsigned)bytes;continue;}
        if(!bytes)break;
        if(bytes!=-11){sacx_user_log(info.error);sacx_user_http_close(id);sacx_user_exit(167,"HTTP: transport failed or response exceeded bounded buffer");}
        if(sacx_user_call(SACX_USER_MONOTONIC_MS,0,0,0)-began>45000){sacx_user_http_close(id);sacx_user_exit(167,"HTTP: fixture deadline");}
        sacx_user_call(SACX_USER_SLEEP_MS,10,0,0);
    }
    check(received && header_bytes && info.http_status>=200 && info.http_status<300 && !info.truncated,"HTTP: incomplete or unsuccessful response");
    if(info.tls_unverified)sacx_user_log("HTTP: TLS peer authentication is NOT verified by this transport");
    check(!sacx_user_http_close(id) && sacx_user_http_poll(id,&info)==-9,"HTTP: stale request handle accepted");
    check(sacx_user_http_headers(id,data,1,0)==-9,"HTTP: stale header handle accepted");
    sacx_user_exit(104,"SACX HTTP: asynchronous GET, checked copies and close OK");
}
