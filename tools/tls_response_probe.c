/* Host diagnostic only: real BearSSL and production HTTP framing, full body.
 * No guest transport, renderer, cookie jar or certificate exceptions. */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <errno.h>
#include "bearssl.h"
#include "net/tls_verify.h"
#include "net/http_stream.h"
#ifdef DIHOS_TLS_FIXTURE
#include "fixtures/tls/test-roots.inc"
#define DIHOS_TLS_ROOTS FIXTURE_TAs
#define DIHOS_TLS_ROOTS_COUNT FIXTURE_TAs_NUM
#define PROBE_PORT "4443"
#define PROBE_NAME "fixture.dihos.test"
#else
#include "../kernel/net/tls_roots.inc"
#define PROBE_PORT "443"
#define PROBE_NAME argv[1]
#endif
static int read_socket(void *ctx,unsigned char *buffer,size_t n){ssize_t rc=recv(*(int*)ctx,buffer,n,0);fprintf(stderr,"TLS recv want=%zu got=%zd errno=%d\n",n,rc,rc<0?errno:0);return rc<=0?-1:(int)rc;}
static int write_socket(void *ctx,const unsigned char *buffer,size_t n){ssize_t rc=send(*(int*)ctx,buffer,n,0);fprintf(stderr,"TLS send want=%zu got=%zd errno=%d\n",n,rc,rc<0?errno:0);return rc<=0?-1:(int)rc;}
int main(int argc,char **argv){
    if(argc!=3 && argc!=4)return 2;
    struct addrinfo hints={0},*list=0;hints.ai_socktype=SOCK_STREAM;hints.ai_family=AF_INET;
    if(getaddrinfo(argv[1],PROBE_PORT,&hints,&list))return 3;
    int fd=socket(list->ai_family,list->ai_socktype,list->ai_protocol);struct timeval timeout={20,0};
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    const char* port=getenv("DIHOS_TLS_TRACE_PORT");
    if(port){struct sockaddr_in local={0};local.sin_family=AF_INET;local.sin_port=htons((uint16_t)atoi(port));int yes=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));if(bind(fd,(struct sockaddr*)&local,sizeof(local))){perror("bind");return 3;}}
    if(connect(fd,list->ai_addr,list->ai_addrlen)){perror("connect");return 3;}
    char address[128];getnameinfo(list->ai_addr,list->ai_addrlen,address,sizeof(address),0,0,NI_NUMERICHOST);fprintf(stderr,"peer=%s\n",address);freeaddrinfo(list);
    br_ssl_client_context client;br_x509_minimal_context x;dihos_tls_verifier verifier;unsigned char io[BR_SSL_BUFSIZE_BIDI],seed[48];
    br_ssl_client_init_full(&client,&x,DIHOS_TLS_ROOTS,DIHOS_TLS_ROOTS_COUNT);
    uint64_t now=(uint64_t)time(0);br_x509_minimal_set_time(&x,719528u+now/86400,now%86400);
    dihos_tls_verifier_init(&verifier,&x.vtable);br_ssl_engine_set_x509(&client.eng,&verifier.vtable);
    br_ssl_engine_set_versions(&client.eng,BR_TLS12,BR_TLS12);br_ssl_engine_set_buffer(&client.eng,io,sizeof(io),1);
    if(strstr(argv[2],"pad"))br_ssl_client_set_min_clienthello_len(&client,512);
    const char* protocols[]={"http/1.1"};
    if(strstr(argv[2],"alpn"))br_ssl_engine_set_protocol_names(&client.eng,protocols,1);
    if(strstr(argv[2],"aes")){static const uint16_t suites[]={BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256};br_ssl_engine_set_suites(&client.eng,suites,2);}
    FILE *random=fopen("/dev/urandom","rb");if(!random || fread(seed,1,sizeof(seed),random)!=sizeof(seed))return 4;fclose(random);
    br_ssl_engine_inject_entropy(&client.eng,seed,sizeof(seed));memset(seed,0,sizeof(seed));
    if(!br_ssl_client_reset(&client,PROBE_NAME,0))return 4;
    br_sslio_context stream;br_sslio_init(&stream,&client.eng,read_socket,&fd,write_socket,&fd);
    const char* ua=!strncmp(argv[2],"browser",7)?"DihOS/0.3 Dihscover WebKit":
        !strcmp(argv[2],"webkit")?"Mozilla/5.0 (DihOS; ARM64) AppleWebKit/622.1 (KHTML, like Gecko) Dihscover/0.3 Safari/622.1":
        !strcmp(argv[2],"wpe")?"Mozilla/5.0 (DihOS; ARM64) AppleWebKit/605.1.15 (KHTML, like Gecko) Dihscover/0.3 Safari/605.1.15":
        !strcmp(argv[2],"current")?"Mozilla/5.0 (DIHOS; Dihscover/0.3) AppleWebKit/537.36 Safari/537.36":
        !strcmp(argv[2],"minimal")?"Mozilla/5.0 (DihOS; ARM64) Dihscover/0.3":
        "Mozilla/5.0 (DIHOS; Dihscover/0.2) AppleWebKit/537.36 Safari/537.36";
    const char* path=getenv("DIHOS_TLS_PROBE_PATH");if(!path)path="/";
    char request[2048];int n=snprintf(request,sizeof(request),"GET %s HTTP/%s\r\nHost: %s\r\nUser-Agent: %s\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\nAccept-Encoding: identity\r\n%sConnection: close\r\n\r\n",path,strstr(argv[2],"http10")?"1.0":"1.1",argv[1],ua,strstr(argv[2],"cl0")?"Content-Length: 0\r\n":"");
    if(br_sslio_write_all(&stream,request,n) || br_sslio_flush(&stream))return 5;
    uint8_t *body=malloc(8*1024*1024);http_stream response;http_stream_init(&response,body,8*1024*1024);
    unsigned chunks=0,total=0;unsigned char bytes[4096];int rc=0;
    while(!response.complete && !response.failed && (rc=br_sslio_read(&stream,bytes,sizeof(bytes)))>0){
        if(!verifier.checked || !verifier.verified)return 6;
        ++chunks;total+=(unsigned)rc;if(http_stream_feed(&response,bytes,(unsigned)rc))break;
    }
    if(!response.complete && !response.failed &&
       !br_ssl_engine_last_error(&client.eng) &&
       (br_ssl_engine_current_state(&client.eng)&BR_SSL_CLOSED))http_stream_finish(&response);
    printf("host=%s profile=%s trusted=%u tls=%d status=%u chunked=%u length_declared=%u expected=%u raw=%u body=%u chunks=%u complete=%u error=%s\n",argv[1],argv[2],verifier.verified,br_ssl_engine_last_error(&client.eng),response.http_status,response.chunked,response.has_length,response.expected,total,response.available,chunks,response.complete,response.error);
    if(argc==4){FILE* file=fopen(argv[3],"wb");if(file){fwrite(body,1,response.available,file);fclose(file);}}
    printf("cipher=0x%04x\n",client.eng.session.cipher_suite);
    close(fd);free(body);return response.complete && verifier.verified?0:1;
}
