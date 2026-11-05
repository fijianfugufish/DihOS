#pragma once
#include <stdint.h>
#include <cstring>
namespace DihosNavigation {
struct BrowserRect {int x,y,w,h;};
// The paint invalidation may be conservative (for example a click forcing a
// full paint). Upload only the bounding box of pixels that actually changed.
// Callers supply a clipped rectangle and two complete, equal-sized surfaces.
inline BrowserRect browserChangedRect(const uint32_t* pixels,const uint32_t* previous,unsigned stride,BrowserRect paint){
    int left=paint.x+paint.w,top=paint.y+paint.h,right=paint.x,bottom=paint.y;
    for(int y=paint.y;y<paint.y+paint.h;++y){
        const auto* row=pixels+(uint64_t)y*stride;const auto* old=previous+(uint64_t)y*stride;
        if(!std::memcmp(row+paint.x,old+paint.x,(size_t)paint.w*4))continue;
        int l=paint.x,r=paint.x+paint.w;
        while(l<r && row[l]==old[l])++l;
        while(r>l && row[r-1]==old[r-1])--r;
        if(l<left)left=l;if(r>right)right=r;if(y<top)top=y;bottom=y+1;
    }
    return right>left?BrowserRect{left,top,right-left,bottom-top}:BrowserRect{};
}
struct BrowserRepaint {
    int width{},height{};BrowserRect rect{};bool pending{};
    void resize(int w,int h){width=w;height=h;full();}
    void full(){rect={0,0,width,height};pending=width>0&&height>0;}
    void add(int x,int y,int w,int h){
        if(w<=0||h<=0)return;
        int64_t l=x<0?0:x,t=y<0?0:y,r=(int64_t)x+w,b=(int64_t)y+h;
        if(r>width)r=width;if(b>height)b=height;if(r<=l||b<=t)return;
        if(pending){if(l>rect.x)l=rect.x;if(t>rect.y)t=rect.y;if(r<rect.x+rect.w)r=rect.x+rect.w;if(b<rect.y+rect.h)b=rect.y+rect.h;}
        rect={(int)l,(int)t,(int)(r-l),(int)(b-t)};pending=true;
    }
    BrowserRect take(){auto result=rect;pending=false;rect={};return result;}
};
struct BrowserWheel {
    int pixels{};
    void add(int ticks){int64_t value=(int64_t)pixels+(int64_t)ticks*40;pixels=value>32000?32000:value< -32000?-32000:(int)value;}
    int take(){int step=pixels>80?80:pixels< -80?-80:pixels;pixels-=step;return step;}
};
}
