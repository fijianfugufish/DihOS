/* Inlined into the transport owner: TCP state stays on its retained stack. */
        __atomic_store_n(&g_fetch_tls_verified,0,__ATOMIC_RELEASE);
        g_fetch_phase="TLS handshake";
        uint64_t utc;
        if(dihos_time_utc(&utc) || !utc){terminal_error("TLS clock unavailable");return -1;}
        if(knet_tls_begin(host)){
            int error=knet_tls_last_error();knet_record_tls_error(knet_tls_error_description(),error<0?(uint32_t)-(int64_t)error:(uint32_t)error);
            KNET_SEND_TCP(tx_seq,rx_seq,tcp_rst|tcp_ack,0,0u,0u);return -1;
        }
        uint32_t request_off=0;
        uint64_t tls_deadline=knet_now_ms()+30000u;
        uint8_t ready=0,peer_fin=0;
        while(!knet_fetch_cancelled() && knet_now_ms()<tls_deadline){
            knet_link_checkpoint();
            uint8_t progress=0;
            if(!ready){
                int result=knet_tls_handshake();
                if(result<0)goto modern_tls_error;
                if(!result){
                    if(!knet_tls_authenticated() || knet_tls_verify_flags()){terminal_error("TLS peer authentication did not complete");return -1;}
                    __atomic_store_n(&g_fetch_tls_verified,1,__ATOMIC_RELEASE);
                    ready=1;progress=1;g_fetch_phase="HTTPS response";
                    terminal_print_inline("net: authenticated ");terminal_print(knet_tls_version());
                }
            }
            if(ready && request_off<request_len){
                int result=knet_tls_write(request+request_off,request_len-request_off);
                if(result<0)goto modern_tls_error;
                if(result>0){request_off+=(uint32_t)result;progress=1;}
            }
            if(ready && request_off==request_len){
                uint8_t plaintext[4096];int result=knet_tls_read(plaintext,sizeof(plaintext));
                if(result==-2){KNET_SEND_TCP(tx_seq,rx_seq,tcp_fin|tcp_ack,0,0u,0u);return 0;}
                if(result<0)goto modern_tls_error;
                if(result>0){
                    if(!knet_tls_authenticated() || !knet_usb_tls_verified()){terminal_error("Unauthenticated HTTPS data rejected");return -1;}
                    uint32_t take=(uint32_t)result;if(take>max_bytes-printed)take=max_bytes-printed;
                    knet_response_emit(plaintext,take);printed+=take;progress=1;tls_deadline=knet_now_ms()+30000u;
                    if(printed>=max_bytes){if(g_fetch_output)g_fetch_truncated=1;KNET_SEND_TCP(tx_seq,rx_seq,tcp_rst|tcp_ack,0,0u,0u);return 0;}
                }
            }
            uint32_t output_bytes;const uint8_t* output=knet_tls_out(&output_bytes);
            if(output_bytes){
                if(output_bytes>1300u)output_bytes=1300u;
                KNET_SEND_TCP(tx_seq,rx_seq,tcp_psh|tcp_ack,output,output_bytes,0u);
                tx_seq+=output_bytes;knet_tls_out_ack(output_bytes);progress=1;tls_deadline=knet_now_ms()+30000u;
            }
            if(progress || knet_tls_input_pending())continue;
            if(peer_fin){terminal_error("TLS peer closed TCP before a complete authenticated response");return -1;}
            tcp_packet packet;uint8_t ok;KNET_PARSE_TCP(packet,ok);if(!ok)continue;
            if(packet.flags&tcp_rst){terminal_error("TLS TCP connection reset");return -1;}
            uint32_t skip=dihos_tcp_receive_skip(rx_seq,packet.seq,packet.payload_len);
            if(packet.payload_len && packet.seq!=rx_seq)++g_fetch_noncontiguous;
            if(packet.payload_len && skip<packet.payload_len){
                uint32_t n=packet.payload_len-skip;
                if(knet_tls_feed(packet.payload+skip,n)){terminal_error("TLS receive buffer limit exceeded");return -1;}
                rx_seq+=n;tls_deadline=knet_now_ms()+30000u;
                KNET_SEND_TCP(tx_seq,rx_seq,tcp_ack,0,0u,0u);
            }else if(packet.payload_len)KNET_SEND_TCP(tx_seq,rx_seq,tcp_ack,0,0u,0u);
            if((packet.flags&tcp_fin) && dihos_tcp_receive_fin(rx_seq,packet.seq,packet.payload_len)){
                ++rx_seq;peer_fin=1;KNET_SEND_TCP(tx_seq,rx_seq,tcp_ack,0,0u,0u);
            }
        }
        /* The framing sink can finish before TLS/TCP close. A completed framed
         * response is authenticated; cancellation and malformed framing are
         * still classified by the worker, which owns the sink and request. */
        if(g_stream_stopped){KNET_SEND_TCP(tx_seq,rx_seq,tcp_fin|tcp_ack,0,0u,0u);return 0;}
        knet_record_timeout();KNET_SEND_TCP(tx_seq,rx_seq,tcp_rst|tcp_ack,0,0u,0u);return -1;
modern_tls_error:;
        int error=knet_tls_last_error();
        knet_record_tls_error(knet_tls_error_description(),error<0?(uint32_t)-(int64_t)error:(uint32_t)error);
        KNET_SEND_TCP(tx_seq,rx_seq,tcp_rst|tcp_ack,0,0u,0u);return -1;
