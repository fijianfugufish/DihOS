/* Host-only interoperability probe: real BearSSL, production roots, no renderer. */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include "bearssl.h"
#include "net/tls_verify.h"
#include "../kernel/net/tls_roots.inc"
static int read_socket(void *ctx,unsigned char *buffer,size_t n){ssize_t rc=recv(*(int*)ctx,buffer,n,0);return rc<=0?-1:(int)rc;}
static int write_socket(void *ctx,const unsigned char *buffer,size_t n){ssize_t rc=send(*(int*)ctx,buffer,n,0);return rc<=0?-1:(int)rc;}
int main(int argc,char **argv){
    if(argc!=2 && argc!=3)return 2;
    int reject=argc==3 && !strcmp(argv[2],"--reject");
    struct addrinfo hints={0},*list=0;hints.ai_socktype=SOCK_STREAM;hints.ai_family=AF_INET;
    if(getaddrinfo(argv[1],"443",&hints,&list)){puts("DNS failed");return 3;}
    int fd=socket(list->ai_family,list->ai_socktype,list->ai_protocol);struct timeval timeout={15,0};
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    if(connect(fd,list->ai_addr,list->ai_addrlen)){perror("connect");freeaddrinfo(list);return 3;}freeaddrinfo(list);
    br_ssl_client_context client;br_x509_minimal_context x;dihos_tls_verifier verifier;unsigned char io[BR_SSL_BUFSIZE_BIDI],seed[48];
    br_ssl_client_init_full(&client,&x,DIHOS_TLS_ROOTS,DIHOS_TLS_ROOTS_COUNT);
    uint64_t now=(uint64_t)time(0);br_x509_minimal_set_time(&x,719528u+now/86400,now%86400);
    dihos_tls_verifier_init(&verifier,&x.vtable);br_ssl_engine_set_x509(&client.eng,&verifier.vtable);
    br_ssl_engine_set_versions(&client.eng,BR_TLS12,BR_TLS12);br_ssl_engine_set_buffer(&client.eng,io,sizeof(io),1);
    FILE *random=fopen("/dev/urandom","rb");if(!random || fread(seed,1,sizeof(seed),random)!=sizeof(seed))return 4;fclose(random);
    br_ssl_engine_inject_entropy(&client.eng,seed,sizeof(seed));memset(seed,0,sizeof(seed));
    if(!br_ssl_client_reset(&client,argv[1],0))return 4;
    br_sslio_context stream;br_sslio_init(&stream,&client.eng,read_socket,&fd,write_socket,&fd);
    char request[1024],response[1024];int n=snprintf(request,sizeof(request),"GET / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",argv[1]);
    int rc=br_sslio_write_all(&stream,request,n);if(!rc)rc=br_sslio_flush(&stream);if(!rc)rc=br_sslio_read(&stream,response,sizeof(response)-1);
    close(fd);
    if(reject){
        if(rc<=0 && verifier.checked && !verifier.verified && verifier.error){
            printf("PASS: %s certificate rejected before HTTP application data (X509=%u)\n",argv[1],verifier.error);return 0;
        }
        printf("FAIL: expected certificate rejection for %s (TLS=%d X509=%u)\n",argv[1],br_ssl_engine_last_error(&client.eng),verifier.error);return 1;
    }
    if(rc<=0 || !verifier.checked || !verifier.verified){printf("FAIL: %s TLS=%d X509=%u\n",argv[1],br_ssl_engine_last_error(&client.eng),verifier.error);return 1;}
    response[rc]=0;char *end=strchr(response,'\r');if(end)*end=0;
    printf("PASS: %s authenticated TLS 1.2 using embedded Mozilla roots: %s\n",argv[1],response);return 0;
}
