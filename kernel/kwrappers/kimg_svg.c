#include <stdint.h>
#include "kwrappers/kimg.h"
#include "memory/pmem.h"
#include "kwrappers/string.h"

#define SVG_MAX_DIM 768u
#define SVG_MAX_POINTS 512u

typedef struct svg_point { int32_t x, y; uint8_t move; } svg_point;

static int svg_space(char c) { return c==' '||c=='\t'||c=='\r'||c=='\n'||c==','; }
static char svg_lower(char c) { return c>='A'&&c<='Z'?(char)(c+32):c; }

static const char *svg_find(const char *p, const char *end, const char *needle)
{
    uint32_t n=0u; while(needle[n])++n;
    for(;p+n<=end;++p){uint32_t i=0u;while(i<n&&svg_lower(p[i])==svg_lower(needle[i]))++i;if(i==n)return p;}
    return 0;
}

static int32_t svg_number(const char **cursor, const char *end, int *ok)
{
    const char *p=*cursor; int sign=1; int32_t whole=0,frac=0,scale=1;
    while(p<end&&svg_space(*p))++p;
    if(p<end&&(*p=='-'||*p=='+')){if(*p=='-')sign=-1;++p;}
    if(p>=end||((*p<'0'||*p>'9')&&*p!='.')){*ok=0;return 0;}
    while(p<end&&*p>='0'&&*p<='9'){if(whole<1000000)whole=whole*10+(*p-'0');++p;}
    if(p<end&&*p=='.'){++p;while(p<end&&*p>='0'&&*p<='9'&&scale<1000){frac=frac*10+(*p-'0');scale*=10;++p;}while(p<end&&*p>='0'&&*p<='9')++p;}
    *cursor=p;*ok=1;return sign*(whole*1024+(frac*1024)/scale);
}

static int svg_attr(const char *tag, const char *end, const char *name, const char **value, const char **value_end)
{
    uint32_t n=0u;while(name[n])++n;
    for(const char *p=tag;p+n<end;++p){
        uint32_t i=0u;while(i<n&&svg_lower(p[i])==svg_lower(name[i]))++i;
        if(i!=n||(p>tag&&!svg_space(p[-1]))||(p+n<end&&!svg_space(p[n])&&p[n]!='='))continue;
        p+=n;while(p<end&&svg_space(*p))++p;if(p>=end||*p!='=')return 0;++p;while(p<end&&svg_space(*p))++p;
        char q=0;if(p<end&&(*p=='\''||*p=='\"'))q=*p++;
        *value=p;while(p<end&&((q&&*p!=q)||(!q&&!svg_space(*p)&&*p!='>')))++p;*value_end=p;return 1;
    }
    return 0;
}

static int32_t svg_attr_number(const char *tag,const char *end,const char *name,int32_t fallback)
{
    const char *v,*ve;if(!svg_attr(tag,end,name,&v,&ve))return fallback;int ok=0;int32_t n=svg_number(&v,ve,&ok);return ok?n:fallback;
}

static uint32_t svg_color(const char *tag,const char *end,const char *name,uint32_t fallback)
{
    const char *v,*ve;if(!svg_attr(tag,end,name,&v,&ve))return fallback;
    if(ve-v==4&&v[0]=='n'&&v[1]=='o'&&v[2]=='n'&&v[3]=='e')return 0u;
    if(v<ve&&*v=='#'){
        ++v;uint32_t value=0u,digits=0u;while(v<ve&&digits<6u){char c=svg_lower(*v++);uint32_t x=c>='0'&&c<='9'?(uint32_t)(c-'0'):c>='a'&&c<='f'?(uint32_t)(c-'a'+10):16u;if(x>15u)break;value=(value<<4)|x;++digits;}
        if(digits==3u){uint32_t r=(value>>8)&15u,g=(value>>4)&15u,b=value&15u;return 0xFF000000u|(r*17u<<16)|(g*17u<<8)|b*17u;}
        if(digits==6u)return 0xFF000000u|value;
    }
    return fallback;
}

static int32_t svg_scale(int32_t value,int32_t origin,int32_t span,uint32_t pixels)
{ return span? (int32_t)(((int64_t)(value-origin)*pixels)/span):0; }

static void svg_pixel(uint32_t *px,uint32_t w,uint32_t h,int32_t x,int32_t y,uint32_t c)
{ if(c&&x>=0&&y>=0&&(uint32_t)x<w&&(uint32_t)y<h)px[(uint32_t)y*w+(uint32_t)x]=c; }

