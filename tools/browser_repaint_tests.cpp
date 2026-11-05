#include "webcore_dihos/browser_repaint.h"
#include <cassert>
#include <climits>
#include <cstdio>
using namespace DihosNavigation;
int main(){
 BrowserRepaint r;r.resize(640,480);auto a=r.take();assert(a.w==640&&a.h==480&&!r.pending);
 r.add(-5,-6,20,30);r.add(620,470,INT_MAX,INT_MAX);a=r.take();assert(a.x==0&&a.y==0&&a.w==640&&a.h==480);
 r.add(INT_MAX,0,INT_MAX,10);r.add(0,0,-1,3);assert(!r.pending);
 r.add(20,30,10,20);r.add(25,35,20,30);a=r.take();assert(a.x==20&&a.y==30&&a.w==25&&a.h==35);
 BrowserWheel w;w.add(-16);int sum=0,n=0;while(w.pixels){int d=w.take();assert(d>=-80&&d<=80);sum+=d;++n;}assert(sum==-640&&n==8);
 w.add(INT_MAX);assert(w.pixels==32000);w.add(INT_MIN);assert(w.pixels==-32000);
 w.pixels=0;w.add(2);w.add(-1);assert(w.take()==40&&!w.pixels);
 uint32_t old[80]={},pixels[80]={};
 assert(!browserChangedRect(pixels,old,10,{0,0,10,8}).w);
 pixels[23]=1;pixels[56]=2;pixels[79]=3;
 auto changed=browserChangedRect(pixels,old,10,{0,0,10,8});assert(changed.x==3&&changed.y==2&&changed.w==7&&changed.h==6);
 changed=browserChangedRect(pixels,old,10,{2,1,5,5});assert(changed.x==3&&changed.y==2&&changed.w==4&&changed.h==4);
 changed=browserChangedRect(pixels,old,10,{0,0,2,8});assert(!changed.w);
 for(auto& p:pixels)p=1;changed=browserChangedRect(pixels,old,10,{0,0,10,8});assert(changed.x==0&&changed.y==0&&changed.w==10&&changed.h==8);
 puts("PASS: clipped and merged damage; bounded wheel steps preserve distance, reversal and saturation");
 puts("PASS: changed-pixel bounds handle conservative full paints, clipped rows, unchanged surfaces and distant changes");
}
