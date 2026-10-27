void dihscover_netsurf_mouse(int32_t x,int32_t y,uint8_t buttons,int32_t wheel)
{
    if(!g_gui.browser)return;if(wheel){clamp_scroll(g_scroll_x,(int64_t)g_scroll_y-(int64_t)wheel*48);damage_all();}
    browser_mouse_state state=BROWSER_MOUSE_HOVER;if((buttons&1u)&&!(g_previous_buttons&1u)){g_mouse_down_x=x;g_mouse_down_y=y;}
    if(buttons&1u)state=(browser_mouse_state)(state|((g_previous_buttons&1u)&&(x!=g_mouse_down_x||y!=g_mouse_down_y)?BROWSER_MOUSE_DRAG_1:BROWSER_MOUSE_PRESS_1));if((g_previous_buttons&1u)&&!(buttons&1u))state=(browser_mouse_state)(state|BROWSER_MOUSE_CLICK_1);
    /* HTML tracking delegates to the same action handler as clicking. Never
     * deliver CLICK through both paths: it navigates twice, aborting the first
     * fetch while the same release event is still being handled. */
    if(state&BROWSER_MOUSE_CLICK_1)
        browser_window_mouse_click(g_gui.browser,state,x+g_scroll_x,y+g_scroll_y);
    else
        browser_window_mouse_track(g_gui.browser,state,x+g_scroll_x,y+g_scroll_y);
    g_previous_buttons=buttons;
}