static void svg_line(uint32_t *px,uint32_t w,uint32_t h,svg_point a,svg_point b,uint32_t c)
{
    int32_t dx=b.x>a.x?b.x-a.x:a.x-b.x,sx=a.x<b.x?1:-1,dy=-(b.y>a.y?b.y-a.y:a.y-b.y),sy=a.y<b.y?1:-1,err=dx+dy;
    for(;;){svg_pixel(px,w,h,a.x,a.y,c);if(a.x==b.x&&a.y==b.y)break;int32_t e=err*2;if(e>=dy){err+=dy;a.x+=sx;}if(e<=dx){err+=dx;a.y+=sy;}}
}

static void svg_fill_polygon(uint32_t *px,uint32_t w,uint32_t h,const svg_point *points,uint32_t count,uint32_t c)
{
    int32_t intersections[SVG_MAX_POINTS];if(!c||count<3u)return;
    for(uint32_t y=0u;y<h;++y){uint32_t used=0u;for(uint32_t i=1u;i<count;++i){if(points[i].move)continue;uint32_t j=i-1u;
        int32_t y0=points[j].y,y1=points[i].y;if(((y0<=(int32_t)y)&&(y1>(int32_t)y))||((y1<=(int32_t)y)&&(y0>(int32_t)y))){if(used<SVG_MAX_POINTS)intersections[used++]=points[j].x+(int32_t)(((int64_t)((int32_t)y-y0)*(points[i].x-points[j].x))/(y1-y0));}}
        for(uint32_t i=1u;i<used;++i){int32_t v=intersections[i];uint32_t k=i;while(k&&intersections[k-1u]>v){intersections[k]=intersections[k-1u];--k;}intersections[k]=v;}
        for(uint32_t i=0u;i+1u<used;i+=2u){int32_t a=intersections[i],b=intersections[i+1u];if(a<0)a=0;if(b>(int32_t)w)b=(int32_t)w;for(int32_t x=a;x<b;++x)px[y*w+(uint32_t)x]=c;}
    }
}

