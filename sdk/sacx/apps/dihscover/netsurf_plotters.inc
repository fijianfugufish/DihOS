static void put_pixel(int x,int y,uint32_t value)
{
    if(!g_surface_pixels||x<g_clip.x0||y<g_clip.y0||x>=g_clip.x1||y>=g_clip.y1||x<0||y<0||x>=(int)g_width||y>=(int)g_height)return;
    g_surface_pixels[(uint32_t)y*g_surface_stride+(uint32_t)x]=value;
}

static void fill_rect(int x0,int y0,int x1,int y1,uint32_t value)
{
    x0=maximum(x0,maximum(g_clip.x0,0));y0=maximum(y0,maximum(g_clip.y0,0));x1=minimum(x1,minimum(g_clip.x1,(int)g_width));y1=minimum(y1,minimum(g_clip.y1,(int)g_height));
    for(int y=y0;y<y1;++y){uint32_t *row=g_surface_pixels+(uint32_t)y*g_surface_stride;for(int x=x0;x<x1;++x)row[x]=value;}
}

static nserror plot_clip(const redraw_context *ctx,const rect *clip){(void)ctx;g_clip={maximum(g_paint_clip.x0,maximum(0,clip->x0)),maximum(g_paint_clip.y0,maximum(0,clip->y0)),minimum(g_paint_clip.x1,minimum((int)g_width,clip->x1)),minimum(g_paint_clip.y1,minimum((int)g_height,clip->y1))};return NSERROR_OK;}
static nserror plot_line(const redraw_context*,const plot_style_t*,const rect*);
static nserror plot_arc(const redraw_context *ctx,const plot_style_t *style,int x,int y,int radius,int a1,int a2){
    if(radius<=0||style->stroke_type==PLOT_OP_TYPE_NONE)return NSERROR_OK;
    double sweep=(double)a2-a1;while(sweep<0)sweep+=360;if(sweep>360)sweep=360;
    double estimate=sweep*radius/90;unsigned steps=estimate>=4095?4096:(unsigned)estimate+1;
    double angle=a1*0.017453292519943295;int px=(int)(x+radius*cos(angle)),py=(int)(y-radius*sin(angle));
    for(unsigned i=1;i<=steps;++i){angle=(a1+sweep*i/steps)*0.017453292519943295;int nx=(int)(x+radius*cos(angle)),ny=(int)(y-radius*sin(angle));rect line={px,py,nx,ny};plot_line(ctx,style,&line);px=nx;py=ny;}return NSERROR_OK;
}
static nserror plot_disc(const redraw_context *ctx,const plot_style_t *style,int cx,int cy,int radius)
{
    (void)ctx;if(radius<=0)return NSERROR_OK;
    int width=(int)((int64_t)style->stroke_width/PLOT_STYLE_SCALE);if(width<1)width=1;if(width>64)width=64;
    int outer=radius>2147483647-width/2?2147483647:radius+width/2,inner=radius-width/2;if(inner<0)inner=0;
    int x0=maximum(g_clip.x0,0),y0=maximum(g_clip.y0,0),x1=minimum(g_clip.x1,(int)g_width),y1=minimum(g_clip.y1,(int)g_height);
    int64_t left=(int64_t)cx-outer,top=(int64_t)cy-outer,right=(int64_t)cx+outer+1,bottom=(int64_t)cy+outer+1;
    if(left>x0)x0=left>=x1?x1:(int)left;if(top>y0)y0=top>=y1?y1:(int)top;
    if(right<x1)x1=right<=x0?x0:(int)right;if(bottom<y1)y1=bottom<=y0?y0:(int)bottom;
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){
        int64_t dx=(int64_t)x-cx,dy=(int64_t)y-cy;
        if(dx>outer||dx<-outer||dy>outer||dy<-outer)continue;
        int64_t distance=dx*dx+dy*dy;
        if(style->fill_type!=PLOT_OP_TYPE_NONE&&distance<=(int64_t)radius*radius)put_pixel(x,y,argb(style->fill_colour));
        if(style->stroke_type!=PLOT_OP_TYPE_NONE&&distance>=(int64_t)inner*inner&&distance<=(int64_t)outer*outer)put_pixel(x,y,argb(style->stroke_colour));
    }return NSERROR_OK;
}
static nserror plot_line(const redraw_context *ctx,const plot_style_t *style,const rect *line)
{
    (void)ctx;if(style->stroke_type==PLOT_OP_TYPE_NONE)return NSERROR_OK;int x0=line->x0,y0=line->y0,x1=line->x1,y1=line->y1;
    int left=maximum(0,g_clip.x0),top=maximum(0,g_clip.y0),right=minimum((int)g_width,g_clip.x1)-1,bottom=minimum((int)g_height,g_clip.y1)-1;
    if(right<left||bottom<top)return NSERROR_OK;
    auto code=[&](int x,int y){return (x<left?1:0)|(x>right?2:0)|(y<top?4:0)|(y>bottom?8:0);};
    for(unsigned pass=0;pass<8;++pass){int a=code(x0,y0),b=code(x1,y1);if(!(a|b))break;if(a&b)return NSERROR_OK;
        int c=a?a:b;int64_t x=x0,y=y0;
        if(c&8){if(y1==y0)return NSERROR_OK;y=bottom;x=x0+(int64_t)(((double)x1-x0)*(bottom-(double)y0)/((double)y1-y0));}
        else if(c&4){if(y1==y0)return NSERROR_OK;y=top;x=x0+(int64_t)(((double)x1-x0)*(top-(double)y0)/((double)y1-y0));}
        else if(c&2){if(x1==x0)return NSERROR_OK;x=right;y=y0+(int64_t)(((double)y1-y0)*(right-(double)x0)/((double)x1-x0));}
        else{if(x1==x0)return NSERROR_OK;x=left;y=y0+(int64_t)(((double)y1-y0)*(left-(double)x0)/((double)x1-x0));}
        if(c==a){x0=(int)x;y0=(int)y;}else{x1=(int)x;y1=(int)y;}
    }
    if(code(x0,y0)||code(x1,y1))return NSERROR_OK;
    int dx=x1>x0?x1-x0:x0-x1,sx=x0<x1?1:-1,dy=-(y1>y0?y1-y0:y0-y1),sy=y0<y1?1:-1,err=dx+dy;
    int thickness=(int)(((int64_t)style->stroke_width+PLOT_STYLE_SCALE/2)/PLOT_STYLE_SCALE);if(thickness<1)thickness=1;if(thickness>64)thickness=64;
    uint32_t color=argb(style->stroke_colour);for(;;){if(thickness==1)put_pixel(x0,y0,color);else fill_rect(x0-thickness/2,y0-thickness/2,x0+(thickness+1)/2,y0+(thickness+1)/2,color);if(x0==x1&&y0==y1)break;int e2=2*err;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}}return NSERROR_OK;
}
static nserror plot_rectangle(const redraw_context *ctx,const plot_style_t *style,const rect *box)
{
    ++g_plot_rects;
    if(style->fill_type!=PLOT_OP_TYPE_NONE)fill_rect(box->x0,box->y0,box->x1,box->y1,argb(style->fill_colour));
    if(style->stroke_type!=PLOT_OP_TYPE_NONE){rect edge={box->x0,box->y0,box->x1,box->y0};plot_line(ctx,style,&edge);edge={box->x1,box->y0,box->x1,box->y1};plot_line(ctx,style,&edge);edge={box->x1,box->y1,box->x0,box->y1};plot_line(ctx,style,&edge);edge={box->x0,box->y1,box->x0,box->y0};plot_line(ctx,style,&edge);}return NSERROR_OK;
}
static nserror plot_polygon(const redraw_context *ctx,const plot_style_t *style,const int *points,unsigned count)
{
    if(count<3u)return NSERROR_OK;if(count>65536u)return NSERROR_BAD_PARAMETER;
    double *crossings=(double*)malloc(count*sizeof(double));if(!crossings)return NSERROR_NOMEM;
    for(int y=maximum(0,g_clip.y0);y<minimum((int)g_height,g_clip.y1);++y){
        unsigned used=0;double scan=y+0.5;
        for(unsigned i=0,j=count-1;i<count;j=i++){
            double ax=points[j*2],ay=points[j*2+1],bx=points[i*2],by=points[i*2+1];
            if((ay<=scan&&by>scan)||(by<=scan&&ay>scan))crossings[used++]=ax+(scan-ay)*(bx-ax)/(by-ay);
        }
        for(unsigned i=1;i<used;++i){double v=crossings[i];unsigned j=i;while(j&&crossings[j-1]>v){crossings[j]=crossings[j-1];--j;}crossings[j]=v;}
        if(style->fill_type!=PLOT_OP_TYPE_NONE)for(unsigned i=0;i+1<used;i+=2){
            double l=crossings[i],r=crossings[i+1];int left=maximum(g_clip.x0,0),right=minimum(g_clip.x1,(int)g_width);
            if(l>left)left=l>=right?right:(int)ceil(l-0.5);if(r<right)right=r<=left?left:(int)ceil(r-0.5);
            fill_rect(left,y,right,y+1,argb(style->fill_colour));
        }
    }free(crossings);
    if(style->stroke_type!=PLOT_OP_TYPE_NONE)for(unsigned i=0;i<count;++i){rect edge={points[i*2u],points[i*2u+1u],points[((i+1u)%count)*2u],points[((i+1u)%count)*2u+1u]};plot_line(ctx,style,&edge);}return NSERROR_OK;
}
static nserror plot_path(const redraw_context *ctx,const plot_style_t *style,const float *path,unsigned count,const float transform[6]) {
    if(!path||!transform||count>65536)return NSERROR_BAD_PARAMETER;
    struct edge {double x0,y0,x1,y1;};
    const unsigned capacity=8192;edge *edges=(edge*)malloc(capacity*sizeof(edge));
    if(!edges)return NSERROR_NOMEM;unsigned used=0;double x=0,y=0,sx=0,sy=0;bool started=false;bool valid=true;
    auto add=[&](double nx,double ny){if(used==capacity){valid=false;return;}edges[used++]={x,y,nx,ny};x=nx;y=ny;};
    auto tx=[&](double px,double py){return px*transform[0]+py*transform[2]+transform[4];};
    auto ty=[&](double px,double py){return px*transform[1]+py*transform[3]+transform[5];};
    for(unsigned i=0;i<count&&valid;) {
        float op=path[i++];
        if(op==PLOTTER_PATH_CLOSE){if(!started){valid=false;break;}add(sx,sy);started=false;continue;}
        unsigned args=op==PLOTTER_PATH_BEZIER?6:2;
        if(i+args>count||(op!=PLOTTER_PATH_MOVE&&op!=PLOTTER_PATH_LINE&&op!=PLOTTER_PATH_BEZIER)){valid=false;break;}
        for(unsigned j=0;j<args;++j)if(!(path[i+j]>=-10000000.f&&path[i+j]<=10000000.f))valid=false;
        if(!valid)break;
        double nx=tx(path[i+args-2],path[i+args-1]),ny=ty(path[i+args-2],path[i+args-1]);
        if(!(nx>=-10000000&&nx<=10000000&&ny>=-10000000&&ny<=10000000)){valid=false;break;}
        if(op==PLOTTER_PATH_MOVE){if(started&&style->fill_type!=PLOT_OP_TYPE_NONE)add(sx,sy);x=sx=nx;y=sy=ny;started=true;}
        else if(!started){valid=false;break;}
        else if(op==PLOTTER_PATH_LINE)add(nx,ny);
        else {
            double ax=x,ay=y,bx=tx(path[i],path[i+1]),by=ty(path[i],path[i+1]),cx=tx(path[i+2],path[i+3]),cy=ty(path[i+2],path[i+3]);
            /* Subdivide in device space, bounded even for hostile paths. */
            double span=fabs(bx-ax)+fabs(by-ay)+fabs(cx-bx)+fabs(cy-by)+fabs(nx-cx)+fabs(ny-cy);
            if(!(bx>=-10000000&&bx<=10000000&&by>=-10000000&&by<=10000000&&cx>=-10000000&&cx<=10000000&&cy>=-10000000&&cy<=10000000)){valid=false;break;}
            unsigned steps=span>=1024?1024:(unsigned)(span/2)+1;
            for(unsigned step=1;step<=steps&&valid;++step){double t=(double)step/steps,u=1-t;add(u*u*u*ax+3*u*u*t*bx+3*u*t*t*cx+t*t*t*nx,u*u*u*ay+3*u*u*t*by+3*u*t*t*cy+t*t*t*ny);}
        }i+=args;
    }
    if(started&&style->fill_type!=PLOT_OP_TYPE_NONE)add(sx,sy);
    if(!valid){free(edges);return NSERROR_BAD_PARAMETER;}
    struct crossing {double x;int winding;};crossing *cross=(crossing*)malloc((used?used:1)*sizeof(crossing));
    if(!cross){free(edges);return NSERROR_NOMEM;}
    if(style->fill_type!=PLOT_OP_TYPE_NONE)for(int yy=maximum(g_clip.y0,0);yy<minimum(g_clip.y1,(int)g_height);++yy){
        unsigned n=0;double scan=yy+0.5;
        for(unsigned i=0;i<used;++i){edge e=edges[i];if((e.y0<=scan&&e.y1>scan)||(e.y1<=scan&&e.y0>scan))cross[n++]={e.x0+(scan-e.y0)*(e.x1-e.x0)/(e.y1-e.y0),e.y1>e.y0?1:-1};}
        for(unsigned i=1;i<n;++i){crossing v=cross[i];unsigned j=i;while(j&&cross[j-1].x>v.x){cross[j]=cross[j-1];--j;}cross[j]=v;}
        int winding=0;for(unsigned i=0;i+1<n;++i){winding+=cross[i].winding;if(!winding)continue;
            double l=cross[i].x,r=cross[i+1].x;int left=maximum(0,g_clip.x0),right=minimum((int)g_width,g_clip.x1);
            if(l>left)left=l>=right?right:(int)ceil(l-0.5);if(r<right)right=r<=left?left:(int)ceil(r-0.5);fill_rect(left,yy,right,yy+1,argb(style->fill_colour));}
    }
    if(style->stroke_type!=PLOT_OP_TYPE_NONE)for(unsigned i=0;i<used;++i){edge e=edges[i];rect line={(int)e.x0,(int)e.y0,(int)e.x1,(int)e.y1};plot_line(ctx,style,&line);}
    free(cross);free(edges);return NSERROR_OK;
}

