static int sacx_loader_checkpoint(void)
{
    if(!G_launch_active||!kfiber_is_current(&G_launch_fiber)) {kbusy_pump();return 0;}
    if(G_launch_active->launch_cancelled||dihos_time_monotonic_ms()>=G_launch_deadline)return -1;
    if(dihos_time_monotonic_ms()>=G_launch_slice_end)kfiber_yield();
    return G_launch_active->launch_cancelled||dihos_time_monotonic_ms()>=G_launch_deadline?-1:0;
}

