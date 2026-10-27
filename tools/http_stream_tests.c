#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "net/http_stream.h"
static void split_response(const char *wire,const char *body)
{
    for(unsigned split=1;split<=strlen(wire);++split) {
        http_stream stream;uint8_t output[128]={0};http_stream_init(&stream,output,sizeof(output));
        for(unsigned offset=0;offset<strlen(wire);) {
            unsigned n=(unsigned)strlen(wire)-offset;if(n>split)n=split;
            assert(!http_stream_feed(&stream,(const uint8_t*)wire+offset,n));offset+=n;
        }
        assert(!http_stream_finish(&stream)&&stream.complete&&stream.headers_ready);
        assert(stream.available==strlen(body)&&!memcmp(output,body,stream.available));
    }
}
static void reject(const char *wire)
{
    http_stream stream;uint8_t output[128];http_stream_init(&stream,output,sizeof(output));
    int result=http_stream_feed(&stream,(const uint8_t*)wire,(uint32_t)strlen(wire));
    assert(result<0||http_stream_finish(&stream)<0);assert(stream.failed&&stream.error[0]);
}
int main(void)
{
    split_response("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Type: text/html; charset=utf-8\r\n\r\nhello","hello");
    split_response("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2;ext=yes\r\nhe\r\n3\r\nllo\r\n0\r\nETag: test\r\n\r\n","hello");
    split_response("HTTP/1.0 200 OK\r\n\r\nhello","hello");
    split_response("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello","hello");
    split_response("HTTP/1.1 204 No Content\r\n\r\n","");
    reject("HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\nhello");
    reject("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\na");
    reject("HTTP/1.1 200 OK\r\nContent-Length: 42949672960\r\n\r\n");
    reject("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n");
    reject("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n");
    reject("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nxyz\r\n");
    reject("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\naXX");
    http_stream stream;uint8_t output[4];http_stream_init(&stream,output,sizeof(output));
    const char *header="HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n";
    assert(!http_stream_feed(&stream,(const uint8_t*)header,(uint32_t)strlen(header)));
    assert(stream.headers_ready&&!stream.complete&&stream.expected==5&&!stream.available);
    assert(!http_stream_feed(&stream,(const uint8_t*)"he",2)&&stream.available==2&&!stream.complete&&stream.expected==5);
    assert(!memcmp(output,"he",2)); /* stable readable prefix before completion */
    assert(!http_stream_feed(&stream,(const uint8_t*)"llo",3)&&stream.truncated&&stream.available==4);
    assert(!http_stream_finish(&stream)&&!memcmp(output,"hell",4));
    puts("PASS: every HTTP split size, streamed stable prefixes, chunk extensions/trailers, informational responses, close-delimited bodies, malformed framing and bounded output");
}