static uint32_t svg_path_points(const char *p,const char *end,svg_point *out,uint32_t cap,int32_t vx,int32_t vy,int32_t vw,int32_t vh,uint32_t w,uint32_t h)
{
    uint32_t used=0u;char command=0;int32_t x=0,y=0,start_x=0,start_y=0;
    while(p<end&&used<cap){while(p<end&&svg_space(*p))++p;if(p>=end)break;if((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z'))command=*p++;if(command=='z'||command=='Z'){x=start_x;y=start_y;out[used++]=(svg_point){svg_scale(x,vx,vw,w),svg_scale(y,vy,vh,h),0u};command=0;continue;}
        int relative=command>='a'&&command<='z';char c=svg_lower(command);int32_t nx=x,ny=y;int ok=1;
        if(c=='h'){nx=svg_number(&p,end,&ok);if(relative)nx+=x;}
        else if(c=='v'){ny=svg_number(&p,end,&ok);if(relative)ny+=y;}
        else if(c=='a'){for(int value=0;value<7&&ok;++value){int32_t v=svg_number(&p,end,&ok);if(value==5)nx=v;else if(value==6)ny=v;}if(relative){nx+=x;ny+=y;}}
        else if(c=='c'||c=='s'||c=='q'){
            int32_t c1x=x,c1y=y,c2x=x,c2y=y;if(c=='c'){c1x=svg_number(&p,end,&ok);c1y=svg_number(&p,end,&ok);}c2x=svg_number(&p,end,&ok);c2y=svg_number(&p,end,&ok);nx=svg_number(&p,end,&ok);ny=svg_number(&p,end,&ok);if(relative){c1x+=x;c1y+=y;c2x+=x;c2y+=y;nx+=x;ny+=y;}if(ok){for(int step=1;step<=8&&used<cap;++step){int64_t t=step,u=8-step,den=512;int64_t bx,by;if(c=='q'){den=64;bx=u*u*x+2*u*t*c2x+t*t*nx;by=u*u*y+2*u*t*c2y+t*t*ny;}else{bx=u*u*u*x+3*u*u*t*c1x+3*u*t*t*c2x+t*t*t*nx;by=u*u*u*y+3*u*u*t*c1y+3*u*t*t*c2y+t*t*t*ny;}out[used++]=(svg_point){svg_scale((int32_t)(bx/den),vx,vw,w),svg_scale((int32_t)(by/den),vy,vh,h),0u};}x=nx;y=ny;continue;}}
        else {nx=svg_number(&p,end,&ok);ny=svg_number(&p,end,&ok);if(relative){nx+=x;ny+=y;}}
        if(!ok){if(p<end)++p;continue;}x=nx;y=ny;uint8_t move=(uint8_t)(c=='m');if(move||!used){start_x=x;start_y=y;move=1u;}out[used++]=(svg_point){svg_scale(x,vx,vw,w),svg_scale(y,vy,vh,h),move};if(c=='m')command=relative?'l':'L';
    }
    return used;
}

int kimg_load_svg_memory(kimg *out,const void *data,uint32_t size)
{
    const char *begin=(const char *)data,*end=begin+size,*svg=svg_find(begin,end,"<svg"),*svg_end;int32_t vx=0,vy=0,vw=300*1024,vh=150*1024;uint32_t w=300u,h=150u;
    if(!out||!svg||size>4u*1024u*1024u)return -1;svg_end=svg;while(svg_end<end&&*svg_end!='>')++svg_end;
    int32_t aw=svg_attr_number(svg,svg_end,"width",vw),ah=svg_attr_number(svg,svg_end,"height",vh);if(aw>0)w=(uint32_t)(aw/1024);if(ah>0)h=(uint32_t)(ah/1024);
    const char *view,*view_end;if(svg_attr(svg,svg_end,"viewBox",&view,&view_end)){int ok=1;vx=svg_number(&view,view_end,&ok);vy=svg_number(&view,view_end,&ok);vw=svg_number(&view,view_end,&ok);vh=svg_number(&view,view_end,&ok);if(!ok||vw<=0||vh<=0){vx=vy=0;vw=(int32_t)w*1024;vh=(int32_t)h*1024;}}
    if(!w||!h)return -1;if(w>SVG_MAX_DIM){h=(uint32_t)((uint64_t)h*SVG_MAX_DIM/w);w=SVG_MAX_DIM;}if(h>SVG_MAX_DIM){w=(uint32_t)((uint64_t)w*SVG_MAX_DIM/h);h=SVG_MAX_DIM;}if(!w)w=1u;if(!h)h=1u;
    uint64_t bytes=(uint64_t)w*h*4u,pages=(bytes+4095u)>>12;uint32_t *px=(uint32_t *)pmem_alloc_pages(pages);if(!px)return -1;memset(px,0,(size_t)bytes);
    const char *p=svg_end;while((p=svg_find(p,end,"<"))!=0&&p<end){const char *te=p;char quote=0;while(te<end){if(quote){if(*te==quote)quote=0;}else if(*te=='\''||*te=='\"')quote=*te;else if(*te=='>')break;++te;}if(te>=end)break;
        if(svg_find(p,p+9<te?p+9:te,"<rect")){int32_t x=svg_attr_number(p,te,"x",0),y=svg_attr_number(p,te,"y",0),rw=svg_attr_number(p,te,"width",0),rh=svg_attr_number(p,te,"height",0);uint32_t c=svg_color(p,te,"fill",0xFF000000u);int32_t x0=svg_scale(x,vx,vw,w),y0=svg_scale(y,vy,vh,h),x1=svg_scale(x+rw,vx,vw,w),y1=svg_scale(y+rh,vy,vh,h);for(int32_t yy=y0;yy<y1;++yy)for(int32_t xx=x0;xx<x1;++xx)svg_pixel(px,w,h,xx,yy,c);}
        else if(svg_find(p,p+11<te?p+11:te,"<circle")){int32_t cx=svg_scale(svg_attr_number(p,te,"cx",0),vx,vw,w),cy=svg_scale(svg_attr_number(p,te,"cy",0),vy,vh,h),r=svg_scale(svg_attr_number(p,te,"r",0),0,vw,w);uint32_t c=svg_color(p,te,"fill",0xFF000000u);for(int32_t yy=-r;yy<=r;++yy)for(int32_t xx=-r;xx<=r;++xx)if((int64_t)xx*xx+(int64_t)yy*yy<=(int64_t)r*r)svg_pixel(px,w,h,cx+xx,cy+yy,c);}
        else if(svg_find(p,p+8<te?p+8:te,"<line")){svg_point a={svg_scale(svg_attr_number(p,te,"x1",0),vx,vw,w),svg_scale(svg_attr_number(p,te,"y1",0),vy,vh,h),0u},b={svg_scale(svg_attr_number(p,te,"x2",0),vx,vw,w),svg_scale(svg_attr_number(p,te,"y2",0),vy,vh,h),0u};svg_line(px,w,h,a,b,svg_color(p,te,"stroke",0xFF000000u));}
        else if(svg_find(p,p+8<te?p+8:te,"<path")){const char *d,*de;if(svg_attr(p,te,"d",&d,&de)){svg_point points[SVG_MAX_POINTS];uint32_t count=svg_path_points(d,de,points,SVG_MAX_POINTS,vx,vy,vw,vh,w,h);uint32_t fill=svg_color(p,te,"fill",0xFF000000u),stroke=svg_color(p,te,"stroke",0u);svg_fill_polygon(px,w,h,points,count,fill);if(stroke)for(uint32_t i=1u;i<count;++i)if(!points[i].move)svg_line(px,w,h,points[i-1u],points[i],stroke);}}
        p=te+1;
    }
    out->px=px;out->w=w;out->h=h;return 0;
}
