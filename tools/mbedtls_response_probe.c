/* Host-only interoperability experiment. Not linked into DihOS. */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "psa/crypto.h"
#include "net/http_stream.h"
int main(int argc,char** argv){
    const char* host=argc>1?argv[1]:"www.google.com";
    mbedtls_ssl_context ssl;mbedtls_ssl_config config;mbedtls_x509_crt roots;
    mbedtls_ctr_drbg_context rng;mbedtls_entropy_context entropy;mbedtls_net_context net;
    mbedtls_ssl_init(&ssl);mbedtls_ssl_config_init(&config);mbedtls_x509_crt_init(&roots);
    mbedtls_ctr_drbg_init(&rng);mbedtls_entropy_init(&entropy);mbedtls_net_init(&net);
    int rc=0;uint32_t trust=0;unsigned total=0;uint8_t* body=malloc(8*1024*1024);http_stream response;http_stream_init(&response,body,8*1024*1024);
    if(psa_crypto_init()!=PSA_SUCCESS)return 2;
    if((rc=mbedtls_x509_crt_parse_file(&roots,"third_party/ca-certificates/cacert-2026-10-20.pem")))goto done;
    if((rc=mbedtls_ctr_drbg_seed(&rng,mbedtls_entropy_func,&entropy,(const unsigned char*)"DihOS host TLS experiment",25)))goto done;
    if((rc=mbedtls_ssl_config_defaults(&config,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT)))goto done;
    mbedtls_ssl_conf_authmode(&config,MBEDTLS_SSL_VERIFY_REQUIRED);mbedtls_ssl_conf_ca_chain(&config,&roots,0);mbedtls_ssl_conf_rng(&config,mbedtls_ctr_drbg_random,&rng);
    if(argc>2){mbedtls_ssl_protocol_version version=!strcmp(argv[2],"12")?MBEDTLS_SSL_VERSION_TLS1_2:MBEDTLS_SSL_VERSION_TLS1_3;mbedtls_ssl_conf_min_tls_version(&config,version);mbedtls_ssl_conf_max_tls_version(&config,version);}
    if((rc=mbedtls_ssl_setup(&ssl,&config)) || (rc=mbedtls_ssl_set_hostname(&ssl,host)))goto done;
    if((rc=mbedtls_net_connect(&net,host,"443",MBEDTLS_NET_PROTO_TCP)))goto done;
    struct timeval timeout={20,0};setsockopt(net.fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(net.fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    mbedtls_ssl_set_bio(&ssl,&net,mbedtls_net_send,mbedtls_net_recv,0);
    while((rc=mbedtls_ssl_handshake(&ssl))==MBEDTLS_ERR_SSL_WANT_READ || rc==MBEDTLS_ERR_SSL_WANT_WRITE){}
    if(rc || (trust=mbedtls_ssl_get_verify_result(&ssl)))goto done;
    char request[2048];int length=snprintf(request,sizeof(request),"GET / HTTP/1.1\r\nHost: %s\r\nUser-Agent: DihOS/0.3 Dihscover WebKit\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",host);
    for(int at=0;at<length;){rc=mbedtls_ssl_write(&ssl,(unsigned char*)request+at,length-at);if(rc<=0)goto done;at+=rc;}
    unsigned char bytes[4096];
    while(!response.complete && !response.failed){rc=mbedtls_ssl_read(&ssl,bytes,sizeof(bytes));if(rc==MBEDTLS_ERR_SSL_WANT_READ || rc==MBEDTLS_ERR_SSL_WANT_WRITE)continue;if(rc<=0)break;total+=rc;if(http_stream_feed(&response,bytes,rc))break;}
    if(!response.complete && !response.failed && rc==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)http_stream_finish(&response);
done:;
    int result=response.complete?0:rc;char error[192];
    if(!result)strcpy(error,"OK");else mbedtls_strerror(result,error,sizeof(error));
    printf("host=%s protocol=%s cipher=%s verify=0x%x raw=%u body=%u complete=%u result=%d detail=%s framing=%s\n",host,mbedtls_ssl_get_version(&ssl),mbedtls_ssl_get_ciphersuite(&ssl),trust,total,response.available,response.complete,result,error,response.error);
    int ok=response.complete && !trust;
    mbedtls_net_free(&net);mbedtls_ssl_free(&ssl);mbedtls_ssl_config_free(&config);mbedtls_x509_crt_free(&roots);mbedtls_ctr_drbg_free(&rng);mbedtls_entropy_free(&entropy);free(body);return ok?0:1;
}
