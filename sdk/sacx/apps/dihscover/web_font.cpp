#include "web_font.h"
#include <stdlib.h>
#include <string.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include FT_SYNTHESIS_H
#include <hb.h>
#include <hb-ot.h>

/* Browser-local font objects: layout and painting consume the same shaped
 * runs and 26.6 advances. No per-candidate kernel text calls or byte widths. */
struct face_entry {uint8_t *bytes;FT_Face ft;hb_face_t *hb;uint32_t px,weight,italic;};
struct glyph {uint32_t id,cluster;int32_t advance,x,y;uint32_t family;};
struct run {
    uint64_t key,age;char *text;uint32_t bytes,count,family,px,weight,italic;
    int32_t width;glyph *items;
};
struct bitmap {
    uint64_t key,age;uint8_t *pixels;uint32_t w,h;
    int left,top;
};
static FT_Library library;
static face_entry faces[3];
static run runs[256];static bitmap bitmaps[1024];
static uint64_t clock_age;
static uint32_t run_bytes,glyph_bytes;
static const uint32_t RUN_LIMIT=2*1024*1024,GLYPH_LIMIT=2*1024*1024;
static uint32_t minimum_px=8;
static uint32_t family_map[3]={0,1,2};
void web_font_preferences(uint32_t sans,uint32_t serif,uint32_t mono,uint32_t minimum) {
    family_map[0]=sans<3?sans:0;family_map[1]=serif<3?serif:1;family_map[2]=mono<3?mono:2;
    minimum_px=minimum<6?6:minimum>32?32:minimum;
}

