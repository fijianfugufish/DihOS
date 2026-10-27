/* Service neighbour discovery while DHCP/DNS/TCP own the receive path too.
 * Keep the reply separate from g_tx: that buffer retains retransmit packets. */
static int knet_receive_frame(void *out,uint32_t capacity,uint32_t *bytes)
{
    int rc=knet_link_recv_frame(out,capacity,bytes);
    if(rc||!bytes||*bytes<42u)return rc;
    const uint8_t *p=(const uint8_t*)out;
    if(!g_knet.ip||p[12]!=8||p[13]!=6||p[14]!=0||p[15]!=1||
       p[16]!=8||p[17]!=0||p[18]!=6||p[19]!=4||p[20]!=0||p[21]!=1||
       p[38]!=(uint8_t)(g_knet.ip>>24)||p[39]!=(uint8_t)(g_knet.ip>>16)||
       p[40]!=(uint8_t)(g_knet.ip>>8)||p[41]!=(uint8_t)g_knet.ip)return rc;
    uint8_t reply[60]={0};
    memcpy(reply,p+22,6);memcpy(reply+6,g_knet.mac,6);
    memcpy(reply+12,p+12,8);reply[21]=2;
    memcpy(reply+22,g_knet.mac,6);memcpy(reply+28,p+38,4);
    memcpy(reply+32,p+22,6);memcpy(reply+38,p+28,4);
    (void)knet_link_send_frame(reply,sizeof reply);
    *bytes=0;
    return -1; /* control frame consumed; the caller's bounded loop continues */
}
