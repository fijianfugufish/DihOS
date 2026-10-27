#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
static struct {uint32_t ip;uint8_t mac[6];} g_knet={0xac100102,{0,0x15,0x5d,0,2,3}};
static std::vector<uint8_t> incoming,sent;
static int knet_link_recv_frame(void *out,uint32_t capacity,uint32_t *bytes)
{
    assert(capacity>=incoming.size());memcpy(out,incoming.data(),incoming.size());
    *bytes=(uint32_t)incoming.size();return 0;
}
static int knet_link_send_frame(const void *p,uint32_t n)
{sent.assign((const uint8_t*)p,(const uint8_t*)p+n);return 0;}
#include "../kernel/net/knet_receive.inc"
int main()
{
    uint8_t rx[1600];uint32_t bytes;
    incoming.resize(42);const uint8_t peer[6]={2,3,4,5,6,7};
    memcpy(incoming.data()+22,peer,6);
    incoming[12]=8;incoming[13]=6;incoming[15]=1;incoming[16]=8;
    incoming[18]=6;incoming[19]=4;incoming[21]=1;
    incoming[28]=172;incoming[29]=16;incoming[30]=1;incoming[31]=1;
    incoming[38]=172;incoming[39]=16;incoming[40]=1;incoming[41]=2;
    assert(knet_receive_frame(rx,sizeof rx,&bytes)==-1&&bytes==0);
    assert(sent.size()==60&&sent[21]==2&&!memcmp(sent.data(),peer,6));
    assert(!memcmp(sent.data()+22,g_knet.mac,6));
    assert(!memcmp(sent.data()+28,incoming.data()+38,4));
    assert(!memcmp(sent.data()+32,peer,6));
    assert(!memcmp(sent.data()+38,incoming.data()+28,4));
    for(unsigned n=42;n<60;++n)assert(!sent[n]);
    sent.clear();incoming[41]=99;
    assert(!knet_receive_frame(rx,sizeof rx,&bytes)&&bytes==42&&sent.empty());
    incoming[41]=2;incoming[21]=2;
    assert(!knet_receive_frame(rx,sizeof rx,&bytes)&&sent.empty());
    incoming[21]=1;incoming.resize(41);
    assert(!knet_receive_frame(rx,sizeof rx,&bytes)&&bytes==41&&sent.empty());
    incoming.resize(100);incoming[13]=0; /* subsequent IPv4 DNS/TCP data is intact */
    assert(!knet_receive_frame(rx,sizeof rx,&bytes)&&bytes==100&&sent.empty());
    assert(!memcmp(rx,incoming.data(),100));
    puts("PASS: active receive answers guest ARP, preserves IPv4 data, ignores unrelated/truncated ARP and pads replies");
}
