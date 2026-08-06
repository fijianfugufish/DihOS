#include "../sdk/sacx/apps/dihscover/browser.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

static int failures;

void *browser_heap_alloc(uint32_t size){return std::malloc(size);}
void *browser_heap_realloc(void *ptr,uint32_t size){return std::realloc(ptr,size);}
void browser_heap_free(void *ptr){std::free(ptr);}

uint32_t b_strlen(const char *s){uint32_t n=0u;while(s&&s[n])++n;return n;}
int b_streq(const char *a,const char *b){while(a&&b&&*a&&*a==*b){++a;++b;}return a&&b&&*a==*b;}
int b_starts(const char *s,const char *p){while(s&&p&&*p&&*s==*p){++s;++p;}return p&&!*p;}
void b_copy_n(char *d,uint32_t cap,const char *s,uint32_t n){uint32_t i=0u;while(i<n&&i+1u<cap)d[i]=s[i],++i;d[i]=0;}
void b_copy(char *d,uint32_t cap,const char *s){b_copy_n(d,cap,s,b_strlen(s));}
void *b_memset(void *d,int v,__SIZE_TYPE__ n){unsigned char *p=(unsigned char *)d;for(__SIZE_TYPE__ i=0;i<n;++i)p[i]=(unsigned char)v;return d;}
void *b_memcpy(void *d,const void *s,__SIZE_TYPE__ n){unsigned char *o=(unsigned char *)d;const unsigned char *i=(const unsigned char *)s;for(__SIZE_TYPE__ k=0;k<n;++k)o[k]=i[k];return d;}

static void check(bool condition, const char *name)
{
    if (!condition) { std::printf("FAIL: %s\n", name); ++failures; }
}

static int find_id(const browser_document &doc,const char *id)
{
    for(uint32_t i=1;i<doc.node_count;++i)if(doc.nodes[i].id_off&&b_streq(doc.text+doc.nodes[i].id_off,id))return (int)i;
    return -1;
}

int main(int argc, char **argv)
{
    char url[DIHSCOVER_URL_CAP];
    browser_document doc={};
    check(browser_url_normalize(" example.com ",url,sizeof(url))==0 && b_streq(url,"https://example.com"),"normalize hostname");
    check(browser_url_resolve("https://example.com/a/page.html","/docs/x",url,sizeof(url))==0 && b_streq(url,"https://example.com/docs/x"),"root relative URL");
    check(browser_url_resolve("https://example.com/a/page.html","next",url,sizeof(url))==0 && b_streq(url,"https://example.com/a/next"),"path relative URL");
    check(browser_url_resolve("https://example.com/a/b/page.html","../image.png",url,sizeof(url))==0 && b_streq(url,"https://example.com/a/image.png"),"dot segment URL");
    check(browser_url_resolve("https://example.com/a/page.html?old=1","?new=2",url,sizeof(url))==0 && b_streq(url,"https://example.com/a/page.html?new=2"),"query URL");

    browser_document_reset(&doc,1u,"https://example.com/");
    const char malformed[]="<title>Fixture</title><h1 id='result'>Hello<p>world<a href='/next'>Next";
    check(browser_document_parse(&doc,malformed,sizeof(malformed)-1u)==0,"malformed HTML recovery");
    check(b_streq(doc.title,"Fixture"),"document title");
    check(browser_document_set_text_by_id(&doc,"result","Changed")==0,"DOM mutation");
    browser_document_layout(&doc,640u);
    check(doc.content_height>20u,"layout height");
    check(doc.node_count<DIHSCOVER_NODE_MAX,"DOM limit");

    const char styled[]="<p style='color:red;font-size:24px;display:block'>Styled</p>";
    browser_document_reset(&doc,2u,"https://example.com/");
    check(browser_document_parse(&doc,styled,sizeof(styled)-1u)==0,"inline style parse");
    browser_document_layout(&doc,320u);
    check(doc.content_height>20u,"styled layout");

    const char css[]="<style>p{color:blue}.notice{background:#ffee99}#target{font-size:24px}</style><p id='target' class='notice'>Cascade</p>";
    browser_document_reset(&doc,3u,"https://example.com/");
    check(browser_document_parse(&doc,css,sizeof(css)-1u)==0,"stylesheet parse");
    int target=find_id(doc,"target");
    check(target>0&&doc.nodes[target].style.color.b==190,"CSS tag selector");
    check(target>0&&doc.nodes[target].style.has_background,"CSS class selector");
    check(target>0&&doc.nodes[target].style.font_px==24,"CSS id selector");

    std::string dynamic_nodes="<body>";
    for(int i=0;i<3500;++i)dynamic_nodes+="<p>dynamic node</p>";
    dynamic_nodes+="</body>";
    browser_document_reset(&doc,4u,"https://example.com/");
    check(browser_document_parse(&doc,dynamic_nodes.data(),(uint32_t)dynamic_nodes.size())==0&&doc.node_count>DIHSCOVER_NODE_INITIAL,"DOM grows dynamically");

    const char scripted[]="<p id='result'>Old</p><script>document.getElementById('result').textContent='Changed';</script>";
    browser_document_reset(&doc,5u,"https://example.com/");
    check(browser_document_parse(&doc,scripted,sizeof(scripted)-1u)==0,"script fixture parse");
    check(browser_scripts_start(&doc,10u)>0,"JavaScript DOM update");
    target=find_id(doc,"result");
    check(target>0&&doc.nodes[target].first_child>=0&&b_streq(doc.text+doc.nodes[doc.nodes[target].first_child].text_off,"Changed"),"JavaScript textContent result");

    const char timer[]="<p id='later'>Wait</p><script>setTimeout(function(){document.querySelector('#later').style.color='red';},100);</script>";
    browser_document_reset(&doc,6u,"https://example.com/");
    check(browser_document_parse(&doc,timer,sizeof(timer)-1u)==0,"timer fixture parse");
    (void)browser_scripts_start(&doc,10u);
    check(browser_scripts_pump(&doc,200u,16u)>0,"JavaScript timer and style update");

    std::string stress;
    stress.reserve(600000u);
    stress="<html><head><style>";
    stress.append(300000u,'x');
    stress+="</style></head><body>";
    for(int i=0;i<5000;++i)stress+="<div data-json='a>b'><a href='/safe'>bounded link</a></div>";
    stress+="</body></html>";
    browser_document_reset(&doc,7u,"https://example.com/");
    check(browser_document_parse(&doc,stress.data(),(uint32_t)stress.size())==0,"large HTML parses safely");
    browser_document_layout(&doc,800u);
    check(doc.node_count<=DIHSCOVER_NODE_MAX&&doc.text_used<=DIHSCOVER_TEXT_MAX,"large HTML respects arenas");
    check(doc.truncated!=0u,"large HTML reports truncation");

    if(argc>1){
        std::ifstream in(argv[1],std::ios::binary);
        std::string fixture((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        check(!fixture.empty(),"Wikipedia fixture loaded");
        browser_document_reset(&doc,8u,"https://en.wikipedia.org/wiki/Wiki");
        check(browser_document_parse(&doc,fixture.data(),(uint32_t)fixture.size())==0,"Wikipedia parses safely");
        browser_document_layout(&doc,860u);
        check(doc.node_count<=DIHSCOVER_NODE_MAX&&doc.text_used<=DIHSCOVER_TEXT_MAX,"Wikipedia respects arenas");
    }

    std::printf(failures?"%d Dihscover tests failed\n":"Dihscover tests passed\n",failures);
    return failures?1:0;
}
