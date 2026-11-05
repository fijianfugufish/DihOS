// Actual native FreeType rasterization and HarfBuzz shaping, not byte metrics.
#include <sacx_user_api.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#include <hb-icu.h>
#include <atomic>
#include <thread>
#ifdef DIHOS_FONTCONFIG
#include <fontconfig/fontconfig.h>
#include <fontconfig/fcfreetype.h>
#include "fontconfig/memory.h"
#include <cstdio>
struct stat;
struct dihos_fc_directory;
static void traceFontIO(const char* operation,const char* path=nullptr){
    char text[160];snprintf(text,sizeof(text),"Font selection I/O: %s %.100s",operation,path?path:"");sacx_user_log(text);
}
extern "C" {
int __real_dihos_fc_stat(const char*,struct stat*);
int __wrap_dihos_fc_stat(const char* path,struct stat* out){traceFontIO("stat",path);return __real_dihos_fc_stat(path,out);}
int __real_dihos_fc_access(const char*,int);
int __wrap_dihos_fc_access(const char* path,int mode){traceFontIO("access",path);return __real_dihos_fc_access(path,mode);}
char* __real_dihos_fc_getcwd(char*,size_t);
char* __wrap_dihos_fc_getcwd(char* out,size_t bytes){traceFontIO("getcwd");return __real_dihos_fc_getcwd(out,bytes);}
char* __real_dihos_fc_realpath(const char*,char*);
char* __wrap_dihos_fc_realpath(const char* path,char* out){traceFontIO("realpath",path);return __real_dihos_fc_realpath(path,out);}
int __real_dihos_fc_open(const char*,int,...);
int __wrap_dihos_fc_open(const char* path,int flags,...){traceFontIO("open",path);return __real_dihos_fc_open(path,flags,0);}
dihos_fc_directory* __real_dihos_fc_opendir(const char*);
dihos_fc_directory* __wrap_dihos_fc_opendir(const char* path){traceFontIO("opendir",path);return __real_dihos_fc_opendir(path);}
}
#endif
#ifdef DIHOS_FONT_PAINT
#include <cairo.h>
#include <cairo-ft.h>
#include <vector>
#endif
extern "C" const unsigned char font_sans[],font_sans_end[],font_serif[],font_serif_end[],font_mono[],font_mono_end[];
static void check(bool ok,const char* why){if(!ok)sacx_user_exit(179,why);}
struct Shaped {unsigned count;int advance;unsigned firstCluster,lastCluster;};
static Shaped shape(FT_Face face,const char* text){
    auto* font=hb_ft_font_create_referenced(face);auto* buffer=hb_buffer_create();
    check(font && buffer && hb_buffer_allocation_successful(buffer),"Fonts: shaping allocation");
    hb_buffer_set_unicode_funcs(buffer,hb_icu_get_unicode_funcs());
    hb_buffer_add_utf8(buffer,text,-1,0,-1);hb_buffer_guess_segment_properties(buffer);hb_shape(font,buffer,nullptr,0);
    unsigned count=0;auto* info=hb_buffer_get_glyph_infos(buffer,&count);auto* positions=hb_buffer_get_glyph_positions(buffer,nullptr);
    check(count && info && positions && hb_buffer_allocation_successful(buffer),"Fonts: shaping output");
    int advance=0;for(unsigned i=0;i<count;++i){check(info[i].codepoint!=0,"Fonts: unexpected missing glyph");advance+=positions[i].x_advance;}
    Shaped result{count,advance,info[0].cluster,info[count-1].cluster};hb_buffer_destroy(buffer);hb_font_destroy(font);return result;
}
static void faceTests(const unsigned char* bytes,long length,bool mono){
    FT_Library library=nullptr;FT_Face face=nullptr;
    check(!FT_Init_FreeType(&library),"Fonts: FreeType initialization");
    check(!FT_New_Memory_Face(library,bytes,length,0,&face),"Fonts: packaged memory face");
    check(!FT_Set_Char_Size(face,0,18*64+32,72,72),"Fonts: fractional size");
    check(face->size->metrics.ascender>0 && face->size->metrics.descender<0,"Fonts: baseline metrics");
    auto narrow=shape(face,"iiii"),wide=shape(face,"WWWW");
    check(mono?narrow.advance==wide.advance:narrow.advance<wide.advance,"Fonts: proportional/monospace advances");
    auto unicode=shape(face,"\xc3\xa9\xce\xa9\xd0\x96");
    check(unicode.count==3 && unicode.firstCluster==0 && unicode.lastCluster==4,"Fonts: Unicode byte clusters");
    check(!FT_Load_Char(face,'A',FT_LOAD_RENDER),"Fonts: glyph rasterization");
    check(face->glyph->bitmap.width && face->glyph->bitmap.rows && face->glyph->bitmap.buffer,"Fonts: bitmap pixels");
    unsigned ink=0;for(unsigned y=0;y<face->glyph->bitmap.rows;++y)for(unsigned x=0;x<face->glyph->bitmap.width;++x)
        ink+=face->glyph->bitmap.buffer[y*face->glyph->bitmap.pitch+x]!=0;
    check(ink>10,"Fonts: nonempty glyph bitmap");
    FT_Done_Face(face);FT_Done_FreeType(library);
}
static void shapingTests(){
    FT_Library library=nullptr;FT_Face face=nullptr;
    check(!FT_Init_FreeType(&library) && !FT_New_Memory_Face(library,font_sans,font_sans_end-font_sans,0,&face),"Fonts: shaping face");
    check(!FT_Set_Char_Size(face,0,18*64,72,72),"Fonts: shape size");
    auto ligatures=shape(face,"office");check(ligatures.count<6,"Fonts: OpenType ligature shaping");
    auto composed=shape(face,"\xc3\xa9"),combining=shape(face,"e\xcc\x81");
    check(composed.advance==combining.advance && combining.count==1,"Fonts: combining mark shaping");
    auto small=shape(face,"measurement");
    check(!FT_Set_Char_Size(face,0,27*64,72,72),"Fonts: zoom size");
    auto large=shape(face,"measurement");check(large.advance>small.advance,"Fonts: zoom metrics");
    check(FT_Get_Char_Index(face,0x10ffff)==0,"Fonts: missing glyph detection");
    FT_Done_Face(face);FT_Done_FreeType(library);
    check(!FT_Init_FreeType(&library),"Fonts: malformed face library");
    const unsigned char invalid[]={1,2,3,4};face=nullptr;
    check(FT_New_Memory_Face(library,invalid,sizeof(invalid),0,&face)!=0,"Fonts: malformed font rejection");
    FT_Done_FreeType(library);
}
#ifdef DIHOS_FONT_PAINT
static void paintingTests(){
    FT_Library library=nullptr;FT_Face face=nullptr;
    check(!FT_Init_FreeType(&library) && !FT_New_Memory_Face(library,font_sans,font_sans_end-font_sans,0,&face),"Fonts: paint face");
    check(!FT_Set_Char_Size(face,0,18*64+32,72,72),"Fonts: paint fractional size");
    auto* font=hb_ft_font_create_referenced(face);auto* buffer=hb_buffer_create();
    hb_buffer_set_unicode_funcs(buffer,hb_icu_get_unicode_funcs());hb_buffer_add_utf8(buffer,"office e\xcc\x81 \xce\xa9",-1,0,-1);
    hb_buffer_guess_segment_properties(buffer);hb_shape(font,buffer,nullptr,0);
    unsigned count=0;auto* info=hb_buffer_get_glyph_infos(buffer,&count);auto* pos=hb_buffer_get_glyph_positions(buffer,nullptr);
    check(count && count<32 && hb_buffer_allocation_successful(buffer),"Fonts: paint shaped run");
    cairo_glyph_t glyphs[32];double x=8,y=40;
    for(unsigned i=0;i<count;++i){glyphs[i]={info[i].codepoint,x+pos[i].x_offset/64.0,y-pos[i].y_offset/64.0};x+=pos[i].x_advance/64.0;y-=pos[i].y_advance/64.0;}
    constexpr unsigned width=256,height=80,guard=16;
    std::vector<uint32_t> pixels(width*height+2*guard,0xdeadbeef);
    auto* surface=cairo_image_surface_create_for_data(reinterpret_cast<unsigned char*>(pixels.data()+guard),CAIRO_FORMAT_ARGB32,width,height,width*4);
    auto* cr=cairo_create(surface);cairo_set_source_rgb(cr,1,1,1);cairo_paint(cr);
    auto* cairoFont=cairo_ft_font_face_create_for_ft_face(face,FT_LOAD_NO_HINTING);
    check(cairo_font_face_status(cairoFont)==CAIRO_STATUS_SUCCESS,"Fonts: Cairo memory font");
    cairo_set_font_face(cr,cairoFont);cairo_set_font_size(cr,18.5);cairo_set_source_rgb(cr,0,0,0);
    cairo_rectangle(cr,0,0,width,height);cairo_clip(cr);cairo_show_glyphs(cr,glyphs,count);cairo_surface_flush(surface);
    check(cairo_status(cr)==CAIRO_STATUS_SUCCESS,"Fonts: Cairo glyph paint status");
    unsigned ink=0;
    for(unsigned row=0;row<height;++row)for(unsigned column=0;column<width;++column)if(pixels[guard+row*width+column]!=0xffffffff){
        ++ink;check(column>=7 && column<x+4 && row>=10 && row<50,"Fonts: shaped paint bounds/baseline");}
    check(ink>100,"Fonts: painted glyph pixels");
    for(unsigned i=0;i<guard;++i)check(pixels[i]==0xdeadbeef && pixels[pixels.size()-1-i]==0xdeadbeef,"Fonts: painted surface guards");
    cairo_destroy(cr);cairo_font_face_destroy(cairoFont);cairo_surface_destroy(surface);
    hb_buffer_destroy(buffer);hb_font_destroy(font);cairo_debug_reset_static_data();FT_Done_Face(face);FT_Done_FreeType(library);
}
#endif
#ifdef DIHOS_FONTCONFIG
static void selectionTests(){
    FT_Library library=nullptr;FT_Face faces[3]={};
    check(!FT_Init_FreeType(&library),"Font selection: FreeType library");
    auto* config=dihos_fc_config_create_memory();check(config,"Font selection: memory config");
    check(FcConfigSetCurrent(config),"Font selection: explicit current config before face queries");
    const unsigned char* starts[]={font_sans,font_serif,font_mono};
    const unsigned char* ends[]={font_sans_end,font_serif_end,font_mono_end};
    const char* identifiers[]={"/DihOS/MemoryFonts/Sans.ttf","/DihOS/MemoryFonts/Serif.ttf","/DihOS/MemoryFonts/Mono.ttf"};
    for(unsigned i=0;i<3;++i){
        check(!FT_New_Memory_Face(library,starts[i],ends[i]-starts[i],0,&faces[i]),"Font selection: memory face");
        auto* pattern=FcFreeTypeQueryFace(faces[i],reinterpret_cast<const FcChar8*>(identifiers[i]),0,nullptr);
        check(pattern && FcPatternAddFTFace(pattern,FC_FT_FACE,faces[i]),"Font selection: actual face metadata");
        check(dihos_fc_config_add_pattern(config,pattern),"Font selection: registered pattern ownership");
        FcPatternDestroy(pattern);
    }
    check(FcConfigGetFonts(config,FcSetApplication)->nfont==3,"Font selection: registered set");
    const char xml[]="<fontconfig><alias binding='strong'><family>sans-serif</family><prefer><family>Noto Sans</family></prefer></alias><alias binding='strong'><family>serif</family><prefer><family>Noto Serif</family></prefer></alias><alias binding='strong'><family>monospace</family><prefer><family>Noto Sans Mono</family></prefer></alias></fontconfig>";
    check(FcConfigParseAndLoadFromMemory(config,reinterpret_cast<const FcChar8*>(xml),FcTrue),"Font selection: memory alias rules");
    unsigned io=dihos_fc_io_denials();const char* generic[]={"sans-serif","serif","monospace"};
    check(io==0,"Font selection: memory registration attempted filesystem access");
    for(unsigned i=0;i<3;++i){
        auto* request=FcPatternCreate();check(request,"Font selection: request");
        check(FcPatternAddString(request,FC_FAMILY,reinterpret_cast<const FcChar8*>(generic[i])) && FcPatternAddDouble(request,FC_PIXEL_SIZE,18.5),"Font selection: family and fractional size");
        check(FcConfigSubstitute(nullptr,request,FcMatchPattern),"Font selection: config substitution");FcDefaultSubstitute(request);
        FcResult result;auto* match=FcFontMatch(nullptr,request,&result);FT_Face selected=nullptr;
        check(match && result==FcResultMatch && FcPatternGetFTFace(match,FC_FT_FACE,0,&selected)==FcResultMatch && selected==faces[i],"Font selection: generic family exact face");
        FcCharSet* charset=nullptr;check(FcPatternGetCharSet(match,FC_CHARSET,0,&charset)==FcResultMatch && FcCharSetHasChar(charset,0x3a9),"Font selection: coverage metadata");
        auto* cairoFont=cairo_ft_font_face_create_for_pattern(match);
        check(cairo_font_face_status(cairoFont)==CAIRO_STATUS_SUCCESS,"Font selection: Cairo Fontconfig face");
        auto* surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,128,64);auto* cr=cairo_create(surface);
        cairo_set_source_rgb(cr,1,1,1);cairo_paint(cr);cairo_set_font_face(cr,cairoFont);cairo_set_font_size(cr,18.5);cairo_set_source_rgb(cr,0,0,0);
        cairo_glyph_t glyph{FT_Get_Char_Index(selected,'M'),8,40};cairo_show_glyphs(cr,&glyph,1);cairo_surface_flush(surface);
        check(cairo_status(cr)==CAIRO_STATUS_SUCCESS,"Font selection: matched-pattern paint");
        auto* pixels=reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(surface));unsigned ink=0;
        const unsigned stride=cairo_image_surface_get_stride(surface)/4;
        for(unsigned row=0;row<64;++row)for(unsigned column=0;column<128;++column)ink+=pixels[row*stride+column]!=0xffffffff;
        check(ink>10,"Font selection: matched-pattern glyph pixels");
        cairo_destroy(cr);cairo_font_face_destroy(cairoFont);cairo_surface_destroy(surface);
        auto* sorted=FcFontSort(nullptr,request,FcFalse,nullptr,&result);check(sorted && sorted->nfont==3,"Font selection: fallback ordering");
        FcFontSetDestroy(sorted);FcPatternDestroy(match);FcPatternDestroy(request);
    }
    auto matchWorker=[&](unsigned family){for(unsigned attempt=0;attempt<4;++attempt){
        auto* request=FcPatternCreate();check(request && FcPatternAddString(request,FC_FAMILY,reinterpret_cast<const FcChar8*>(generic[family])),"Font selection: concurrent request");
        check(FcConfigSubstitute(nullptr,request,FcMatchPattern),"Font selection: concurrent substitution");FcDefaultSubstitute(request);
        FcResult result;auto* match=FcFontMatch(nullptr,request,&result);FT_Face selected=nullptr;
        check(match && result==FcResultMatch && FcPatternGetFTFace(match,FC_FT_FACE,0,&selected)==FcResultMatch && selected==faces[family],"Font selection: concurrent matching");
        FcPatternDestroy(match);FcPatternDestroy(request);
    }};
    std::thread first(matchWorker,0),second(matchWorker,1),third(matchWorker,2);first.join();second.join();third.join();
    check(dihos_fc_io_denials()==io,"Font selection: matching attempted filesystem access");
    cairo_debug_reset_static_data();
    FcConfigDestroy(config);FcFini();
    for(auto* face:faces)FT_Done_Face(face);FT_Done_FreeType(library);
}
#endif
extern "C" int dihos_user_main(const sacx_api* api){
    faceTests(font_sans,font_sans_end-font_sans,false);faceTests(font_serif,font_serif_end-font_serif,false);faceTests(font_mono,font_mono_end-font_mono,true);
    shapingTests();api->log("Native fonts: packaged families, baselines, Unicode, ligatures, combining marks and zoom OK");
#ifdef DIHOS_FONTCONFIG
    selectionTests();api->log("Native fonts: real Fontconfig memory registration, aliases, coverage, matching and fallback OK");
#endif
#ifdef DIHOS_FONT_PAINT
    paintingTests();api->log("Native fonts: shaped Unicode glyphs painted through Cairo FreeType OK");
#endif
    std::atomic<unsigned> done{0};auto worker=[&]{for(unsigned i=0;i<3;++i)faceTests(font_sans,font_sans_end-font_sans,false);++done;};
    std::thread first(worker),second(worker),third(worker);first.join();second.join();third.join();check(done==3,"Fonts: concurrent shaping");
    api->log("SACX fonts: native FreeType/HarfBuzz rasterization, shaping and threading OK");return 114;
}