static uint32_t blend(uint32_t dst,uint32_t src)
{
    uint32_t a=src>>24;if(a==255u)return src;if(!a)return dst;uint32_t ia=255u-a;
    uint32_t r=(((src>>16)&255u)*a+((dst>>16)&255u)*ia)/255u,g=(((src>>8)&255u)*a+((dst>>8)&255u)*ia)/255u,b=((src&255u)*a+(dst&255u)*ia)/255u;
    return 0xff000000u|(r<<16)|(g<<8)|b;
}

static nserror plot_bitmap(const redraw_context *ctx,struct bitmap *bitmap,int x,int y,int width,int height,colour background,bitmap_flags_t flags)
{
    ++g_plot_bitmaps;
    (void)ctx;(void)background;dihos_bitmap *source=(dihos_bitmap*)bitmap;if(!source||!source->pixels||!source->width||!source->height||width<=0||height<=0)return NSERROR_OK;
    int x0=maximum(x,g_clip.x0),y0=maximum(y,g_clip.y0),x1=(int)(((int64_t)x+width<g_clip.x1)?(int64_t)x+width:g_clip.x1),y1=(int)(((int64_t)y+height<g_clip.y1)?(int64_t)y+height:g_clip.y1);
    if(flags&BITMAPF_REPEAT_X){x0=g_clip.x0;x1=g_clip.x1;}if(flags&BITMAPF_REPEAT_Y){y0=g_clip.y0;y1=g_clip.y1;}
    for(int dy=y0;dy<y1;++dy)for(int dx=x0;dx<x1;++dx){int64_t rx=((int64_t)dx-x)%width,ry=((int64_t)dy-y)%height;if(rx<0)rx+=width;if(ry<0)ry+=height;uint32_t sx=(uint32_t)((uint64_t)rx*source->width/(uint32_t)width),sy=(uint32_t)((uint64_t)ry*source->height/(uint32_t)height);uint32_t *dst=g_surface_pixels+(uint32_t)dy*g_surface_stride+(uint32_t)dx;*dst=blend(*dst,source->pixels[sy*source->stride+sx]);}return NSERROR_OK;
}

