/* Host socket harness for the actual freestanding kernel TLS engine. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include "net/knet_tls.h"
#include "net/http_stream.h"
static uint64_t pages_live;
static int entropy_failure;
void* pmem_alloc_pages(uint64_t n){void* p=0;if(posix_memalign(&p,4096,n*4096))return 0;pages_live+=n;return p;}
void pmem_free_pages(void* p,uint64_t n){pages_live-=n;free(p);}
void knet_link_checkpoint(void){}
uint64_t dihos_time_monotonic_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
int dihos_time_utc(uint64_t* out){*out=(uint64_t)time(0);return 0;}
int dihos_entropy_fill(void* p,size_t bytes){if(entropy_failure)return -1;FILE* f=fopen("/dev/urandom","rb");if(!f)return -1;size_t n=fread(p,1,bytes,f);fclose(f);return n==bytes?0:-1;}
int main(int argc,char** argv){
    const char* host=argc>1?argv[1]:"www.google.com";
    const char* port=argc>2?argv[2]:"443";
    const char* path=argc>3 && argv[3][0]=='/'?argv[3]:"/";
    if(argc>3 && !strcmp(argv[3],"--no-entropy"))entropy_failure=1;
    struct addrinfo hints={0},*addresses=0;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
    if(getaddrinfo(host,port,&hints,&addresses))return 2;
    int fd=socket(addresses->ai_family,addresses->ai_socktype,addresses->ai_protocol);
    struct timeval timeout={20,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    if(connect(fd,addresses->ai_addr,addresses->ai_addrlen)){perror("connect");return 2;}freeaddrinfo(addresses);
    uint64_t started=dihos_time_monotonic_ms();int rc=knet_tls_begin(host),ready=0;unsigned request_at=0,raw=0;
    char request[SACX_HTTP_URL_MAX+1025];unsigned length=snprintf(request,sizeof(request),"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: DihOS/0.3 Dihscover WebKit\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",path,host);
    if(length>=sizeof(request))return 2;
    uint8_t* body=malloc(8*1024*1024);http_stream response;http_stream_init(&response,body,8*1024*1024);
    while(!rc && !response.complete && dihos_time_monotonic_ms()-started<25000){
        int progress=0;
        if(!ready){int n=knet_tls_handshake();if(n<0){rc=-1;break;}if(!n){ready=1;progress=1;}}
        if(ready && request_at<length){int n=knet_tls_write(request+request_at,length-request_at);if(n<0){rc=-1;break;}if(n){request_at+=n;progress=1;}}
        if(ready && request_at==length){uint8_t bytes[4096];int n=knet_tls_read(bytes,sizeof(bytes));
            if(n==-2){rc=http_stream_finish(&response);break;}if(n<0){rc=-1;break;}
            if(n){raw+=n;if(http_stream_feed(&response,bytes,n)){rc=-1;break;}progress=1;}}
        uint32_t bytes;const uint8_t* output=knet_tls_out(&bytes);
        if(bytes){if(bytes>1300)bytes=1300;int n=(int)send(fd,output,bytes,0);if(n<=0){rc=-1;break;}knet_tls_out_ack(n);progress=1;}
        if(progress || knet_tls_input_pending())continue;
        uint8_t input[1460];int n=(int)recv(fd,input,sizeof(input),0);if(n<=0 || knet_tls_feed(input,n)){rc=-1;break;}
    }
    int ok=!rc && response.complete && knet_tls_authenticated() && request_at==length;
    printf("host=%s protocol=%s authenticated=%d verify=0x%x error=%d raw=%u body=%u complete=%u status=%u request_bytes=%u elapsed_ms=%llu pages=%llu\n",host,knet_tls_version(),knet_tls_authenticated(),knet_tls_verify_flags(),knet_tls_last_error(),raw,response.available,response.complete,response.http_status,request_at,(unsigned long long)(dihos_time_monotonic_ms()-started),(unsigned long long)knet_tls_memory_pages());
    printf("framing: length_declared=%u expected=%u chunked=%u parser_failed=%u error=%s\n",response.has_length,response.expected,response.chunked,response.failed,response.error);
    printf("redirect: %s\n",response.location);
    knet_tls_end();printf("retained_root_service_pages=%llu total_pages=%llu\n",(unsigned long long)knet_tls_memory_pages(),(unsigned long long)pages_live);
    close(fd);free(body);return ok?0:1;
}