static void free_run(run &r) {
    if(r.text){run_bytes-=r.bytes+1+r.count*sizeof(glyph);free(r.text);free(r.items);}
    r={};
}
static void free_bitmap(bitmap &b) {if(b.pixels){glyph_bytes-=b.w*b.h;free(b.pixels);}b={};}
static uint64_t hash_bytes(uint64_t h,const void *data,uint32_t n) {
    auto *p=(const uint8_t*)data;for(uint32_t i=0;i<n;++i){h^=p[i];h*=1099511628211ull;}return h;
}
static void select_face(uint32_t family,uint32_t px,uint32_t weight,uint32_t italic) {
    face_entry &entry=faces[family];
    if(entry.px==px&&entry.weight==weight&&entry.italic==italic)return;
    entry.px=px;entry.weight=weight;entry.italic=italic;
    FT_Face face=entry.ft;
    FT_Set_Char_Size(face,0,(FT_F26Dot6)px,72,72);
    FT_MM_Var *axes=0;
    if(!FT_Get_MM_Var(face,&axes)) {
        FT_Fixed values[16];uint32_t count=axes->num_axis<16 ? axes->num_axis:16;
        for(uint32_t i=0;i<count;++i) {
            values[i]=axes->axis[i].def;
            if(axes->axis[i].tag==FT_MAKE_TAG('w','g','h','t')) values[i]=(FT_Fixed)(weight*65536u);
            if(axes->axis[i].tag==FT_MAKE_TAG('w','d','t','h')) values[i]=100*65536;
        }
        FT_Set_Var_Design_Coordinates(face,count,values);FT_Done_MM_Var(library,axes);
    }
    FT_Matrix transform={65536,italic ? 13930:0,0,65536};
    FT_Set_Transform(face,&transform,0);
}
static run *shape(const plot_font_style_t *style,const char *text,uint32_t bytes) {
    if(!library||!style||!text||bytes>65536)return 0;
    uint32_t family=style->family==PLOT_FONT_FAMILY_SERIF ? 1:style->family==PLOT_FONT_FAMILY_MONOSPACE ? 2:0;
    family=family_map[family];
    uint32_t px=(uint32_t)(((int64_t)style->size*256)/(PLOT_STYLE_SCALE*3));
    if(px<minimum_px*64)px=minimum_px*64;if(px>256*64)px=256*64;
    uint32_t weight=style->weight<100 ? 400:style->weight>900 ? 900:(uint32_t)style->weight;
    uint32_t italic=(style->flags&(FONTF_ITALIC|FONTF_OBLIQUE))!=0;
    uint32_t params[]={family,px,weight,italic};
    uint64_t key=hash_bytes(hash_bytes(1469598103934665603ull,params,sizeof(params)),text,bytes);
    run *slot=&runs[0];
    for(auto &r:runs) {
        if(r.text&&r.key==key&&r.bytes==bytes&&!memcmp(r.text,text,bytes)){r.age=++clock_age;return &r;}
        if(!r.text || (slot->text&&r.age<slot->age))slot=&r;
    }
    hb_font_t *font=hb_font_create(faces[family].hb);hb_ot_font_set_funcs(font);hb_font_set_scale(font,px,px);
    hb_variation_t variation[]={{HB_TAG('w','g','h','t'),(float)weight},{HB_TAG('w','d','t','h'),100.0f}};
    hb_font_set_variations(font,variation,2);
    hb_buffer_t *buffer=hb_buffer_create();hb_buffer_add_utf8(buffer,text,bytes,0,bytes);
    hb_buffer_guess_segment_properties(buffer);hb_shape(font,buffer,0,0);
    unsigned count=0;auto *info=hb_buffer_get_glyph_infos(buffer,&count);auto *pos=hb_buffer_get_glyph_positions(buffer,0);
    uint64_t needed=(uint64_t)bytes+1+(uint64_t)count*sizeof(glyph);
    if(!hb_buffer_allocation_successful(buffer)||needed>RUN_LIMIT) {hb_buffer_destroy(buffer);hb_font_destroy(font);return 0;}
    free_run(*slot);
    while(run_bytes+needed>RUN_LIMIT) {
        run *old=0;for(auto &r:runs)if(r.text&&(!old||r.age<old->age))old=&r;
        if(!old)break;free_run(*old);
    }
    char *copy=(char*)malloc(bytes+1);glyph *items=(glyph*)malloc(count ? count*sizeof(glyph):1);
    if(!copy||!items){free(copy);free(items);hb_buffer_destroy(buffer);hb_font_destroy(font);return 0;}
    memcpy(copy,text,bytes);copy[bytes]=0;int32_t width=0;
    for(unsigned i=0;i<count;++i){
        items[i]={info[i].codepoint,info[i].cluster,pos[i].x_advance,pos[i].x_offset,pos[i].y_offset,family};
        if(!items[i].id&&items[i].cluster<bytes){
            uint32_t at=items[i].cluster,codepoint=(uint8_t)text[at++],extra=0;
            if(codepoint>=0xc2&&codepoint<0xe0){extra=1;codepoint&=31;}else if(codepoint>=0xe0&&codepoint<0xf0){extra=2;codepoint&=15;}else if(codepoint>=0xf0&&codepoint<0xf5){extra=3;codepoint&=7;}
            bool valid=at+extra<=bytes;while(extra--&&valid){uint8_t c=(uint8_t)text[at++];if((c&0xc0)!=0x80)valid=false;codepoint=(codepoint<<6)|(c&63);}
            /* Preserve the primary shaping result for a missing mark in a
             * cluster whose base exists. Try bundled faces for missing bases. */
            if(valid&&!FT_Get_Char_Index(faces[family].ft,codepoint))for(unsigned fallback=0;fallback<3;++fallback){
                if(fallback==family)continue;uint32_t id=FT_Get_Char_Index(faces[fallback].ft,codepoint);if(!id)continue;
                hb_font_t *other=hb_font_create(faces[fallback].hb);hb_ot_font_set_funcs(other);hb_font_set_scale(other,px,px);hb_font_set_variations(other,variation,2);
                items[i].id=id;items[i].family=fallback;items[i].advance=hb_font_get_glyph_h_advance(other,id);hb_font_destroy(other);break;
            }
        }
        width+=items[i].advance;
    }
    *slot={key,++clock_age,copy,bytes,count,family,px,weight,italic,width,items};run_bytes+=(uint32_t)needed;
    hb_buffer_destroy(buffer);hb_font_destroy(font);return slot;
}
static bitmap *raster(const run &r,const glyph &g) {
    uint32_t id=g.id;uint32_t fields[]={g.family,r.px,r.weight,r.italic,id};
    uint64_t key=hash_bytes(1469598103934665603ull,fields,sizeof(fields));bitmap *slot=&bitmaps[0];
    for(auto &b:bitmaps){if(b.pixels&&b.key==key){b.age=++clock_age;return &b;}if(!b.pixels||(slot->pixels&&b.age<slot->age))slot=&b;}
    select_face(g.family,r.px,r.weight,r.italic);FT_Face face=faces[g.family].ft;
    if(FT_Load_Glyph(face,id,FT_LOAD_DEFAULT)||FT_Render_Glyph(face->glyph,FT_RENDER_MODE_NORMAL))return 0;
    FT_Bitmap &source=face->glyph->bitmap;uint32_t n=source.width*source.rows;
    if(n>GLYPH_LIMIT)return 0;free_bitmap(*slot);
    while(glyph_bytes+n>GLYPH_LIMIT){bitmap *old=0;for(auto &b:bitmaps)if(b.pixels&&(!old||b.age<old->age))old=&b;if(!old)break;free_bitmap(*old);}
    uint8_t *pixels=(uint8_t*)malloc(n ? n:1);if(!pixels)return 0;
    for(uint32_t y=0;y<source.rows;++y)for(uint32_t x=0;x<source.width;++x) {
        int row=source.pitch<0 ? (int)(source.rows-1-y):(int)y;
        const uint8_t *p=source.buffer+row*(source.pitch<0?-source.pitch:source.pitch);
        pixels[y*source.width+x]=source.pixel_mode==FT_PIXEL_MODE_MONO ? ((p[x/8]&(128>>(x%8)))?255:0):p[x];
    }
    *slot={key,++clock_age,pixels,source.width,source.rows,face->glyph->bitmap_left,face->glyph->bitmap_top};glyph_bytes+=n;return slot;
}
int web_font_init(const sacx_api *api) {
    if(library)return 0;if(!api||FT_Init_FreeType(&library))return -1;
    const char *paths[]={"0:/OS/System/Fonts/Web/Sans.ttf","0:/OS/System/Fonts/Web/Serif.ttf","0:/OS/System/Fonts/Web/Mono.ttf"};
    for(uint32_t i=0;i<3;++i) {
        uint32_t handle=0,got=0;if(api->file_open(paths[i],SACX_FILE_READ,&handle))goto failed;
        uint64_t size=api->file_size(handle);
        if(!size||size>8*1024*1024){api->file_close(handle);goto failed;}
        faces[i].bytes=(uint8_t*)malloc((size_t)size);
        if(!faces[i].bytes){api->file_close(handle);goto failed;}
        uint32_t offset=0;
        while(offset<size){uint32_t want=(uint32_t)size-offset;if(want>65536)want=65536;
            if(api->file_read(handle,faces[i].bytes+offset,want,&got)||!got)break;offset+=got;}
        api->file_close(handle);if(offset!=size||FT_New_Memory_Face(library,faces[i].bytes,(FT_Long)size,0,&faces[i].ft))goto failed;
        hb_blob_t *blob=hb_blob_create((const char*)faces[i].bytes,(unsigned)size,HB_MEMORY_MODE_READONLY,0,0);
        faces[i].hb=hb_face_create(blob,0);hb_blob_destroy(blob);
    }
    return 0;
failed:web_font_shutdown();return -1;
}
void web_font_shutdown(void) {
    for(auto &r:runs)free_run(r);for(auto &b:bitmaps)free_bitmap(b);
    for(auto &f:faces){if(f.hb)hb_face_destroy(f.hb);if(f.ft)FT_Done_Face(f.ft);free(f.bytes);f={};}
    if(library)FT_Done_FreeType(library);library=0;
}
int web_font_width(const plot_font_style_t *style,const char *text,uint32_t bytes) {
    run *r=shape(style,text,bytes);return r ? (r->width+32)/64:0;
}
uint32_t web_font_position(const plot_font_style_t *style,const char *text,uint32_t bytes,int x,int *actual) {
    run *r=shape(style,text,bytes);if(!r){*actual=0;return 0;}int32_t pen=0;
    for(uint32_t i=0;i<r->count;++i){if(pen+r->items[i].advance/2>x*64){*actual=(pen+32)/64;return r->items[i].cluster;}pen+=r->items[i].advance;}
    *actual=(pen+32)/64;return bytes;
}
void web_font_paint(const plot_font_style_t *style,const char *text,uint32_t bytes,uint32_t *pixels,uint32_t stride,
                    int x,int baseline,int x0,int y0,int x1,int y1) {
    run *r=shape(style,text,bytes);if(!r||!pixels)return;int32_t pen=0;
    uint32_t color=style->foreground;uint32_t red=color&255,green=(color>>8)&255,blue=(color>>16)&255;
    for(uint32_t i=0;i<r->count;++i) {
        const glyph &g=r->items[i];bitmap *b=raster(*r,g);if(!b){pen+=g.advance;continue;}
        int left=x+(pen+g.x)/64+b->left,top=baseline-g.y/64-b->top;
        for(uint32_t row=0;row<b->h;++row){int yy=top+(int)row;if(yy<y0||yy>=y1)continue;
            for(uint32_t col=0;col<b->w;++col){int xx=left+(int)col;if(xx<x0||xx>=x1)continue;
                uint32_t a=b->pixels[row*b->w+col];if(!a)continue;uint32_t &dst=pixels[(uint32_t)yy*stride+(uint32_t)xx];
                uint32_t inv=255-a;dst=0xff000000u|(((red*a+((dst>>16)&255)*inv+127)/255)<<16)|
                    (((green*a+((dst>>8)&255)*inv+127)/255)<<8)|((blue*a+(dst&255)*inv+127)/255);
            }
        }pen+=g.advance;
    }
}
