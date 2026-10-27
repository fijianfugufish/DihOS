#include "net/http_stream.h"
enum {HEADER,IDENTITY,CHUNK_SIZE,CHUNK_DATA,CHUNK_CR,CHUNK_LF,TRAILER,DONE};
static int fail(http_stream *s,const char *message) {
    s->failed=1;uint32_t n=0;while(message[n]&&n+1<sizeof(s->error)){s->error[n]=message[n];++n;}s->error[n]=0;return -1;
}
static char lower(char c){return c>='A'&&c<='Z' ? c+32:c;}
static int equal(const char *a,uint32_t n,const char *b) {
    uint32_t i=0;for(;i<n&&b[i];++i)if(lower(a[i])!=b[i])return 0;return i==n&&!b[i];
}
static void copy(char *out,uint32_t cap,const char *in,uint32_t n) {
    uint32_t i=0;while(i<n&&i+1<cap){out[i]=in[i];++i;}out[i]=0;
}
static int decimal(const char *p,uint32_t n,uint32_t *out) {
    uint32_t v=0;if(!n)return -1;for(uint32_t i=0;i<n;++i){uint32_t d=(uint8_t)p[i]-'0';if(d>9||v>(0xffffffffu-d)/10)return -1;v=v*10+d;}*out=v;return 0;
}
void http_stream_init(http_stream *s,uint8_t *body,uint32_t capacity) {
    *s=(http_stream){0};s->body=body;s->capacity=capacity;
}
static int parse_headers(http_stream *s) {
    const char *p=s->headers;uint32_t n=s->header_bytes;
    if(n<12||p[0]!='H'||p[1]!='T'||p[2]!='T'||p[3]!='P'||p[4]!='/'||p[5]!='1'||p[6]!='.'||
       (p[7]!='0'&&p[7]!='1')||p[8]!=' ')return fail(s,"invalid HTTP status line");
    if(decimal(p+9,3,&s->http_status)||s->http_status<100)return fail(s,"invalid HTTP status code");
    uint32_t offset=0;while(offset+1<n&&!(p[offset]=='\r'&&p[offset+1]=='\n'))++offset;
    offset+=2;
    while(offset+2<n) {
        if(p[offset]=='\r'&&p[offset+1]=='\n')break;
        uint32_t end=offset;while(end+1<n&&!(p[end]=='\r'&&p[end+1]=='\n'))++end;
        uint32_t colon=offset;while(colon<end&&p[colon]!=':')++colon;
        if(colon==end||colon==offset)return fail(s,"invalid HTTP header");
        uint32_t value=colon+1;while(value<end&&(p[value]==' '||p[value]=='\t'))++value;
        uint32_t tail=end;while(tail>value&&(p[tail-1]==' '||p[tail-1]=='\t'))--tail;
        if(equal(p+offset,colon-offset,"content-length")) {
            uint32_t length;if(decimal(p+value,tail-value,&length))return fail(s,"invalid Content-Length");
            if(s->has_length&&s->expected!=length)return fail(s,"conflicting Content-Length");
            s->has_length=1;s->expected=length;
        } else if(equal(p+offset,colon-offset,"transfer-encoding")) {
            if(!equal(p+value,tail-value,"chunked")||s->chunked)return fail(s,"unsupported Transfer-Encoding");s->chunked=1;
        } else if(equal(p+offset,colon-offset,"content-encoding")) {
            if(!equal(p+value,tail-value,"identity"))return fail(s,"server ignored identity Content-Encoding");
        } else if(equal(p+offset,colon-offset,"content-type")) {
            copy(s->content_type,sizeof(s->content_type),p+value,tail-value);
        } else if(equal(p+offset,colon-offset,"location")) {
            if(tail-value>=sizeof(s->location))return fail(s,"redirect URL too long");copy(s->location,sizeof(s->location),p+value,tail-value);
        }
        offset=end+2;
    }
    if(s->http_status>=100&&s->http_status<200) {
        if(s->http_status==101)return fail(s,"HTTP protocol upgrade unsupported");
        s->header_bytes=0;s->has_length=s->chunked=0;s->expected=0;s->content_type[0]=s->location[0]=0;return 0;
    }
    if(s->chunked&&s->has_length)return fail(s,"ambiguous HTTP body framing");
    s->headers_ready=1;s->remaining=s->expected;s->state=s->chunked ? CHUNK_SIZE:IDENTITY;
    if(s->http_status==204||s->http_status==304||(s->has_length&&!s->expected))s->state=DONE;
    s->complete=s->state==DONE;return 0;
}
int http_stream_feed(http_stream *s,const uint8_t *bytes,uint32_t length) {
    if(!s||!s->body||(!bytes&&length)||s->failed)return -1;
    for(uint32_t i=0;i<length;++i) {
        uint8_t c=bytes[i];
        if(s->state==HEADER) {
            if(s->header_bytes==sizeof(s->headers))return fail(s,"HTTP headers exceed 16 KiB");
            s->headers[s->header_bytes++]=(char)c;
            uint32_t n=s->header_bytes;
            if(n>=4&&s->headers[n-4]=='\r'&&s->headers[n-3]=='\n'&&s->headers[n-2]=='\r'&&s->headers[n-1]=='\n')
                if(parse_headers(s))return -1;
        } else if(s->state==IDENTITY||s->state==CHUNK_DATA) {
            if(s->available<s->capacity)s->body[s->available++]=c;else s->truncated=1;
            if(s->state==CHUNK_DATA){if(!--s->remaining)s->state=CHUNK_CR;}
            else if(s->has_length){if(s->remaining)s->remaining--;if(!s->remaining){s->state=DONE;s->complete=1;}}
        } else if(s->state==CHUNK_SIZE||s->state==TRAILER) {
            if(s->line_bytes==sizeof(s->line))return fail(s,"HTTP chunk/trailer line too long");
            s->line[s->line_bytes++]=(char)c;
            if(c=='\n') {
                uint32_t n=s->line_bytes;if(n<2||s->line[n-2]!='\r')return fail(s,"invalid HTTP chunk line ending");
                if(s->state==TRAILER){if(n==2){s->state=DONE;s->complete=1;}}
                else {
                    uint32_t size=0,j=0;
                    for(;j<n-2&&s->line[j]!=';';++j){char ch=lower(s->line[j]);uint32_t digit=ch>='0'&&ch<='9'?ch-'0':ch>='a'&&ch<='f'?ch-'a'+10:99;
                        if(digit>15||size>(0xffffffffu-digit)/16)return fail(s,"invalid HTTP chunk size");size=size*16+digit;}
                    if(!j)return fail(s,"empty HTTP chunk size");s->remaining=size;s->state=size ? CHUNK_DATA:TRAILER;
                }
                s->line_bytes=0;
            }
        } else if(s->state==CHUNK_CR) {if(c!='\r')return fail(s,"missing chunk terminator");s->state=CHUNK_LF;}
        else if(s->state==CHUNK_LF) {if(c!='\n')return fail(s,"missing chunk terminator");s->state=CHUNK_SIZE;}
        else if(s->state==DONE) return fail(s,"unexpected bytes after HTTP body");
    }
    return 0;
}
int http_stream_finish(http_stream *s) {
    if(!s||s->failed)return -1;
    if(s->state==IDENTITY&&!s->has_length){s->state=DONE;s->complete=1;}
    return s->complete ? 0:fail(s,"incomplete HTTP response body");
}
