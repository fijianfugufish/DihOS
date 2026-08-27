#include "browser.h"

enum {
    SM_COLOR=1u<<0, SM_BACKGROUND=1u<<1, SM_FONT=1u<<2, SM_BOLD=1u<<3,
    SM_UNDERLINE=1u<<4, SM_DISPLAY=1u<<5, SM_ITALIC=1u<<6, SM_ALIGN=1u<<7
};

#define B_TEXT_SCALE_FP_FLAG 0x80000000u
#define B_TEXT_SCALE_FP_ONE 1024u

static const sacx_api *g_text_api;
static uint16_t g_glyph_width[73][128];
static uint32_t g_css_viewport=860u;
typedef struct css_variable{char name[48];char value[192];}css_variable;
static css_variable g_css_variables[128];
static uint32_t g_css_variable_count;

void browser_document_set_api(const sacx_api*api){g_text_api=api;b_memset(g_glyph_width,0,sizeof(g_glyph_width));}
uint32_t browser_text_scale(uint16_t font_px){uint32_t px=font_px?font_px:16u;uint32_t fp=(px*B_TEXT_SCALE_FP_ONE+8u)/16u;if(fp<256u)fp=256u;return B_TEXT_SCALE_FP_FLAG|fp;}
static uint32_t glyph_width(uint16_t font_px,unsigned char c){uint32_t px=font_px?font_px:16u;if(px>72u)px=72u;if(c>=128u)c='?';uint16_t*w=&g_glyph_width[px][c];if(!*w){char s[2]={(char)c,0};uint32_t scale=browser_text_scale((uint16_t)px);uint32_t v=g_text_api&&g_text_api->text_measure_line_px?g_text_api->text_measure_line_px(s,scale,0):((px+1u)/2u);if(!v)v=1u;*w=(uint16_t)(v>65535u?65535u:v);}return*w;}
uint32_t browser_text_wrap(const browser_style*style,const char*src,uint32_t width,char*out,uint32_t cap,uint32_t*out_width){uint32_t n=0,x=0,maxw=0,lines=1,line_start=0,last_space=0xffffffffu;if(out&&cap)out[0]=0;if(!src||!style||!width){if(out_width)*out_width=0;return 1;}while(*src&&(!out||n+2u<cap)){unsigned char c=(unsigned char)*src++;if(c=='\r')continue;if(c=='\n'){if(out)out[n]='\n';++n;if(x>maxw)maxw=x;x=0;line_start=n;last_space=0xffffffffu;++lines;continue;}uint32_t cw=glyph_width(style->font_px,c);if(x&&x+cw>width){if(last_space!=0xffffffffu&&last_space>=line_start&&last_space<n){if(out)out[last_space]='\n';++lines;x=0;for(uint32_t k=last_space+1u;k<n;++k)x+=glyph_width(style->font_px,(unsigned char)out[k]);line_start=last_space+1u;last_space=0xffffffffu;}else{if(out)out[n]='\n';++n;++lines;x=0;line_start=n;last_space=0xffffffffu;}}if(c==' ')last_space=n;if(out)out[n]=(char)c;++n;x+=cw;if(x>maxw)maxw=x;}if(out&&cap)out[n<cap?n:cap-1u]=0;if(out_width)*out_width=maxw;return lines;}

static sacx_color color(uint8_t r,uint8_t g,uint8_t b){sacx_color c={r,g,b};return c;}
static char lower(char c){return c>='A'&&c<='Z'?(char)(c+32):c;}
static int space(char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}
static int span_eq(const char*s,uint32_t n,const char*lit){uint32_t i=0;while(i<n&&lit[i]&&lower(s[i])==lower(lit[i]))++i;return i==n&&!lit[i];}
static int span_same(const char*a,uint32_t an,const char*b,uint32_t bn){if(an!=bn)return 0;for(uint32_t i=0;i<an;++i)if(lower(a[i])!=lower(b[i]))return 0;return 1;}
static uint32_t tag_hash(const char*s,uint32_t n){uint32_t h=2166136261u;for(uint32_t i=0;i<n;++i){h^=(uint8_t)lower(s[i]);h*=16777619u;}return h?h:1u;}

static browser_style default_style(uint16_t tag)
{
    browser_style s;b_memset(&s,0,sizeof(s));s.color=color(28,34,40);s.border_color=color(120,128,134);
    s.font_px=16;s.display=B_DISPLAY_BLOCK;s.margin_bottom=10;s.flex_shrink=1u;
    if(tag==B_TAG_DOCUMENT||tag==B_TAG_DIV||tag==B_TAG_NAV||tag==B_TAG_HEADER||tag==B_TAG_FOOTER||tag==B_TAG_FORM)s.margin_bottom=0;
    if(tag==B_TAG_SPAN||tag==B_TAG_A||tag==B_TAG_STRONG||tag==B_TAG_EM||tag==B_TAG_LABEL||tag==B_TAG_IMG||tag==B_TAG_SVG){s.display=B_DISPLAY_INLINE;s.margin_bottom=0;}
    if(tag==B_TAG_A){s.color=color(16,92,172);s.underline=1;s.mask|=SM_COLOR|SM_UNDERLINE;}
    if(tag==B_TAG_H1){s.font_px=32;s.bold=1;s.margin_top=14;s.margin_bottom=14;s.mask|=SM_FONT|SM_BOLD;}
    if(tag==B_TAG_H2){s.font_px=26;s.bold=1;s.margin_top=12;s.mask|=SM_FONT|SM_BOLD;}
    if(tag==B_TAG_H3){s.font_px=21;s.bold=1;s.margin_top=10;s.mask|=SM_FONT|SM_BOLD;}
    if(tag==B_TAG_H4){s.font_px=18;s.bold=1;s.margin_top=9;s.margin_bottom=7;s.mask|=SM_FONT|SM_BOLD;}
    if(tag==B_TAG_H5){s.font_px=16;s.bold=1;s.margin_top=8;s.margin_bottom=6;s.mask|=SM_FONT|SM_BOLD;}
    if(tag==B_TAG_H6){s.font_px=14;s.bold=1;s.margin_top=7;s.margin_bottom=5;s.mask|=SM_FONT|SM_BOLD;}
    if(tag==B_TAG_STRONG){s.bold=1;s.mask|=SM_BOLD;}if(tag==B_TAG_EM){s.italic=1;s.mask|=SM_ITALIC;}
    if(tag==B_TAG_CODE){s.display=B_DISPLAY_INLINE;s.margin_bottom=0;s.background=color(239,242,245);s.has_background=1;s.padding_left=s.padding_right=3;s.font_px=14;s.mask|=SM_FONT;}
    if(tag==B_TAG_BLOCKQUOTE){s.margin_left=s.margin_right=28;s.padding_left=12;s.border_width=1;s.border_color=color(180,186,192);}
    if(tag==B_TAG_FIGURE){s.margin_left=s.margin_right=12;s.margin_bottom=16;}
    if(tag==B_TAG_FIGCAPTION){s.font_px=14;s.text_align=1u;s.color=color(78,85,92);s.mask|=SM_FONT|SM_COLOR|SM_ALIGN;}
    if(tag==B_TAG_SMALL){s.display=B_DISPLAY_INLINE;s.margin_bottom=0;s.font_px=13;s.mask|=SM_FONT;}
    if(tag==B_TAG_LI)s.margin_left=20;
    if(tag==B_TAG_PRE){s.background=color(239,242,245);s.has_background=1;s.padding_left=s.padding_top=s.padding_bottom=8;}
    if(tag==B_TAG_BUTTON||tag==B_TAG_INPUT||tag==B_TAG_SELECT){s.display=B_DISPLAY_INLINE;s.background=color(242,244,246);s.has_background=1;s.padding_left=s.padding_right=10;s.padding_top=s.padding_bottom=6;s.border_width=1;s.height_px=34;s.margin_right=6;}
    if(tag==B_TAG_INPUT)s.width_px=220;
    if(tag==B_TAG_SELECT)s.width_px=180;
    if(tag==B_TAG_TR)s.display=B_DISPLAY_FLEX;
    if(tag==B_TAG_TD){s.padding_left=s.padding_right=6;s.border_width=1;}
    if(tag==B_TAG_SVG){s.width_px=120u;s.height_px=40u;}
    if(tag==B_TAG_STYLE||tag==B_TAG_SCRIPT||tag==B_TAG_TITLE||tag==B_TAG_HEAD||tag==B_TAG_META||tag==B_TAG_LINK||tag==B_TAG_TEMPLATE)s.display=B_DISPLAY_NONE;
    return s;
}

static int ensure_nodes(browser_document*d,uint32_t need)
{
    uint32_t cap;void*p;if(need<=d->node_capacity)return 0;if(need>DIHSCOVER_NODE_MAX)return -1;
    cap=d->node_capacity?d->node_capacity:DIHSCOVER_NODE_INITIAL;while(cap<need&&cap<DIHSCOVER_NODE_MAX)cap*=2;if(cap>DIHSCOVER_NODE_MAX)cap=DIHSCOVER_NODE_MAX;
    p=browser_heap_realloc(d->nodes,cap*sizeof(browser_node));if(!p)return -1;d->nodes=(browser_node*)p;d->node_capacity=cap;return 0;
}

static int ensure_text(browser_document*d,uint32_t need)
{
    uint32_t cap;void*p;if(need<=d->text_capacity)return 0;if(need>DIHSCOVER_TEXT_MAX)return -1;
    cap=d->text_capacity?d->text_capacity:DIHSCOVER_TEXT_INITIAL;while(cap<need&&cap<DIHSCOVER_TEXT_MAX)cap*=2;if(cap>DIHSCOVER_TEXT_MAX)cap=DIHSCOVER_TEXT_MAX;
    p=browser_heap_realloc(d->text,cap);if(!p)return -1;d->text=(char*)p;d->text_capacity=cap;return 0;
}

int browser_document_init(browser_document*d)
{
    if(!d)return -1;if(!d->nodes&&ensure_nodes(d,DIHSCOVER_NODE_INITIAL))return -1;if(!d->text&&ensure_text(d,DIHSCOVER_TEXT_INITIAL))return -1;return 0;
}
void browser_document_release(browser_document*d){if(!d)return;browser_heap_free(d->nodes);browser_heap_free(d->text);b_memset(d,0,sizeof(*d));}

static int decode_entity(const char*s,uint32_t n,uint32_t*used,char*out)
{
    struct named{const char*name;char value;};static const named names[]={{"amp;",'&'},{"lt;",'<'},{"gt;",'>'},{"quot;",'"'},{"apos;",'\''},{"nbsp;",' '},{"ndash;",'-'},{"mdash;",'-'},{"hellip;",'.'},{"lsquo;",'\''},{"rsquo;",'\''},{"ldquo;",'"'},{"rdquo;",'"'},{"copy;",'c'},{"reg;",'R'},{"trade;",'T'},{"bull;",'*'},{"middot;",'.'}};
    for(uint32_t k=0u;k<sizeof(names)/sizeof(names[0]);++k){uint32_t z=b_strlen(names[k].name);if(z<=n){uint32_t i=0u;while(i<z&&s[i]==names[k].name[i])++i;if(i==z){*used=z;*out=names[k].value;return 1;}}}
    if(n>2u&&s[0]=='#'){uint32_t i=1u,value=0u;int base=10;if(i<n&&(s[i]=='x'||s[i]=='X')){base=16;++i;}uint32_t digits=0u;while(i<n&&s[i]!=';'&&digits<8u){char c=s[i];int v=c>='0'&&c<='9'?c-'0':base==16&&c>='a'&&c<='f'?c-'a'+10:base==16&&c>='A'&&c<='F'?c-'A'+10:-1;if(v<0)return 0;value=value*(uint32_t)base+(uint32_t)v;++i;++digits;}if(i<n&&s[i]==';'&&digits){*used=i+1u;if(value==160u)*out=' ';else if(value==8211u||value==8212u)*out='-';else if(value==8216u||value==8217u)*out='\'';else if(value==8220u||value==8221u)*out='"';else *out=value<128u?(char)value:'?';return 1;}}
    return 0;
}

static uint32_t store(browser_document*d,const char*s,uint32_t n)
{
    uint32_t off,w=0;if(!d||!s||!n)return 0;if(n>DIHSCOVER_TEXT_MAX-2u||ensure_text(d,d->text_used+n+2u)){d->truncated=1;return 0;}off=d->text_used;
    while(w<n){char c=s[w++];if(c=='\r'||c=='\n'||c=='\t')c=' ';if(c=='&'){uint32_t used=0u;if(decode_entity(s+w,n-w,&used,&c))w+=used;}
        if(c==' '&&d->text_used>off&&d->text[d->text_used-1]==' ')continue;d->text[d->text_used++]=c;}
    while(d->text_used>off&&d->text[d->text_used-1]==' ')--d->text_used;d->text[d->text_used++]=0;return off;
}
static uint32_t store_raw_join(browser_document*d,uint32_t aoff,const char*b,uint32_t bn,const char*c)
{
    const char*fallback="<svg>";uint32_t an=aoff?b_strlen(d->text+aoff):b_strlen(fallback),cn=b_strlen(c);if(an+bn+cn>262144u||ensure_text(d,d->text_used+an+bn+cn+1u)){d->truncated=1u;return 0u;}const char*a=aoff?d->text+aoff:fallback;uint32_t off=d->text_used;b_memcpy(d->text+d->text_used,a,an);d->text_used+=an;b_memcpy(d->text+d->text_used,b,bn);d->text_used+=bn;b_memcpy(d->text+d->text_used,c,cn);d->text_used+=cn;d->text[d->text_used++]=0;return off;
}

static int add_node(browser_document*d,int32_t parent,uint16_t tag)
{
    int32_t idx;if(ensure_nodes(d,d->node_count+1u)){d->truncated=1;return -1;}idx=(int32_t)d->node_count++;browser_node*n=&d->nodes[idx];b_memset(n,0,sizeof(*n));
    n->parent=parent;n->first_child=n->last_child=n->next_sibling=-1;n->image_slot=-1;n->tag=tag;n->style=default_style(tag);
    if(parent>=0){int32_t child=d->nodes[parent].last_child;if(child<0)d->nodes[parent].first_child=idx;else d->nodes[child].next_sibling=idx;d->nodes[parent].last_child=idx;}return idx;
}

static uint16_t parse_tag(const char*s,uint32_t n)
{
    if(span_eq(s,n,"html")||span_eq(s,n,"body")||span_eq(s,n,"div")||span_eq(s,n,"section")||span_eq(s,n,"main")||span_eq(s,n,"article")||span_eq(s,n,"aside")||span_eq(s,n,"picture")||span_eq(s,n,"details")||span_eq(s,n,"summary")||span_eq(s,n,"fieldset")||span_eq(s,n,"dl")||span_eq(s,n,"dt")||span_eq(s,n,"dd")||span_eq(s,n,"center"))return B_TAG_DIV;
    if(span_eq(s,n,"head"))return B_TAG_HEAD;if(span_eq(s,n,"p"))return B_TAG_P;if(span_eq(s,n,"blockquote"))return B_TAG_BLOCKQUOTE;if(span_eq(s,n,"h1"))return B_TAG_H1;if(span_eq(s,n,"h2"))return B_TAG_H2;
    if(span_eq(s,n,"h3"))return B_TAG_H3;if(span_eq(s,n,"h4"))return B_TAG_H4;if(span_eq(s,n,"h5"))return B_TAG_H5;if(span_eq(s,n,"h6"))return B_TAG_H6;if(span_eq(s,n,"a"))return B_TAG_A;if(span_eq(s,n,"img"))return B_TAG_IMG;
    if(span_eq(s,n,"button"))return B_TAG_BUTTON;if(span_eq(s,n,"input")||span_eq(s,n,"textarea"))return B_TAG_INPUT;if(span_eq(s,n,"select"))return B_TAG_SELECT;if(span_eq(s,n,"li"))return B_TAG_LI;if(span_eq(s,n,"ul"))return B_TAG_UL;if(span_eq(s,n,"ol"))return B_TAG_OL;
    if(span_eq(s,n,"pre"))return B_TAG_PRE;if(span_eq(s,n,"code"))return B_TAG_CODE;if(span_eq(s,n,"figure"))return B_TAG_FIGURE;if(span_eq(s,n,"figcaption"))return B_TAG_FIGCAPTION;if(span_eq(s,n,"small"))return B_TAG_SMALL;if(span_eq(s,n,"strong")||span_eq(s,n,"b"))return B_TAG_STRONG;if(span_eq(s,n,"em")||span_eq(s,n,"i"))return B_TAG_EM;
    if(span_eq(s,n,"style"))return B_TAG_STYLE;if(span_eq(s,n,"script"))return B_TAG_SCRIPT;if(span_eq(s,n,"title"))return B_TAG_TITLE;if(span_eq(s,n,"br"))return B_TAG_BR;if(span_eq(s,n,"hr"))return B_TAG_HR;
    if(span_eq(s,n,"nav"))return B_TAG_NAV;if(span_eq(s,n,"header"))return B_TAG_HEADER;if(span_eq(s,n,"footer"))return B_TAG_FOOTER;if(span_eq(s,n,"form"))return B_TAG_FORM;if(span_eq(s,n,"label"))return B_TAG_LABEL;
    if(span_eq(s,n,"table"))return B_TAG_TABLE;if(span_eq(s,n,"tr"))return B_TAG_TR;if(span_eq(s,n,"td")||span_eq(s,n,"th"))return B_TAG_TD;if(span_eq(s,n,"meta"))return B_TAG_META;if(span_eq(s,n,"link"))return B_TAG_LINK;if(span_eq(s,n,"template"))return B_TAG_TEMPLATE;if(span_eq(s,n,"svg"))return B_TAG_SVG;return B_TAG_SPAN;
}

static int hex(char c){if(c>='0'&&c<='9')return c-'0';c=lower(c);return c>='a'&&c<='f'?c-'a'+10:-1;}
static uint32_t color_component(const char*s,uint32_t n,uint32_t*p){uint32_t v=0;while(*p<n&&space(s[*p]))++*p;while(*p<n&&s[*p]>='0'&&s[*p]<='9'){v=v*10u+(uint32_t)(s[(*p)++]-'0');if(v>255u)v=255u;}while(*p<n&&s[*p]!=','&&s[*p]!=')')++*p;if(*p<n&&s[*p]==',')++*p;return v;}
static int named_color(const char*v,uint32_t n,sacx_color*out)
{
    while(n&&space(*v)){++v;--n;}while(n&&space(v[n-1]))--n;
    if((n==7||n==9)&&v[0]=='#'){int a=hex(v[1]),b=hex(v[2]),c=hex(v[3]),d=hex(v[4]),e=hex(v[5]),f=hex(v[6]);if(a<0||b<0||c<0||d<0||e<0||f<0)return -1;*out=color((uint8_t)(a*16+b),(uint8_t)(c*16+d),(uint8_t)(e*16+f));return 0;}
    if((n==4||n==5)&&v[0]=='#'){int a=hex(v[1]),b=hex(v[2]),c=hex(v[3]);if(a<0||b<0||c<0)return -1;*out=color((uint8_t)(a*17),(uint8_t)(b*17),(uint8_t)(c*17));return 0;}
    if((n>5&&span_same(v,4,"rgb(",4))||(n>6&&span_same(v,5,"rgba(",5))){uint32_t p=v[3]=='('?4u:5u;uint32_t r=color_component(v,n,&p),g=color_component(v,n,&p),b=color_component(v,n,&p);*out=color((uint8_t)r,(uint8_t)g,(uint8_t)b);return 0;}
    if(span_eq(v,n,"red"))*out=color(190,35,42);else if(span_eq(v,n,"blue"))*out=color(20,80,190);else if(span_eq(v,n,"green"))*out=color(22,128,65);else if(span_eq(v,n,"white"))*out=color(255,255,255);else if(span_eq(v,n,"black"))*out=color(0,0,0);else if(span_eq(v,n,"gray")||span_eq(v,n,"grey"))*out=color(110,115,120);else if(span_eq(v,n,"yellow"))*out=color(225,190,35);else if(span_eq(v,n,"orange"))*out=color(220,120,30);else if(span_eq(v,n,"purple"))*out=color(125,65,160);else if(span_eq(v,n,"navy"))*out=color(0,0,128);else if(span_eq(v,n,"teal"))*out=color(0,128,128);else if(span_eq(v,n,"aqua")||span_eq(v,n,"cyan"))*out=color(0,180,190);else if(span_eq(v,n,"lime"))*out=color(0,190,70);else if(span_eq(v,n,"maroon"))*out=color(128,0,0);else if(span_eq(v,n,"silver"))*out=color(192,192,192);else if(span_eq(v,n,"fuchsia")||span_eq(v,n,"magenta"))*out=color(200,0,180);else if(span_eq(v,n,"whitesmoke"))*out=color(245,245,245);else if(span_eq(v,n,"transparent"))*out=color(0,0,0);else return -1;return 0;
}
static uint32_t number_px(const char*s,uint32_t n){uint32_t v=0,i=0;while(i<n&&space(s[i]))++i;while(i<n&&s[i]>='0'&&s[i]<='9'){if(v<100000)v=v*10+(uint32_t)(s[i]-'0');++i;}return v;}
static uint8_t percent_value(const char*s,uint32_t n){uint32_t v=number_px(s,n);for(uint32_t i=0;i<n;++i)if(s[i]=='%')return (uint8_t)(v>100u?100u:v);return 0u;}
static int span_contains_ci(const char*s,uint32_t n,const char*needle);
static uint32_t decimal_1000(const char*s,uint32_t n,uint32_t*unit){uint32_t i=0,whole=0,frac=0,mul=100;while(i<n&&space(s[i]))++i;if(i<n&&s[i]=='-')++i;while(i<n&&s[i]>='0'&&s[i]<='9'){if(whole<100000u)whole=whole*10u+(uint32_t)(s[i]-'0');++i;}if(i<n&&s[i]=='.'){++i;while(i<n&&s[i]>='0'&&s[i]<='9'&&mul){frac+=(uint32_t)(s[i]-'0')*mul;mul/=10u;++i;}while(i<n&&s[i]>='0'&&s[i]<='9')++i;}while(i<n&&space(s[i]))++i;if(unit)*unit=i;return whole*1000u+frac;}
static uint32_t css_length(const browser_style*st,const char*s,uint32_t n,uint32_t relative)
{
    while(n&&space(*s)){++s;--n;}while(n&&space(s[n-1]))--n;
    if(n>6u&&span_same(s,5u,"calc(",5u)){int64_t total=0;uint32_t p=5u;int sign=1;while(p<n&&s[p]!=')'){while(p<n&&space(s[p]))++p;if(p<n&&(s[p]=='+'||s[p]=='-')){sign=s[p++]=='-'?-1:1;while(p<n&&space(s[p]))++p;}uint32_t a=p,depth=0u;while(p<n){char c=s[p];if(c=='(')++depth;else if(c==')'){if(!depth)break;--depth;}else if(!depth&&(c=='+'||c=='-'))break;++p;}uint32_t part=css_length(st,s+a,p-a,relative);total+=sign*(int64_t)part;sign=1;}return total>0?(uint32_t)(total>65535?65535:total):0u;}
    if(n>5u&&(span_same(s,4u,"min(",4u)||span_same(s,4u,"max(",4u))){int want_max=lower(s[1])=='a';uint32_t best=want_max?0u:0xffffffffu,p=4u;while(p<n&&s[p]!=')'){uint32_t a=p,depth=0u;while(p<n){char c=s[p];if(c=='(')++depth;else if(c==')'){if(!depth)break;--depth;}else if(c==','&&!depth)break;++p;}uint32_t part=css_length(st,s+a,p-a,relative);if(want_max?(part>best):(part<best))best=part;if(p<n&&s[p]==',')++p;}return best==0xffffffffu?0u:best;}
    if(n>7u&&span_same(s,6u,"clamp(",6u)){uint32_t values[3]={0,0,0},count=0u,p=6u;while(p<n&&s[p]!=')'&&count<3u){uint32_t a=p,depth=0u;while(p<n){char c=s[p];if(c=='(')++depth;else if(c==')'){if(!depth)break;--depth;}else if(c==','&&!depth)break;++p;}values[count++]=css_length(st,s+a,p-a,relative);if(p<n&&s[p]==',')++p;}if(count==3u)return values[1]<values[0]?values[0]:values[1]>values[2]?values[2]:values[1];}
    uint32_t u=0,v=decimal_1000(s,n,&u);if(u<n&&s[u]=='%')return (relative*v+50000u)/100000u;if(u+1u<n&&lower(s[u])=='e'&&lower(s[u+1])=='m')return ((st->font_px?st->font_px:16u)*v+500u)/1000u;if(u+2u<n&&lower(s[u])=='r'&&lower(s[u+1])=='e'&&lower(s[u+2])=='m')return (16u*v+500u)/1000u;if(u+1u<n&&lower(s[u])=='v'&&(lower(s[u+1])=='w'||lower(s[u+1])=='h'))return (g_css_viewport*v+50000u)/100000u;if(u+1u<n&&lower(s[u])=='c'&&lower(s[u+1])=='h')return (((st->font_px?st->font_px:16u)/2u)*v+500u)/1000u;return (v+500u)/1000u;
}
static int32_t css_signed_length(const browser_style*st,const char*s,uint32_t n,uint32_t relative){uint32_t p=0u;while(p<n&&space(s[p]))++p;int sign=1;if(p<n&&s[p]=='-'){sign=-1;++p;}else if(p<n&&s[p]=='+')++p;uint32_t v=css_length(st,s+p,n-p,relative);return sign*(int32_t)(v>32767u?32767u:v);}
static uint32_t box_lengths(const browser_style*st,const char*s,uint32_t n,uint16_t out[4],uint8_t autos[4])
{
    uint16_t values[4]={0,0,0,0};uint8_t is_auto[4]={0,0,0,0};uint32_t count=0u,p=0u;while(p<n&&count<4u){while(p<n&&space(s[p]))++p;if(p>=n)break;uint32_t a=p,paren=0u;while(p<n&&(!space(s[p])||paren)){if(s[p]=='(')++paren;else if(s[p]==')'&&paren)--paren;++p;}uint32_t len=p-a;if(span_eq(s+a,len,"auto"))is_auto[count]=1u;else{uint32_t v=css_length(st,s+a,len,st->font_px?st->font_px:16u);values[count]=(uint16_t)(v>256u?256u:v);}++count;}
    if(!count)return 0u;uint32_t map[4]={0u,count==1u?0u:1u,count<3u?0u:2u,count<2u?0u:(count<4u?1u:3u)};for(uint32_t i=0u;i<4u;++i){out[i]=values[map[i]];autos[i]=is_auto[map[i]];}return count;
}
static int declaration_color(const char*s,uint32_t n,sacx_color*out){if(!named_color(s,n,out))return 0;for(uint32_t i=0;i<n;++i)if(s[i]=='#'||(i+4u<n&&lower(s[i])=='r'&&lower(s[i+1])=='g'&&lower(s[i+2])=='b'&&s[i+3]=='('))if(!named_color(s+i,n-i,out))return 0;return -1;}
static int transparent_value(const char*s,uint32_t n){while(n&&space(*s)){++s;--n;}while(n&&space(s[n-1]))--n;if(span_eq(s,n,"transparent"))return 1;if((n==5u||n==9u)&&s[0]=='#'&&s[n-2]=='0'&&s[n-1]=='0')return 1;if(n>6u&&span_same(s,5u,"rgba(",5u)){uint32_t commas=0u;for(uint32_t i=5u;i<n;++i)if(s[i]==','&&++commas==3u)return decimal_1000(s+i+1u,n-i-1u,0)==0u;}return 0;}
static int starts_number(const char*s,uint32_t n){uint32_t i=0;while(i<n&&space(s[i]))++i;return i<n&&((s[i]>='0'&&s[i]<='9')||s[i]=='.'||s[i]=='-');}
static const char*css_variable_value(const char*name,uint32_t n){for(uint32_t i=g_css_variable_count;i>0u;--i)if(span_same(g_css_variables[i-1u].name,b_strlen(g_css_variables[i-1u].name),name,n))return g_css_variables[i-1u].value;return 0;}
static int resolve_css_value_depth(const char*s,uint32_t n,char*out,uint32_t cap,uint32_t depth)
{
    uint32_t p=0u,w=0u;if(!out||!cap||depth>6u)return -1;while(p<n&&w+1u<cap){if(p+4u<=n&&lower(s[p])=='v'&&lower(s[p+1])=='a'&&lower(s[p+2])=='r'&&s[p+3]=='('){uint32_t q=p+4u;while(q<n&&space(s[q]))++q;uint32_t name=q;while(q<n&&s[q]!=','&&s[q]!=')'&&!space(s[q]))++q;uint32_t name_end=q;while(q<n&&space(s[q]))++q;uint32_t fallback=0u,fallback_end=0u;if(q<n&&s[q]==','){fallback=++q;uint32_t paren=1u;char quote=0;while(q<n&&paren){char c=s[q];if(quote){if(c=='\\'&&q+1u<n)++q;else if(c==quote)quote=0;}else if(c=='\''||c=='"')quote=c;else if(c=='(')++paren;else if(c==')')--paren;if(paren)++q;}fallback_end=q;}else while(q<n&&s[q]!=')')++q;if(q>=n)return -1;const char*value=css_variable_value(s+name,name_end-name);char nested[384];if(value){if(resolve_css_value_depth(value,b_strlen(value),nested,sizeof(nested),depth+1u)==0)value=nested;}else if(fallback){while(fallback<fallback_end&&space(s[fallback]))++fallback;while(fallback_end>fallback&&space(s[fallback_end-1u]))--fallback;if(resolve_css_value_depth(s+fallback,fallback_end-fallback,nested,sizeof(nested),depth+1u)!=0)return -1;value=nested;}else return -1;for(uint32_t i=0u;value[i]&&w+1u<cap;++i)out[w++]=value[i];p=q+1u;continue;}out[w++]=s[p++];}out[w]=0;return p==n?0:-1;
}
static void collect_css_variables(const char*s,uint32_t n)
{
    uint32_t p=0u;while(p+3u<n){if(s[p]!='-'||s[p+1]!='-'){++p;continue;}uint32_t a=p;p+=2u;while(p<n&&((s[p]>='a'&&s[p]<='z')||(s[p]>='A'&&s[p]<='Z')||(s[p]>='0'&&s[p]<='9')||s[p]=='-'||s[p]=='_'))++p;uint32_t b=p;while(p<n&&space(s[p]))++p;if(p>=n||s[p]!=':'||b-a>=sizeof(g_css_variables[0].name)){p=a+2u;continue;}uint32_t v=++p,paren=0u;char quote=0;while(p<n){char c=s[p];if(quote){if(c=='\\'&&p+1u<n)++p;else if(c==quote)quote=0;}else if(c=='\''||c=='"')quote=c;else if(c=='(')++paren;else if(c==')'&&paren)--paren;else if(!paren&&(c==';'||c=='}'))break;++p;}uint32_t e=p;while(v<e&&space(s[v]))++v;while(e>v&&space(s[e-1u]))--e;if(e>v){uint32_t slot=g_css_variable_count<128u?g_css_variable_count++:127u;b_copy_n(g_css_variables[slot].name,sizeof(g_css_variables[slot].name),s+a,b-a);b_copy_n(g_css_variables[slot].value,sizeof(g_css_variables[slot].value),s+v,e-v);}if(p<n)++p;}
}
static void parse_declarations(browser_style*st,const char*s,uint32_t n)
{
    uint32_t p=0;while(p<n){uint32_t a,b,v,e,num;while(p<n&&(space(s[p])||s[p]==';'))++p;a=p;while(p<n&&s[p]!=':'&&s[p]!=';')++p;b=p;while(b>a&&space(s[b-1]))--b;if(p>=n||s[p++]!=':')continue;while(p<n&&space(s[p]))++p;v=p;char quote=0;uint32_t paren=0;while(p<n){char c=s[p];if(quote){if(c=='\\'&&p+1u<n)++p;else if(c==quote)quote=0;}else if(c=='\''||c=='\"')quote=c;else if(c=='(')++paren;else if(c==')'&&paren)--paren;else if(!paren&&(c==';'||c=='}'))break;++p;}e=p;while(e>v&&space(s[e-1]))--e;if(span_contains_ci(s+v,e-v,"var(")){char value[384],decl[480];if(resolve_css_value_depth(s+v,e-v,value,sizeof(value),0u)==0){uint32_t w=0u;for(uint32_t i=a;i<b&&w+1u<sizeof(decl);++i)decl[w++]=s[i];if(w+1u<sizeof(decl))decl[w++]=':';for(uint32_t i=0u;value[i]&&w+1u<sizeof(decl);++i)decl[w++]=value[i];decl[w]=0;parse_declarations(st,decl,w);}continue;}num=css_length(st,s+v,e-v,st->font_px?st->font_px:16u);
        if(span_eq(s+a,b-a,"color")){if(!declaration_color(s+v,e-v,&st->color))st->mask|=SM_COLOR;}
        else if(span_eq(s+a,b-a,"background")||span_eq(s+a,b-a,"background-color")){if(transparent_value(s+v,e-v)){st->has_background=0;st->mask|=SM_BACKGROUND;}else if(!declaration_color(s+v,e-v,&st->background)){st->has_background=1;st->mask|=SM_BACKGROUND;}}
        else if(span_eq(s+a,b-a,"font-size")&&num){st->font_px=(uint16_t)(num>72?72:(num<6?6:num));st->mask|=SM_FONT;}
        else if(span_eq(s+a,b-a,"line-height")){uint32_t unit=0,raw=decimal_1000(s+v,e-v,&unit);if(unit==e-v&&raw<=4000u)num=((st->font_px?st->font_px:16u)*raw+500u)/1000u;if(num)st->line_height_px=(uint16_t)(num>128?128:num);}
        else if(span_eq(s+a,b-a,"font-weight")){st->bold=(uint8_t)(span_eq(s+v,e-v,"bold")||num>=600);st->mask|=SM_BOLD;}
        else if(span_eq(s+a,b-a,"font-style")){st->italic=(uint8_t)span_eq(s+v,e-v,"italic");st->mask|=SM_ITALIC;}
        else if(span_eq(s+a,b-a,"text-decoration")){st->underline=(uint8_t)(span_eq(s+v,e-v,"underline")||b_starts(s+v,"underline "));st->mask|=SM_UNDERLINE;}
        else if(span_eq(s+a,b-a,"text-align")){st->text_align=span_eq(s+v,e-v,"center")?1u:span_eq(s+v,e-v,"right")?2u:0u;st->mask|=SM_ALIGN;}
        else if(span_eq(s+a,b-a,"text-transform"))st->text_transform=span_eq(s+v,e-v,"uppercase")?1u:span_eq(s+v,e-v,"lowercase")?2u:span_eq(s+v,e-v,"capitalize")?3u:0u;
        else if(span_eq(s+a,b-a,"display")){st->grid_layout=(uint8_t)(span_eq(s+v,e-v,"grid")||span_eq(s+v,e-v,"inline-grid"));st->display=span_eq(s+v,e-v,"none")?B_DISPLAY_NONE:(span_eq(s+v,e-v,"flex")||span_eq(s+v,e-v,"inline-flex")||st->grid_layout||span_eq(s+v,e-v,"table-row"))?B_DISPLAY_FLEX:(span_eq(s+v,e-v,"inline")||span_eq(s+v,e-v,"inline-block")||span_eq(s+v,e-v,"table-cell")||span_eq(s+v,e-v,"contents"))?B_DISPLAY_INLINE:B_DISPLAY_BLOCK;if(st->grid_layout)st->flex_wrap=1u;st->mask|=SM_DISPLAY;}
        else if(span_eq(s+a,b-a,"visibility")&&span_eq(s+v,e-v,"hidden")){st->display=B_DISPLAY_NONE;st->mask|=SM_DISPLAY;}
        else if(span_eq(s+a,b-a,"opacity")){/* Keep server-rendered content visible without full JavaScript transitions. */}
        else if(span_eq(s+a,b-a,"position"))st->position=span_eq(s+v,e-v,"fixed")?3u:span_eq(s+v,e-v,"absolute")?2u:span_eq(s+v,e-v,"relative")?1u:0u;
        else if(span_eq(s+a,b-a,"top")){st->inset_top=(int16_t)css_signed_length(st,s+v,e-v,g_css_viewport);st->inset_mask|=1u;}
        else if(span_eq(s+a,b-a,"right")){st->inset_right=(int16_t)css_signed_length(st,s+v,e-v,g_css_viewport);st->inset_mask|=2u;}
        else if(span_eq(s+a,b-a,"bottom")){st->inset_bottom=(int16_t)css_signed_length(st,s+v,e-v,g_css_viewport);st->inset_mask|=4u;}
        else if(span_eq(s+a,b-a,"left")){st->inset_left=(int16_t)css_signed_length(st,s+v,e-v,g_css_viewport);st->inset_mask|=8u;}
        else if(span_eq(s+a,b-a,"z-index")){int32_t z=css_signed_length(st,s+v,e-v,1u);st->z_index=(int16_t)(z>64?64:z<-64?-64:z);}
        else if(span_eq(s+a,b-a,"float"))st->float_side=span_eq(s+v,e-v,"left")?1u:span_eq(s+v,e-v,"right")?2u:0u;
        else if(span_eq(s+a,b-a,"clear"))st->clear_float=(uint8_t)!span_eq(s+v,e-v,"none");
        else if(span_eq(s+a,b-a,"object-fit"))st->object_fit=(uint8_t)(span_eq(s+v,e-v,"cover")?2u:(span_eq(s+v,e-v,"contain")||span_eq(s+v,e-v,"scale-down"))?1u:0u);
        else if(span_eq(s+a,b-a,"aspect-ratio")){uint32_t slash=0u;while(slash<e-v&&s[v+slash]!='/')++slash;uint32_t an=number_px(s+v,slash),ad=slash<e-v?number_px(s+v+slash+1u,e-v-slash-1u):1u;if(an&&ad){st->aspect_num=(uint8_t)(an>32u?32u:an);st->aspect_den=(uint8_t)(ad>32u?32u:ad);}}
        else if(span_eq(s+a,b-a,"overflow"))st->overflow_hidden=(uint8_t)(span_eq(s+v,e-v,"hidden")||span_eq(s+v,e-v,"clip"));
        else if(span_eq(s+a,b-a,"white-space"))st->nowrap=(uint8_t)(span_eq(s+v,e-v,"nowrap")||span_eq(s+v,e-v,"pre"));
        else if(span_eq(s+a,b-a,"flex-direction"))st->flex_direction=(uint8_t)(span_eq(s+v,e-v,"column")?1:0);
        else if(span_eq(s+a,b-a,"flex-grow"))st->flex_grow=(uint8_t)(num>16u?16u:num);
        else if(span_eq(s+a,b-a,"flex-shrink"))st->flex_shrink=(uint8_t)(num>16u?16u:num);
        else if(span_eq(s+a,b-a,"flex-basis"))st->flex_basis_px=(uint16_t)(num>4096u?4096u:num);
        else if(span_eq(s+a,b-a,"flex")){st->flex_grow=(uint8_t)(num>16u?16u:num);if(span_contains_ci(s+v,e-v,"auto"))st->flex_grow=1u;}
        else if(span_eq(s+a,b-a,"grid-template-columns")){
            uint32_t columns=0u;
            if(span_contains_ci(s+v,e-v,"auto-fit")||span_contains_ci(s+v,e-v,"auto-fill"))columns=0u;
            else if(e-v>=7u&&span_same(s+v,7u,"repeat(",7u))columns=number_px(s+v+7u,e-v-7u);
            else{
                uint32_t q=0u;
                while(q<e-v){
                    while(q<e-v&&space(s[v+q]))++q;
                    if(q>=e-v)break;
                    ++columns;uint32_t depth=0u;
                    while(q<e-v){char c=s[v+q];if(c=='(')++depth;else if(c==')'&&depth)--depth;if(!depth&&space(c))break;++q;}
                }
            }
            st->grid_columns=(uint8_t)(columns>12u?12u:columns);st->grid_layout=1u;st->display=B_DISPLAY_FLEX;st->flex_wrap=1u;
        }
        else if(span_eq(s+a,b-a,"flex-wrap"))st->flex_wrap=(uint8_t)!span_eq(s+v,e-v,"nowrap");
        else if(span_eq(s+a,b-a,"justify-content"))st->justify_content=span_eq(s+v,e-v,"center")?1u:(span_eq(s+v,e-v,"flex-end")||span_eq(s+v,e-v,"end"))?2u:span_eq(s+v,e-v,"space-between")?3u:0u;
        else if(span_eq(s+a,b-a,"align-items"))st->align_items=span_eq(s+v,e-v,"center")?1u:(span_eq(s+v,e-v,"flex-end")||span_eq(s+v,e-v,"end"))?2u:0u;
        else if(span_eq(s+a,b-a,"gap")||span_eq(s+a,b-a,"column-gap")||span_eq(s+a,b-a,"row-gap"))st->gap_px=(uint16_t)(num>128?128:num);
        else if(span_eq(s+a,b-a,"margin")){uint16_t q[4];uint8_t au[4];if(box_lengths(st,s+v,e-v,q,au)){st->margin_top=q[0];st->margin_right=q[1];st->margin_bottom=q[2];st->margin_left=q[3];st->margin_auto_right=au[1];st->margin_auto_left=au[3];}}
        else if(span_eq(s+a,b-a,"padding")){uint16_t q[4];uint8_t au[4];if(box_lengths(st,s+v,e-v,q,au)){st->padding_top=q[0];st->padding_right=q[1];st->padding_bottom=q[2];st->padding_left=q[3];}}
        else if(span_eq(s+a,b-a,"margin-top"))st->margin_top=(uint16_t)num;else if(span_eq(s+a,b-a,"margin-right")){st->margin_right=(uint16_t)num;st->margin_auto_right=(uint8_t)span_eq(s+v,e-v,"auto");}else if(span_eq(s+a,b-a,"margin-bottom"))st->margin_bottom=(uint16_t)num;else if(span_eq(s+a,b-a,"margin-left")){st->margin_left=(uint16_t)num;st->margin_auto_left=(uint8_t)span_eq(s+v,e-v,"auto");}
        else if(span_eq(s+a,b-a,"padding-top"))st->padding_top=(uint16_t)num;else if(span_eq(s+a,b-a,"padding-right"))st->padding_right=(uint16_t)num;else if(span_eq(s+a,b-a,"padding-bottom"))st->padding_bottom=(uint16_t)num;else if(span_eq(s+a,b-a,"padding-left"))st->padding_left=(uint16_t)num;
        else if(span_eq(s+a,b-a,"width")){uint8_t pc=percent_value(s+v,e-v);st->width_percent=pc;if(!pc)st->width_px=(uint16_t)(num>4096?4096:num);}
        else if(span_eq(s+a,b-a,"max-width")){uint8_t pc=percent_value(s+v,e-v);st->max_width_percent=pc;if(!pc)st->max_width_px=(uint16_t)(num>4096?4096:num);}
        else if(span_eq(s+a,b-a,"min-width"))st->min_width_px=(uint16_t)(num>4096?4096:num);
        else if(span_eq(s+a,b-a,"height"))st->height_px=(uint16_t)(num>4096?4096:num);
        else if(span_eq(s+a,b-a,"min-height"))st->min_height_px=(uint16_t)(num>4096?4096:num);
        else if(span_eq(s+a,b-a,"max-height"))st->max_height_px=(uint16_t)(num>4096?4096:num);
        else if(span_eq(s+a,b-a,"border-width"))st->border_width=(uint16_t)(num>16?16:num);else if(span_eq(s+a,b-a,"border-color"))(void)declaration_color(s+v,e-v,&st->border_color);else if(span_eq(s+a,b-a,"border")||span_eq(s+a,b-a,"border-top")||span_eq(s+a,b-a,"border-right")||span_eq(s+a,b-a,"border-bottom")||span_eq(s+a,b-a,"border-left")){if(span_contains_ci(s+v,e-v,"none")||(starts_number(s+v,e-v)&&num==0u))st->border_width=0u;else{st->border_width=(uint16_t)(num?num:1u);(void)declaration_color(s+v,e-v,&st->border_color);}}
    }
    if(st->position==2u&&st->overflow_hidden&&st->width_px<=2u&&st->height_px<=2u){st->display=B_DISPLAY_NONE;st->mask|=SM_DISPLAY;}
}

static void parse_attrs(browser_document*d,browser_node*n,const char*s,uint32_t len)
{
    uint32_t p=0;while(p<len){uint32_t a,b,v=0,e=0;char q=0;while(p<len&&(space(s[p])||s[p]=='/'))++p;a=p;while(p<len&&s[p]!='='&&!space(s[p])&&s[p]!='/')++p;b=p;while(p<len&&space(s[p]))++p;
        if(p>=len||s[p]!='='){if(span_eq(s+a,b-a,"checked"))n->flags|=4u;else if(span_eq(s+a,b-a,"hidden"))n->flags|=2u;else if(span_eq(s+a,b-a,"disabled"))n->flags|=32u;while(p<len&&!space(s[p]))++p;continue;}
        ++p;while(p<len&&space(s[p]))++p;if(p<len&&(s[p]=='\''||s[p]=='"'))q=s[p++];v=p;while(p<len&&((q&&s[p]!=q)||(!q&&!space(s[p]))))++p;e=p;if(q&&p<len)++p;
        if(span_eq(s+a,b-a,"href"))n->href_off=store(d,s+v,e-v);else if(span_eq(s+a,b-a,"src"))n->src_off=store(d,s+v,e-v);else if(span_eq(s+a,b-a,"id"))n->id_off=store(d,s+v,e-v);else if(span_eq(s+a,b-a,"class"))n->class_off=store(d,s+v,e-v);else if(span_eq(s+a,b-a,"rel"))n->rel_off=store(d,s+v,e-v);else if(span_eq(s+a,b-a,"onclick"))n->onclick_off=store(d,s+v,e-v);else if(span_eq(s+a,b-a,"style"))n->inline_style_off=store(d,s+v,e-v);else if(span_eq(s+a,b-a,"name"))n->name_off=store(d,s+v,e-v);else if(span_eq(s+a,b-a,"value"))n->value_off=store(d,s+v,e-v);else if(span_eq(s+a,b-a,"action"))n->action_off=store(d,s+v,e-v);else if(span_eq(s+a,b-a,"method"))n->method_off=store(d,s+v,e-v);else if(span_eq(s+a,b-a,"for"))n->for_off=store(d,s+v,e-v);else if(span_eq(s+a,b-a,"width")){n->attr_width=(uint16_t)number_px(s+v,e-v);n->style.width_px=n->attr_width;}else if(span_eq(s+a,b-a,"height")){n->attr_height=(uint16_t)number_px(s+v,e-v);n->style.height_px=n->attr_height;}else if(span_eq(s+a,b-a,"placeholder")&&n->tag==B_TAG_INPUT&&!n->text_off){n->text_off=store(d,s+v,e-v);n->text_len=b_strlen(d->text+n->text_off);}else if(span_eq(s+a,b-a,"type")){n->type_off=store(d,s+v,e-v);if(span_eq(s+v,e-v,"hidden"))n->flags|=2u;else if(span_eq(s+v,e-v,"checkbox")||span_eq(s+v,e-v,"radio"))n->flags|=8u;else if(span_eq(s+v,e-v,"submit")||span_eq(s+v,e-v,"image"))n->flags|=16u;}}
    if(n->tag==B_TAG_BUTTON&&!(n->type_off&&b_streq(d->text+n->type_off,"button")))n->flags|=16u;
}

static int class_has(const char*s,const char*w,uint32_t wn){if(!s)return 0;while(*s){while(*s&&space(*s))++s;const char*a=s;while(*s&&!space(*s))++s;if(span_same(a,(uint32_t)(s-a),w,wn))return 1;}return 0;}
static const char*tag_name(uint16_t t){static const char*names[]={"document","div","p","h1","h2","h3","a","span","img","button","input","li","pre","style","script","title","br","ul","ol","strong","em","nav","header","footer","form","label","table","tr","td","hr","head","meta","link","template","svg","select","h4","h5","h6","code","blockquote","figure","figcaption","small"};return t<sizeof(names)/sizeof(names[0])?names[t]:"span";}
static const char*node_attr(const browser_document*d,const browser_node*x,const char*name,uint32_t n,int*present)
{
    uint32_t off=0u;*present=0;
    if(span_eq(name,n,"href"))off=x->href_off;else if(span_eq(name,n,"src"))off=x->src_off;else if(span_eq(name,n,"id"))off=x->id_off;else if(span_eq(name,n,"class"))off=x->class_off;else if(span_eq(name,n,"rel"))off=x->rel_off;else if(span_eq(name,n,"name"))off=x->name_off;else if(span_eq(name,n,"value"))off=x->value_off;else if(span_eq(name,n,"type"))off=x->type_off;else if(span_eq(name,n,"action"))off=x->action_off;else if(span_eq(name,n,"method"))off=x->method_off;else if(span_eq(name,n,"for"))off=x->for_off;else if(span_eq(name,n,"hidden")){*present=(x->flags&2u)!=0u;return "";}else if(span_eq(name,n,"checked")){*present=(x->flags&4u)!=0u;return "";}else return "";
    *present=off!=0u;return off?d->text+off:"";
}
static int simple_match(const browser_document*d,uint32_t idx,const char*s,uint32_t n);
static int pseudo_match(const browser_document*d,uint32_t idx,const char*s,uint32_t n,uint32_t*used)
{
    const browser_node*x=&d->nodes[idx];uint32_t p=0u;while(p<n&&((s[p]>='a'&&s[p]<='z')||(s[p]>='A'&&s[p]<='Z')||s[p]=='-'))++p;uint32_t name=p,open=p;
    if(open<n&&s[open]=='('){uint32_t depth=1u;char quote=0;++p;uint32_t body=p;while(p<n&&depth){char c=s[p];if(quote){if(c=='\\'&&p+1u<n)++p;else if(c==quote)quote=0;}else if(c=='\''||c=='\"')quote=c;else if(c=='(')++depth;else if(c==')')--depth;if(depth)++p;}if(depth)return 0;*used=p+1u;uint32_t body_n=p-body;
        if(span_eq(s,name,"not"))return !simple_match(d,idx,s+body,body_n);
        if(span_eq(s,name,"is")||span_eq(s,name,"where")){uint32_t a=body;while(a<p){uint32_t e=a,sub=0;while(e<p){if(s[e]=='(')++sub;else if(s[e]==')'&&sub)--sub;else if(s[e]==','&&!sub)break;++e;}while(a<e&&space(s[a]))++a;while(e>a&&space(s[e-1]))--e;if(simple_match(d,idx,s+a,e-a))return 1;a=e+1u;}return 0;}
        if(span_eq(s,name,"nth-child")){uint32_t ordinal=1u;for(int32_t c=x->parent>=0?d->nodes[x->parent].first_child:-1;c>=0&&c!=(int32_t)idx;c=d->nodes[c].next_sibling)if(!d->nodes[c].text_off)++ordinal;if(span_eq(s+body,body_n,"odd"))return (ordinal&1u)!=0u;if(span_eq(s+body,body_n,"even"))return (ordinal&1u)==0u;return ordinal==number_px(s+body,body_n);}
        return 0;
    }
    *used=p;
    if(span_eq(s,name,"root"))return x->parent==0;
    if(span_eq(s,name,"link")||span_eq(s,name,"any-link"))return x->href_off!=0u;
    if(span_eq(s,name,"checked"))return (x->flags&4u)!=0u;
    if(span_eq(s,name,"disabled"))return (x->flags&32u)!=0u;
    if(span_eq(s,name,"enabled"))return (x->flags&32u)==0u;
    if(span_eq(s,name,"empty"))return x->first_child<0&&!x->text_off;
    if(span_eq(s,name,"first-child")||span_eq(s,name,"last-child")||span_eq(s,name,"only-child")){int32_t first=-1,last=-1;if(x->parent>=0)for(int32_t c=d->nodes[x->parent].first_child;c>=0;c=d->nodes[c].next_sibling)if(!d->nodes[c].text_off){if(first<0)first=c;last=c;}if(span_eq(s,name,"first-child"))return first==(int32_t)idx;if(span_eq(s,name,"last-child"))return last==(int32_t)idx;return first==(int32_t)idx&&last==(int32_t)idx;}
    /* Dynamic pseudo-classes are deliberately inactive until the UI supplies state. */
    return 0;
}
static int simple_match(const browser_document*d,uint32_t idx,const char*s,uint32_t n)
{
    uint32_t p=0,tag_end=0;const browser_node*x=&d->nodes[idx];while(n&&(s[n-1]=='>'||s[n-1]=='+'||s[n-1]=='~'||space(s[n-1])))--n;if(!n)return 0;while(p<n&&s[p]!='.'&&s[p]!='#'&&s[p]!=':'&&s[p]!='[')++p;tag_end=p;if(tag_end&&!(tag_end==1u&&s[0]=='*')&&!span_eq(s,tag_end,tag_name(x->tag))&&(!x->source_tag_hash||tag_hash(s,tag_end)!=x->source_tag_hash))return 0;
    while(p<n){
        if(s[p]==':'){uint32_t used=0u;if(!pseudo_match(d,idx,s+p+1u,n-p-1u,&used))return 0;p+=used+1u;continue;}
        if(s[p]=='.'||s[p]=='#'){char kind=s[p++];uint32_t a=p;while(p<n&&s[p]!='.'&&s[p]!='#'&&s[p]!=':'&&s[p]!='[')++p;if(kind=='#'){if(!x->id_off||!span_same(d->text+x->id_off,b_strlen(d->text+x->id_off),s+a,p-a))return 0;}else if(!x->class_off||!class_has(d->text+x->class_off,s+a,p-a))return 0;continue;}
        if(s[p]=='['){uint32_t close=++p;while(close<n&&s[close]!=']')++close;if(close>=n)return 0;uint32_t a=p;while(p<close&&!space(s[p])&&s[p]!='='&&s[p]!='^'&&s[p]!='*'&&s[p]!='$'&&s[p]!='~'&&s[p]!='|')++p;uint32_t b=p;while(p<close&&space(s[p]))++p;char op=0;if(p<close&&s[p]!='='){op=s[p++];if(p<close&&s[p]=='=')++p;}else if(p<close&&s[p]=='='){op='=';++p;}while(p<close&&space(s[p]))++p;char quote=0;if(p<close&&(s[p]=='\''||s[p]=='"'))quote=s[p++];uint32_t v=p;while(p<close&&(!quote||s[p]!=quote))++p;uint32_t ve=p;int present=0;const char*actual=node_attr(d,x,s+a,b-a,&present);if(!present)return 0;if(op){uint32_t an=b_strlen(actual),vn=ve-v;int match=0;if(op=='=')match=span_same(actual,an,s+v,vn);else if(op=='^')match=an>=vn&&span_same(actual,vn,s+v,vn);else if(op=='$')match=an>=vn&&span_same(actual+an-vn,vn,s+v,vn);else if(op=='*'){for(uint32_t k=0;k+vn<=an;++k)if(span_same(actual+k,vn,s+v,vn)){match=1;break;}}else if(op=='~')match=class_has(actual,s+v,vn);if(!match)return 0;}p=close+1u;continue;}
        return 0;
    }return 1;
}
static int selector_match_from(const browser_document*d,int32_t idx,const char*s,uint32_t n,uint32_t depth,uint32_t*budget)
{
    if(idx<0||depth>12u||!*budget)return 0;--*budget;while(n&&space(*s)){++s;--n;}while(n&&space(s[n-1]))--n;if(!n)return 1;
    uint32_t left_end=0u,right_start=0u,paren=0u,bracket=0u;uint8_t combinator=0u;char quote=0;
    for(uint32_t i=0u;i<n;){char c=s[i];if(quote){if(c=='\\'&&i+1u<n)++i;else if(c==quote)quote=0;++i;continue;}if(c=='\''||c=='"'){quote=c;++i;continue;}if(c=='('){++paren;++i;continue;}if(c==')'&&paren){--paren;++i;continue;}if(c=='['){++bracket;++i;continue;}if(c==']'&&bracket){--bracket;++i;continue;}if(!paren&&!bracket&&(c=='>'||c=='+'||c=='~')){left_end=i;right_start=i+1u;combinator=c=='>'?2u:c=='+'?3u:4u;++i;continue;}if(!paren&&!bracket&&space(c)){uint32_t a=i;while(i<n&&space(s[i]))++i;uint32_t before=a;while(before&&space(s[before-1u]))--before;char prev=before?s[before-1u]:0,next=i<n?s[i]:0;if(prev!='>'&&prev!='+'&&prev!='~'&&next!='>'&&next!='+'&&next!='~'){left_end=a;right_start=i;combinator=1u;}continue;}++i;}
    if(!combinator)return simple_match(d,(uint32_t)idx,s,n);while(right_start<n&&space(s[right_start]))++right_start;while(left_end&&space(s[left_end-1u]))--left_end;if(!simple_match(d,(uint32_t)idx,s+right_start,n-right_start))return 0;if(!left_end)return 1;
    if(combinator==2u)return selector_match_from(d,d->nodes[idx].parent,s,left_end,depth+1u,budget);
    if(combinator==3u||combinator==4u){int32_t previous=-1,parent=d->nodes[idx].parent;if(parent>=0)for(int32_t c=d->nodes[parent].first_child;c>=0&&c!=idx;c=d->nodes[c].next_sibling){if(d->nodes[c].text_off)continue;if(combinator==3u)previous=c;else if(selector_match_from(d,c,s,left_end,depth+1u,budget))return 1;}return combinator==3u&&previous>=0?selector_match_from(d,previous,s,left_end,depth+1u,budget):0;}
    for(int32_t p=d->nodes[idx].parent;p>=0&&*budget;p=d->nodes[p].parent)if(selector_match_from(d,p,s,left_end,depth+1u,budget))return 1;return 0;
}
static int selector_match(const browser_document*d,uint32_t idx,const char*s,uint32_t n){uint32_t budget=128u;return selector_match_from(d,(int32_t)idx,s,n,0u,&budget);}
static uint32_t specificity(const char*s,uint32_t n){uint32_t v=1;for(uint32_t i=0;i<n;++i){if(s[i]=='#')v+=100;else if(s[i]=='.')v+=10;}return v;}
static int span_starts_ci(const char*s,uint32_t n,const char*prefix){uint32_t i=0;while(prefix[i]){if(i>=n||lower(s[i])!=lower(prefix[i]))return 0;++i;}return 1;}
static int span_contains_ci(const char*s,uint32_t n,const char*needle){uint32_t z=b_strlen(needle);if(!z||z>n)return 0;for(uint32_t i=0;i+z<=n;++i)if(span_same(s+i,z,needle,z))return 1;return 0;}
static int media_applies(const char*s,uint32_t n){if(span_contains_ci(s,n,"print")||span_contains_ci(s,n,"prefers-color-scheme")||span_contains_ci(s,n,"prefers-reduced-motion"))return 0;const char*keys[2]={"min-width","max-width"};for(uint32_t k=0;k<2u;++k){uint32_t z=b_strlen(keys[k]);for(uint32_t i=0;i+z<n;++i)if(span_same(s+i,z,keys[k],z)){uint32_t p=i+z;while(p<n&&s[p]!=':')++p;if(p<n)++p;uint32_t px=number_px(s+p,n-p);if(px&&(k==0u?g_css_viewport<px:g_css_viewport>px))return 0;break;}}return 1;}
static void apply_css_range(browser_document*d,const char*css,uint32_t n,uint32_t pass,uint32_t limit,uint32_t*rules,uint32_t depth)
{
    uint32_t p=0;if(depth>4u)return;
    while(p<n&&*rules<limit){while(p<n){while(p<n&&(space(css[p])||css[p]==';'))++p;if(p+1u<n&&css[p]=='/'&&css[p+1]=='*'){p+=2u;while(p+1u<n&&!(css[p]=='*'&&css[p+1]=='/'))++p;if(p+1u<n)p+=2u;continue;}break;}if(p>=n)break;
        if(css[p]=='@'){uint32_t head=p,open=p;char quote=0;while(open<n){char c=css[open];if(quote){if(c=='\\'&&open+1u<n)++open;else if(c==quote)quote=0;}else if(c=='\''||c=='"')quote=c;else if(c=='{'||c==';')break;++open;}if(open>=n)break;if(css[open]==';'){p=open+1u;continue;}uint32_t q=open+1u,brace=1u;quote=0;while(q<n&&brace){char c=css[q++];if(quote){if(c=='\\'&&q<n)++q;else if(c==quote)quote=0;}else if(c=='\''||c=='"')quote=c;else if(c=='{')++brace;else if(c=='}')--brace;}uint32_t body_end=q&&q<=n?q-1u:n;int media=span_starts_ci(css+head,open-head,"@media"),layer=span_starts_ci(css+head,open-head,"@layer");if((layer||(media&&media_applies(css+head,open-head))))apply_css_range(d,css+open+1u,body_end-(open+1u),pass,limit,rules,depth+1u);p=q;continue;}
        uint32_t sel=p;while(p<n&&css[p]!='{')++p;if(p>=n)break;uint32_t se=p++,decl=p;char quote=0;uint32_t paren=0;while(p<n){char c=css[p];if(quote){if(c=='\\'&&p+1u<n)++p;else if(c==quote)quote=0;}else if(c=='\''||c=='"')quote=c;else if(c=='(')++paren;else if(c==')'&&paren)--paren;else if(c=='}'&&!paren)break;++p;}uint32_t de=p;if(p<n)++p;++*rules;if(se-sel>512u||de-decl>8192u)continue;
        uint32_t q=sel;while(q<se){uint32_t qe=q,paren_depth=0u,bracket_depth=0u;char selector_quote=0;while(qe<se){char c=css[qe];if(selector_quote){if(c=='\\'&&qe+1u<se)++qe;else if(c==selector_quote)selector_quote=0;}else if(c=='\''||c=='"')selector_quote=c;else if(c=='(')++paren_depth;else if(c==')'&&paren_depth)--paren_depth;else if(c=='[')++bracket_depth;else if(c==']'&&bracket_depth)--bracket_depth;else if(c==','&&!paren_depth&&!bracket_depth)break;++qe;}while(q<qe&&space(css[q]))++q;while(qe>q&&space(css[qe-1]))--qe;if(qe-q<=256u){uint32_t spec=specificity(css+q,qe-q);uint32_t bucket=spec>=100u?2u:spec>=10u?1u:0u;if(bucket==pass)for(uint32_t i=1;i<d->node_count;++i)if(selector_match(d,i,css+q,qe-q))parse_declarations(&d->nodes[i].style,css+decl,de-decl);}q=qe+1u;}
    }
}
static void apply_css(browser_document*d)
{
    g_css_variable_count=0u;b_memset(g_css_variables,0,sizeof(g_css_variables));for(uint32_t si=1u;si<d->node_count;++si){browser_node*sn=&d->nodes[si];if(sn->tag==B_TAG_STYLE&&sn->text_off)collect_css_variables(d->text+sn->text_off,b_strlen(d->text+sn->text_off));}
    for(uint32_t i=1;i<d->node_count;++i){browser_node*n=&d->nodes[i];n->style=default_style(n->tag);if(n->attr_width)n->style.width_px=n->attr_width;if(n->attr_height)n->style.height_px=n->attr_height;if(n->flags&1u)n->style.margin_right=4u;if(n->flags&2u)n->style.display=B_DISPLAY_NONE;}
    uint32_t limit=16384u;
    for(uint32_t pass=0;pass<3;++pass){uint32_t rules=0;for(uint32_t si=1;si<d->node_count&&rules<limit;++si){browser_node*sn=&d->nodes[si];if(sn->tag!=B_TAG_STYLE||!sn->text_off)continue;const char*css=d->text+sn->text_off;uint32_t n=b_strlen(css);if(n>524288u)n=524288u;apply_css_range(d,css,n,pass,limit,&rules,0u);}}
    for(uint32_t i=1;i<d->node_count;++i)if(d->nodes[i].inline_style_off)parse_declarations(&d->nodes[i].style,d->text+d->nodes[i].inline_style_off,b_strlen(d->text+d->nodes[i].inline_style_off));
    for(uint32_t i=1;i<d->node_count;++i){browser_node*n=&d->nodes[i];if(n->parent<0)continue;browser_style*p=&d->nodes[n->parent].style;if(!(n->style.mask&SM_COLOR))n->style.color=p->color;if(!(n->style.mask&SM_FONT))n->style.font_px=p->font_px;if(!(n->style.mask&SM_BOLD)&&p->bold)n->style.bold=1;if(!(n->style.mask&SM_ITALIC)&&p->italic)n->style.italic=1;if(!(n->style.mask&SM_UNDERLINE)&&p->underline)n->style.underline=1;if(!(n->style.mask&SM_ALIGN))n->style.text_align=p->text_align;}
}

void browser_document_reset(browser_document*d,uint32_t generation,const char*url)
{
    browser_node*nodes;char*text;uint32_t nc,tc;if(!d)return;nodes=d->nodes;text=d->text;nc=d->node_capacity;tc=d->text_capacity;b_memset(d,0,sizeof(*d));d->nodes=nodes;d->text=text;d->node_capacity=nc;d->text_capacity=tc;if(browser_document_init(d))return;
    d->generation=generation;d->text_used=1;d->text[0]=0;d->node_count=1;b_memset(&d->nodes[0],0,sizeof(browser_node));d->nodes[0].parent=d->nodes[0].first_child=d->nodes[0].last_child=d->nodes[0].next_sibling=-1;d->nodes[0].tag=B_TAG_DOCUMENT;d->nodes[0].style=default_style(B_TAG_DOCUMENT);b_copy(d->url,sizeof(d->url),url);b_copy(d->base_url,sizeof(d->base_url),url);b_copy(d->title,sizeof(d->title),"Dihscover");
}

static uint32_t find_raw_close(const char*html,uint32_t at,uint32_t size,const char*name)
{
    uint32_t name_len=b_strlen(name);
    for(uint32_t i=at;i+2u+name_len<=size;++i){if(html[i]!='<'||html[i+1]!='/')continue;uint32_t k=0;while(k<name_len&&lower(html[i+2u+k])==name[k])++k;if(k==name_len)return i;}
    return size;
}

int browser_document_parse(browser_document*d,const char*html,uint32_t size)
{
    int32_t stack[128];uint32_t depth=1u,p=0u;stack[0]=0;if(!d||!d->nodes||!d->text||!html)return -1;
    while(p<size&&d->node_count<DIHSCOVER_NODE_MAX){
        int32_t parent=stack[depth-1u];browser_node*pn=&d->nodes[parent];
        if(pn->tag==B_TAG_SCRIPT||pn->tag==B_TAG_STYLE||pn->tag==B_TAG_TITLE||pn->tag==B_TAG_TEMPLATE||pn->tag==B_TAG_SVG){const char*name=pn->tag==B_TAG_SCRIPT?"script":pn->tag==B_TAG_STYLE?"style":pn->tag==B_TAG_TITLE?"title":pn->tag==B_TAG_TEMPLATE?"template":"svg";uint32_t raw_end=find_raw_close(html,p,size,name);if(raw_end>p){if(pn->tag==B_TAG_SVG){uint32_t take=raw_end-p;if(take>262000u){take=262000u;d->truncated=1u;}uint32_t off=store_raw_join(d,pn->value_off,html+p,take,"</svg>");if(off){pn->text_off=off;pn->text_len=b_strlen(d->text+off);}}else if(pn->tag!=B_TAG_TEMPLATE){uint32_t take=raw_end-p,cap=pn->tag==B_TAG_STYLE?524288u:pn->tag==B_TAG_SCRIPT?32768u:DIHSCOVER_TITLE_CAP-1u;if(take>cap){take=cap;d->truncated=1;}uint32_t off=store(d,html+p,take);if(off&&d->text[off]){if(pn->tag==B_TAG_TITLE)b_copy(d->title,sizeof(d->title),d->text+off);else{pn->text_off=off;pn->text_len=b_strlen(d->text+off);}}}p=raw_end;continue;}}
        if(html[p]!='<'){uint32_t a=p;while(p<size&&html[p]!='<')++p;uint32_t at=a;while(at<p&&d->node_count<DIHSCOVER_NODE_MAX){uint32_t chunk=p-at;if(chunk>180u){chunk=180u;while(chunk>96u&&!space(html[at+chunk]))--chunk;}uint32_t off=store(d,html+at,chunk);if(off&&d->text[off]){int32_t idx=add_node(d,parent,B_TAG_SPAN);if(idx>=0){d->nodes[idx].text_off=off;d->nodes[idx].text_len=b_strlen(d->text+off);if(space(html[at+chunk-1u]))d->nodes[idx].flags|=1u;}}at+=chunk;}continue;}
        if(p+3u<size&&html[p+1]=='!'&&html[p+2]=='-'&&html[p+3]=='-'){p+=4u;while(p+2u<size&&!(html[p]=='-'&&html[p+1]=='-'&&html[p+2]=='>'))++p;if(p+2u<size)p+=3u;continue;}
        if(p+1u<size&&html[p+1]=='!'){while(p<size&&html[p]!='>')++p;if(p<size)++p;continue;}
        uint32_t end=p+1u;char quote=0;while(end<size){char c=html[end];if(quote){if(c=='\\'&&end+1u<size)++end;else if(c==quote)quote=0;}else if(c=='\''||c=='"')quote=c;else if(c=='>')break;++end;}if(end>=size)break;
        uint32_t q=p+1u;int closing=q<end&&html[q]=='/';if(closing)++q;while(q<end&&space(html[q]))++q;uint32_t name=q;while(q<end&&!space(html[q])&&html[q]!='/'&&html[q]!='>')++q;uint16_t tag=parse_tag(html+name,q-name);uint32_t source_hash=tag_hash(html+name,q-name);
        if(closing){for(uint32_t k=depth;k>1u;--k)if(d->nodes[stack[k-1u]].source_tag_hash==source_hash){depth=k-1u;break;}p=end+1u;continue;}
        if(depth>1u){uint16_t open_tag=d->nodes[stack[depth-1u]].tag;int closes_same=(tag==B_TAG_P&&open_tag==B_TAG_P)||(tag==B_TAG_LI&&open_tag==B_TAG_LI)||(tag==B_TAG_TR&&open_tag==B_TAG_TR)||(tag==B_TAG_TD&&open_tag==B_TAG_TD);int block_closes_p=open_tag==B_TAG_P&&(tag==B_TAG_DIV||tag==B_TAG_P||tag==B_TAG_H1||tag==B_TAG_H2||tag==B_TAG_H3||tag==B_TAG_H4||tag==B_TAG_H5||tag==B_TAG_H6||tag==B_TAG_BLOCKQUOTE||tag==B_TAG_FIGURE||tag==B_TAG_UL||tag==B_TAG_OL||tag==B_TAG_TABLE||tag==B_TAG_FORM);if(closes_same||block_closes_p){--depth;parent=stack[depth-1u];}}
        int32_t idx=add_node(d,parent,tag);if(idx>=0){d->nodes[idx].source_tag_hash=source_hash;parse_attrs(d,&d->nodes[idx],html+q,end-q);if(tag==B_TAG_SVG)d->nodes[idx].value_off=store(d,html+p,end-p+1u);}int is_void=tag==B_TAG_IMG||tag==B_TAG_INPUT||tag==B_TAG_BR||tag==B_TAG_HR||tag==B_TAG_META||tag==B_TAG_LINK||(end>p&&html[end-1u]=='/');if(!is_void&&idx>=0&&depth<128u)stack[depth++]=idx;p=end+1u;
    }
    if(p<size)d->truncated=1;apply_css(d);return d->node_count>1u?0:-1;
}

int browser_document_apply_stylesheet(browser_document*d,const char*css,uint32_t size)
{
    if(!d||!css||!size)return -1;if(size>524288u){size=524288u;d->truncated=1;}int32_t idx=add_node(d,0,B_TAG_STYLE);if(idx<0)return -1;uint32_t off=store(d,css,size);if(!off)return -1;d->nodes[idx].text_off=off;d->nodes[idx].text_len=b_strlen(d->text+off);apply_css(d);return 0;
}

void browser_document_apply_site_defaults(browser_document*d)
{
    if(!d||!d->nodes)return;
    if(span_contains_ci(d->url,b_strlen(d->url),"wikipedia.org/")){
        for(uint32_t i=1u;i<d->node_count;++i){browser_node*n=&d->nodes[i];const char*c=n->class_off?d->text+n->class_off:0;if(!c)continue;
            if(class_has(c,"vector-header-container",23u)||class_has(c,"vector-main-menu-container",26u)||class_has(c,"vector-column-start",19u)||class_has(c,"vector-column-end",17u)||class_has(c,"vector-page-toolbar",19u)||class_has(c,"vector-sticky-header",20u)||class_has(c,"vector-toc",10u)||class_has(c,"mw-jump-link",12u)||class_has(c,"noprint",7u)){n->style.display=B_DISPLAY_NONE;n->style.mask|=SM_DISPLAY;continue;}
            if(class_has(c,"mw-page-container",17u)){n->style.display=B_DISPLAY_BLOCK;n->style.max_width_px=980u;n->style.margin_auto_left=n->style.margin_auto_right=1u;n->style.padding_left=n->style.padding_right=8u;n->style.border_width=0u;n->style.has_background=0u;}
            if(class_has(c,"mw-content-container",20u)||class_has(c,"mw-body",7u)||class_has(c,"mw-body-content",15u)||class_has(c,"vector-body",11u)){n->style.display=B_DISPLAY_BLOCK;n->style.max_width_px=920u;n->style.margin_auto_left=n->style.margin_auto_right=1u;n->style.width_percent=100u;n->style.border_width=0u;n->style.has_background=0u;}
            if(class_has(c,"infobox",7u)||class_has(c,"sidebar",7u)){n->style.display=B_DISPLAY_BLOCK;n->style.max_width_px=420u;n->style.margin_auto_left=n->style.margin_auto_right=1u;n->style.padding_left=n->style.padding_right=8u;}
            if(class_has(c,"gallery",7u)){n->style.display=B_DISPLAY_FLEX;n->style.flex_wrap=1u;n->style.gap_px=10u;}
        }
        return;
    }
    if(!span_contains_ci(d->url,b_strlen(d->url),"duckduckgo.com/"))return;
    for(uint32_t i=1u;i<d->node_count;++i){browser_node*n=&d->nodes[i];const char*c=n->class_off?d->text+n->class_off:0;if(!c)continue;
        if(class_has(c,"result",6u)){n->style.display=B_DISPLAY_BLOCK;n->style.max_width_px=760u;n->style.margin_bottom=22u;n->style.padding_bottom=8u;}
        if(class_has(c,"results",7u)||class_has(c,"results--main",13u)){n->style.display=B_DISPLAY_BLOCK;n->style.max_width_px=780u;n->style.margin_auto_left=n->style.margin_auto_right=1u;}
        if(class_has(c,"result__title",13u)){n->style.display=B_DISPLAY_BLOCK;n->style.font_px=21u;n->style.bold=1u;n->style.margin_bottom=4u;}
        if(class_has(c,"result__a",9u)){n->style.color=color(15,82,172);n->style.font_px=20u;n->style.bold=0u;n->style.underline=1u;}
        if(class_has(c,"result__snippet",15u)){n->style.display=B_DISPLAY_BLOCK;n->style.color=color(45,52,58);n->style.font_px=16u;n->style.line_height_px=23u;n->style.margin_bottom=4u;}
        if(class_has(c,"result__url",11u)||class_has(c,"result__url__domain",19u)){n->style.display=B_DISPLAY_BLOCK;n->style.color=color(27,118,69);n->style.font_px=14u;n->style.underline=0u;n->style.margin_bottom=3u;}
        if(class_has(c,"result__icon",12u)||class_has(c,"result__extras",14u)||class_has(c,"result__menu",12u)||class_has(c,"dropdown",8u)||class_has(c,"feedback-btn",12u)){n->style.display=B_DISPLAY_NONE;n->style.mask|=SM_DISPLAY;}
    }
}

static void shift_subtree(browser_document*d,int32_t idx,int32_t dx){if(idx<0||!dx)return;d->nodes[idx].x+=dx;for(int32_t c=d->nodes[idx].first_child;c>=0;c=d->nodes[c].next_sibling)shift_subtree(d,c,dx);}
static void shift_subtree_y(browser_document*d,int32_t idx,int32_t dy){if(idx<0||!dy)return;d->nodes[idx].y+=dy;for(int32_t c=d->nodes[idx].first_child;c>=0;c=d->nodes[c].next_sibling)shift_subtree_y(d,c,dy);}

static uint32_t intrinsic_width(browser_document*d,int32_t idx,uint32_t limit,uint32_t depth)
{
    if(idx<0||depth>24u)return 1u;browser_node*n=&d->nodes[idx];if(n->style.display==B_DISPLAY_NONE)return 0u;uint32_t margins=n->style.margin_left+n->style.margin_right;if(n->style.width_percent)return (limit*n->style.width_percent)/100u;if(n->style.width_px)return n->style.width_px+margins;if(n->style.flex_basis_px)return n->style.flex_basis_px+margins;if(n->text_off){uint32_t measured=0u;(void)browser_text_wrap(&n->style,d->text+n->text_off,0x100000u,0,0,&measured);uint32_t w=measured+n->style.padding_left+n->style.padding_right+margins;return w>limit?limit:(w?w:1u);}if(n->tag==B_TAG_IMG){uint32_t w=n->attr_width?n->attr_width:180u;return w+margins>limit?limit:w+margins;}uint32_t value=0u;if(n->style.display==B_DISPLAY_FLEX&&!n->style.flex_direction){for(int32_t c=n->first_child;c>=0;c=d->nodes[c].next_sibling)value+=intrinsic_width(d,c,limit,depth+1u)+n->style.gap_px;}else for(int32_t c=n->first_child;c>=0;c=d->nodes[c].next_sibling){uint32_t w=intrinsic_width(d,c,limit,depth+1u);if(w>value)value=w;}value+=n->style.padding_left+n->style.padding_right+margins;if(n->style.min_width_px&&value<n->style.min_width_px)value=n->style.min_width_px;if(n->style.max_width_px&&value>n->style.max_width_px)value=n->style.max_width_px;return value>limit?limit:(value?value:1u);
}

static uint32_t layout_node(browser_document*d,int32_t idx,int32_t x,int32_t y,uint32_t width,uint32_t depth)
{
    browser_node*n=&d->nodes[idx];if(depth>128||n->style.display==B_DISPLAY_NONE){n->w=n->h=0;return 0;}uint32_t avail=width>n->style.margin_left+n->style.margin_right?width-n->style.margin_left-n->style.margin_right:width;n->y=y+n->style.margin_top;n->w=n->style.width_percent?(avail*n->style.width_percent)/100u:(n->style.width_px&&n->style.width_px<avail?n->style.width_px:avail);uint32_t maxw=n->style.max_width_percent?(avail*n->style.max_width_percent)/100u:n->style.max_width_px;if(maxw&&n->w>maxw)n->w=maxw;if(n->style.min_width_px&&n->w<n->style.min_width_px)n->w=n->style.min_width_px<avail?n->style.min_width_px:avail;n->x=x+n->style.margin_left;if(n->style.margin_auto_left&&n->style.margin_auto_right&&n->w<width)n->x=x+(int32_t)((width-n->w)/2u);else if(n->style.margin_auto_left&&n->w<width)n->x=x+(int32_t)(width-n->w-n->style.margin_right);if(n->style.position==1u){if(n->style.inset_mask&8u)n->x+=n->style.inset_left;else if(n->style.inset_mask&2u)n->x-=n->style.inset_right;if(n->style.inset_mask&1u)n->y+=n->style.inset_top;else if(n->style.inset_mask&4u)n->y-=n->style.inset_bottom;}
    uint32_t inner=n->w>n->style.padding_left+n->style.padding_right?n->w-n->style.padding_left-n->style.padding_right:n->w;uint32_t scale=browser_text_scale(n->style.font_px);uint32_t glyph_h=g_text_api&&g_text_api->text_line_height?g_text_api->text_line_height(scale,0):n->style.font_px;uint32_t line_h=n->style.line_height_px?n->style.line_height_px:glyph_h+4u;if(line_h<glyph_h)line_h=glyph_h;
    if(n->text_off){char wrapped[256];uint32_t measured=0;uint32_t lines=browser_text_wrap(&n->style,d->text+n->text_off,n->style.nowrap?0x100000u:inner,wrapped,sizeof(wrapped),&measured);if(n->style.display==B_DISPLAY_INLINE&&lines==1u){uint32_t wanted=measured+n->style.padding_left+n->style.padding_right;if(wanted<n->w)n->w=wanted;}n->h=lines*line_h+n->style.padding_top+n->style.padding_bottom;if(n->style.height_px&&n->style.height_px>n->h)n->h=n->style.height_px;if(n->style.min_height_px&&n->h<n->style.min_height_px)n->h=n->style.min_height_px;if(n->style.max_height_px&&n->h>n->style.max_height_px)n->h=n->style.max_height_px;return n->style.margin_top+n->h+n->style.margin_bottom;}
    if(n->tag==B_TAG_IMG||n->tag==B_TAG_SVG){n->h=n->style.height_px?n->style.height_px:(n->style.aspect_num&&n->style.aspect_den?(uint32_t)(((uint64_t)n->w*n->style.aspect_den)/n->style.aspect_num):180u);n->w=n->style.width_px?n->style.width_px:n->w;if(n->style.min_height_px&&n->h<n->style.min_height_px)n->h=n->style.min_height_px;if(n->style.max_height_px&&n->h>n->style.max_height_px)n->h=n->style.max_height_px;return n->style.margin_top+n->h+n->style.margin_bottom;}
    if(n->tag==B_TAG_BR){n->h=line_h;return n->h;}if(n->tag==B_TAG_HR){n->h=n->style.height_px?n->style.height_px:2;return n->h+12;}
    uint32_t used=n->style.padding_top,max_right=0;int32_t child=n->first_child;if(n->style.display==B_DISPLAY_FLEX&&n->style.flex_direction&&child>=0){
        uint32_t count=0u;for(int32_t c=child;c>=0;c=d->nodes[c].next_sibling)if(d->nodes[c].style.display!=B_DISPLAY_NONE&&d->nodes[c].style.position<2u)++count;uint32_t seen=0u;int32_t parent_x=n->x+(int32_t)n->style.padding_left;
        for(int32_t c=child;c>=0;c=d->nodes[c].next_sibling){browser_node*cn=&d->nodes[c];if(cn->style.display==B_DISPLAY_NONE||cn->style.position>=2u)continue;if(seen++)used+=n->style.gap_px;uint32_t child_w=inner;if(cn->style.width_px&&cn->style.width_px<child_w)child_w=cn->style.width_px;int32_t child_x=parent_x;if(child_w<inner&&n->style.align_items==1u)child_x+=(int32_t)((inner-child_w)/2u);else if(child_w<inner&&n->style.align_items==2u)child_x+=(int32_t)(inner-child_w);uint32_t h=layout_node(d,c,child_x,n->y+(int32_t)used,child_w,depth+1);if(used>0x1000000u-h){d->truncated=1;break;}used+=h;}(void)count;
    }else if(n->style.display==B_DISPLAY_FLEX&&!n->style.flex_direction&&n->style.flex_wrap&&child>=0){
        uint32_t pos=0u,row_h=0u;int32_t parent_x=n->x+(int32_t)n->style.padding_left;uint32_t grid_track=n->style.grid_layout&&n->style.grid_columns?(inner>(n->style.grid_columns-1u)*n->style.gap_px?(inner-(n->style.grid_columns-1u)*n->style.gap_px)/n->style.grid_columns:1u):0u;for(int32_t c=child;c>=0;c=d->nodes[c].next_sibling){browser_node*cn=&d->nodes[c];if(cn->style.display==B_DISPLAY_NONE||cn->style.position>=2u)continue;uint32_t track=cn->style.width_px?cn->style.width_px:cn->style.flex_basis_px?cn->style.flex_basis_px:grid_track?grid_track:n->style.grid_layout?(inner>240u?240u:inner):intrinsic_width(d,c,inner,0u);track+=cn->style.margin_left+cn->style.margin_right;if(track>inner)track=inner;if(pos&&pos+track>inner){used+=row_h+n->style.gap_px;pos=0u;row_h=0u;}uint32_t h=layout_node(d,c,parent_x+(int32_t)pos,n->y+(int32_t)used,track,depth+1);if(h>row_h)row_h=h;pos+=track+n->style.gap_px;}used+=row_h;
    }else if(n->style.display==B_DISPLAY_FLEX&&!n->style.flex_direction&&child>=0){
        uint32_t count=0u,grow=0u,bases=0u,maxh=0u,pos=0u;for(int32_t c=child;c>=0;c=d->nodes[c].next_sibling){browser_node*cn=&d->nodes[c];if(cn->style.display==B_DISPLAY_NONE||cn->style.position>=2u)continue;++count;bases+=intrinsic_width(d,c,inner,0u);grow+=cn->style.flex_grow;}
        uint32_t gap=count>1u?n->style.gap_px:0u,gaps=count>1u?(count-1u)*gap:0u,available=inner>gaps?inner-gaps:1u;uint32_t occupied=bases<available?bases:available;if(bases<available&&grow)occupied=available;uint32_t start=0u;if(occupied<available&&n->style.justify_content==1u)start=(available-occupied)/2u;else if(occupied<available&&n->style.justify_content==2u)start=available-occupied;else if(occupied<available&&n->style.justify_content==3u&&count>1u)gap+=(available-occupied)/(count-1u);pos=start;
        for(int32_t c=child;c>=0;c=d->nodes[c].next_sibling){browser_node*cn=&d->nodes[c];if(cn->style.display==B_DISPLAY_NONE||cn->style.position>=2u)continue;uint32_t basis=intrinsic_width(d,c,inner,0u),track=basis;if(bases<available&&grow&&cn->style.flex_grow)track+=(uint32_t)(((uint64_t)(available-bases)*cn->style.flex_grow)/grow);else if(bases>available&&bases)track=(uint32_t)(((uint64_t)basis*available)/bases);if(!track)track=1u;if(pos>=inner)track=1u;else if(track>inner-pos)track=inner-pos;uint32_t h=layout_node(d,c,n->x+n->style.padding_left+(int32_t)pos,n->y+n->style.padding_top,track,depth+1);if(h>maxh)maxh=h;if(pos>0x1000000u-track-gap){d->truncated=1;break;}pos+=track+gap;}used+=maxh;
    }else{uint32_t row_x=0,row_h=0,float_w=0u,float_bottom=0u;uint8_t float_side=0u;int32_t parent_x=n->x+(int32_t)n->style.padding_left;for(int32_t c=child;c>=0;c=d->nodes[c].next_sibling){browser_node*cn=&d->nodes[c];if(cn->style.display==B_DISPLAY_NONE||cn->style.position>=2u)continue;if(cn->style.display==B_DISPLAY_INLINE){uint32_t remain=inner>row_x?inner-row_x:inner;if(row_x&&remain<32u){used+=row_h;row_x=0;row_h=0;remain=inner;}uint32_t h=layout_node(d,c,parent_x+(int32_t)row_x,n->y+(int32_t)used,remain,depth+1);uint32_t child_right=(uint32_t)(cn->x-parent_x)+cn->w+cn->style.margin_right;if(row_x&&child_right>inner){used+=row_h;row_x=0;row_h=0;h=layout_node(d,c,parent_x,n->y+(int32_t)used,inner,depth+1);child_right=(uint32_t)(cn->x-parent_x)+cn->w+cn->style.margin_right;}row_x=child_right>inner?inner:child_right;if(row_x>max_right)max_right=row_x;if(h>row_h)row_h=h;}else{if(row_h){used+=row_h;row_x=0;row_h=0;}if(cn->style.clear_float&&used<float_bottom){used=float_bottom;float_w=float_bottom=0u;float_side=0u;}if(cn->style.float_side){uint32_t track=cn->style.width_px?cn->style.width_px:(inner>560u?inner*45u/100u:inner);if(track>inner)track=inner;int32_t fx=parent_x;if(cn->style.float_side==2u&&inner>track)fx+=(int32_t)(inner-track);uint32_t h=layout_node(d,c,fx,n->y+(int32_t)used,track,depth+1);float_w=track+(n->style.gap_px?n->style.gap_px:14u);float_bottom=used+h;float_side=cn->style.float_side;continue;}uint32_t available=inner;int32_t block_x=parent_x;if(float_w&&used<float_bottom&&inner>float_w+80u){available=inner-float_w;if(float_side==1u)block_x+=(int32_t)float_w;}else if(used>=float_bottom){float_w=float_bottom=0u;float_side=0u;}uint32_t h=layout_node(d,c,block_x,n->y+(int32_t)used,available,depth+1);if(used>0x1000000u-h){d->truncated=1;break;}used+=h;}}if(row_h)used+=row_h;if(used<float_bottom)used=float_bottom;if(n->style.display==B_DISPLAY_INLINE&&max_right&&max_right+n->style.padding_right<n->w)n->w=max_right+n->style.padding_right;}
    if(n->style.text_align&&max_right<inner){int32_t shift=n->style.text_align==1u?(int32_t)((inner-max_right)/2u):(int32_t)(inner-max_right);for(int32_t c=child;c>=0;c=d->nodes[c].next_sibling)if(d->nodes[c].style.display==B_DISPLAY_INLINE)shift_subtree(d,c,shift);}
    used+=n->style.padding_bottom;n->h=n->style.height_px?n->style.height_px:used;if(n->style.min_height_px&&n->h<n->style.min_height_px)n->h=n->style.min_height_px;if(n->style.max_height_px&&n->h>n->style.max_height_px)n->h=n->style.max_height_px;if(!n->h&&n->style.has_background)n->h=line_h;for(int32_t c=child;c>=0;c=d->nodes[c].next_sibling){browser_node*cn=&d->nodes[c];if(cn->style.display==B_DISPLAY_NONE||cn->style.position<2u)continue;uint32_t track=cn->style.width_px?cn->style.width_px:intrinsic_width(d,c,inner,0u);if(track>inner)track=inner;int32_t px=n->x+(int32_t)n->style.padding_left,py=n->y+(int32_t)n->style.padding_top;if(cn->style.inset_mask&8u)px+=cn->style.inset_left;else if(cn->style.inset_mask&2u)px+=(int32_t)(inner-track)-cn->style.inset_right;if(cn->style.inset_mask&1u)py+=cn->style.inset_top;else if(cn->style.inset_mask&4u)py+=(int32_t)n->h-cn->style.inset_bottom;(void)layout_node(d,c,px,py,track,depth+1u);if(!(cn->style.inset_mask&1u)&&(cn->style.inset_mask&4u))shift_subtree_y(d,c,-(int32_t)(cn->h+cn->style.margin_top));}return n->style.margin_top+n->h+n->style.margin_bottom;
}

void browser_document_layout(browser_document*d,uint32_t viewport_w)
{
    if(!d||!d->nodes)return;g_css_viewport=viewport_w?viewport_w:860u;uint32_t y=18;uint32_t w=viewport_w>48?viewport_w-48:viewport_w;for(int32_t c=d->nodes[0].first_child;c>=0;c=d->nodes[c].next_sibling){browser_node*n=&d->nodes[c];int32_t x=24,cy=(int32_t)y;if(n->style.position>=2u){if(n->style.inset_mask&8u)x+=n->style.inset_left;else if((n->style.inset_mask&2u)&&n->style.width_px<viewport_w)x=(int32_t)viewport_w-(int32_t)n->style.width_px-n->style.inset_right;if(n->style.inset_mask&1u)cy=n->style.inset_top;}(void)layout_node(d,c,x,cy,w,0);if(n->style.position>=2u)continue;uint32_t h=n->style.margin_top+n->h+n->style.margin_bottom;if(y>0x3fffffffu-h){d->truncated=1;break;}y+=h;}d->nodes[0].x=0;d->nodes[0].y=0;d->nodes[0].w=viewport_w;d->nodes[0].h=y+24;d->content_height=y+24;
}

int browser_document_make_readable(browser_document*d)
{
    uint32_t visible=0u;if(!d||!d->nodes)return 0;for(uint32_t i=1u;i<d->node_count;++i)if(d->nodes[i].text_off&&d->nodes[i].h)++visible;if(visible)return 0;
    int changed=0;for(uint32_t i=1u;i<d->node_count;++i)if(d->nodes[i].text_off){int blocked=0;for(int32_t p=(int32_t)i;p>0;p=d->nodes[p].parent)if(d->nodes[p].tag==B_TAG_HEAD||d->nodes[p].tag==B_TAG_STYLE||d->nodes[p].tag==B_TAG_SCRIPT||d->nodes[p].tag==B_TAG_TEMPLATE||d->nodes[p].tag==B_TAG_SVG){blocked=1;break;}if(blocked)continue;d->nodes[i].style.display=B_DISPLAY_INLINE;for(int32_t p=d->nodes[i].parent;p>0;p=d->nodes[p].parent)if(d->nodes[p].style.display==B_DISPLAY_NONE)d->nodes[p].style.display=B_DISPLAY_BLOCK;changed=1;}return changed;
}

int browser_document_hit_test(const browser_document*d,int32_t x,int32_t y){if(!d||!d->nodes)return -1;for(int32_t i=(int32_t)d->node_count-1;i>0;--i){const browser_node*n=&d->nodes[i];if(n->h&&x>=n->x-3&&y>=n->y-2&&x<n->x+(int32_t)n->w+3&&y<n->y+(int32_t)n->h+2){for(int32_t at=i;at>0;at=d->nodes[at].parent)if(d->nodes[at].tag==B_TAG_A||d->nodes[at].tag==B_TAG_BUTTON||d->nodes[at].tag==B_TAG_LABEL||d->nodes[at].tag==B_TAG_INPUT)return at;}}return -1;}

static int node_descends_from(const browser_document*d,uint32_t node,uint32_t ancestor){for(int32_t p=(int32_t)node;p>=0;p=d->nodes[p].parent)if((uint32_t)p==ancestor)return 1;return 0;}
static int32_t containing_form(const browser_document*d,uint32_t node){for(int32_t p=(int32_t)node;p>0;p=d->nodes[p].parent)if(d->nodes[p].tag==B_TAG_FORM)return p;return -1;}

int browser_document_activate(browser_document*d,uint32_t node_index,uint32_t*out_form_index)
{
    if(out_form_index)*out_form_index=0u;if(!d||node_index>=d->node_count)return 0;browser_node*n=&d->nodes[node_index];if(n->flags&32u)return 0;
    if((n->tag==B_TAG_BUTTON||n->tag==B_TAG_INPUT)&&(n->flags&16u)){int32_t form=containing_form(d,node_index);if(form>0&&out_form_index)*out_form_index=(uint32_t)form;return form>0?2:0;}
    int32_t input=n->tag==B_TAG_INPUT?(int32_t)node_index:-1;
    if(n->tag==B_TAG_LABEL){if(n->for_off){const char*wanted=d->text+n->for_off;for(uint32_t i=1u;i<d->node_count;++i)if(d->nodes[i].tag==B_TAG_INPUT&&d->nodes[i].id_off&&b_streq(d->text+d->nodes[i].id_off,wanted)){input=(int32_t)i;break;}}if(input<0)for(uint32_t i=node_index+1u;i<d->node_count;++i)if(node_descends_from(d,i,node_index)&&d->nodes[i].tag==B_TAG_INPUT){input=(int32_t)i;break;}}
    if(input>=0&&(d->nodes[input].flags&8u)){d->nodes[input].flags^=4u;if(n->tag==B_TAG_LABEL){n->style.border_width=(d->nodes[input].flags&4u)?3u:0u;n->style.border_color=color(15,135,164);n->style.has_background=(uint8_t)((d->nodes[input].flags&4u)!=0u);n->style.background=color(213,244,248);}return 1;}
    return 0;
}

static int form_put_encoded(char*out,uint32_t cap,uint32_t*p,const char*s)
{
    static const char hex[]="0123456789ABCDEF";for(uint32_t i=0u;s&&s[i];++i){unsigned char c=(unsigned char)s[i];if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~'){if(*p+1u>=cap)return -1;out[(*p)++]=(char)c;}else if(c==' '){if(*p+1u>=cap)return -1;out[(*p)++]='+';}else{if(*p+3u>=cap)return -1;out[(*p)++]='%';out[(*p)++]=hex[c>>4];out[(*p)++]=hex[c&15u];}}out[*p]=0;return 0;
}

int browser_document_encode_form(const browser_document*d,uint32_t form_index,char*action,uint32_t action_cap,char*method,uint32_t method_cap,char*body,uint32_t body_cap)
{
    if(!d||form_index>=d->node_count||d->nodes[form_index].tag!=B_TAG_FORM||!action||!method||!body)return -1;const browser_node*f=&d->nodes[form_index];b_copy(action,action_cap,f->action_off?d->text+f->action_off:d->url);b_copy(method,method_cap,f->method_off?d->text+f->method_off:"GET");uint32_t p=0u;body[0]=0;
    for(uint32_t i=form_index+1u;i<d->node_count;++i){const browser_node*n=&d->nodes[i];if(!node_descends_from(d,i,form_index))continue;if(n->tag!=B_TAG_INPUT||!n->name_off||(n->flags&16u)||((n->flags&8u)&&!(n->flags&4u)))continue;if(p){if(p+1u>=body_cap)return -1;body[p++]='&';body[p]=0;}if(form_put_encoded(body,body_cap,&p,d->text+n->name_off)||p+1u>=body_cap)return -1;body[p++]='=';body[p]=0;const char*v=n->value_off?d->text+n->value_off:((n->flags&8u)?"on":(n->text_off?d->text+n->text_off:""));if(form_put_encoded(body,body_cap,&p,v))return -1;}
    return 0;
}
const char*browser_document_string(const browser_document*d,uint32_t off){return d&&d->text&&off<d->text_used?d->text+off:"";}
int browser_document_find_selector(const browser_document*d,const char*selector){if(!d||!selector||!selector[0])return -1;uint32_t n=b_strlen(selector);if(n>256u)return -1;for(uint32_t i=1u;i<d->node_count;++i)if(selector_match(d,i,selector,n))return (int)i;return -1;}
int browser_document_set_text(browser_document*d,uint32_t index,const char*value){if(!d||index>=d->node_count||!value)return -1;browser_node*n=&d->nodes[index];uint32_t off=store(d,value,b_strlen(value));if(!off)return -1;int32_t c=n->first_child;if(c<0)c=add_node(d,(int32_t)index,B_TAG_SPAN);if(c<0)return -1;d->nodes[c].text_off=off;d->nodes[c].text_len=b_strlen(value);d->nodes[c].style.display=B_DISPLAY_INLINE;for(int32_t rest=d->nodes[c].next_sibling;rest>=0;rest=d->nodes[rest].next_sibling)d->nodes[rest].style.display=B_DISPLAY_NONE;return 0;}
int browser_document_set_script_source(browser_document*d,uint32_t index,const char*source,uint32_t size){if(!d||index>=d->node_count||d->nodes[index].tag!=B_TAG_SCRIPT||!source||!size)return -1;uint32_t off=store(d,source,size);if(!off)return -1;d->nodes[index].text_off=off;d->nodes[index].text_len=size;return 0;}
int browser_document_set_text_by_id(browser_document*d,const char*id,const char*value){if(!d||!id)return -1;char selector[100]="#";b_copy(selector+1,sizeof(selector)-1u,id);int idx=browser_document_find_selector(d,selector);return idx<0?-1:browser_document_set_text(d,(uint32_t)idx,value);}
int browser_document_set_style(browser_document*d,uint32_t index,const char*property,const char*value)
{
    if(!d||index>=d->node_count||!property||!value)return -1;char declaration[384];uint32_t p=0u;for(uint32_t i=0u;property[i]&&p+2u<sizeof(declaration);++i){char c=property[i];if(c>='A'&&c<='Z'){declaration[p++]='-';c=(char)(c+32);}declaration[p++]=c;}if(p+2u>=sizeof(declaration))return -1;declaration[p++]=':';for(uint32_t i=0u;value[i]&&p+1u<sizeof(declaration);++i)declaration[p++]=value[i];declaration[p]=0;parse_declarations(&d->nodes[index].style,declaration,p);return 0;
}
int browser_document_set_style_by_id(browser_document*d,const char*id,const char*property,const char*value){if(!d||!id)return -1;char selector[100]="#";b_copy(selector+1,sizeof(selector)-1u,id);int idx=browser_document_find_selector(d,selector);return idx<0?-1:browser_document_set_style(d,(uint32_t)idx,property,value);}
int browser_document_set_class(browser_document*d,uint32_t index,const char*name,uint32_t operation)
{
    if(!d||index>=d->node_count||!name||!name[0])return -1;browser_node*n=&d->nodes[index];const char*old=n->class_off?d->text+n->class_off:"";uint32_t name_n=b_strlen(name);int present=class_has(old,name,name_n);if(operation==2u)operation=present?1u:0u;if((operation==0u&&present)||(operation==1u&&!present))return 0;char out[384];uint32_t w=0u;if(operation==0u){for(uint32_t i=0u;old[i]&&w+1u<sizeof(out);++i)out[w++]=old[i];if(w&&w+1u<sizeof(out))out[w++]=' ';for(uint32_t i=0u;name[i]&&w+1u<sizeof(out);++i)out[w++]=name[i];}else{const char*p=old;while(*p){while(*p&&space(*p))++p;const char*a=p;while(*p&&!space(*p))++p;uint32_t z=(uint32_t)(p-a);if(z&&!(z==name_n&&span_same(a,z,name,name_n))){if(w&&w+1u<sizeof(out))out[w++]=' ';for(uint32_t i=0u;i<z&&w+1u<sizeof(out);++i)out[w++]=a[i];}}}out[w]=0;n->class_off=w?store(d,out,w):0u;apply_css(d);return 0;
}
int browser_document_set_attribute(browser_document*d,uint32_t index,const char*name,const char*value,uint8_t remove)
{
    if(!d||index>=d->node_count||!name)return -1;browser_node*n=&d->nodes[index];uint32_t off=(!remove&&value&&value[0])?store(d,value,b_strlen(value)):0u;
    if(b_streq(name,"class")){n->class_off=off;apply_css(d);}else if(b_streq(name,"id")){n->id_off=off;apply_css(d);}else if(b_streq(name,"value"))n->value_off=off;else if(b_streq(name,"href"))n->href_off=off;else if(b_streq(name,"src"))n->src_off=off;else if(b_streq(name,"hidden")){if(remove)n->flags&=(uint16_t)~2u;else n->flags|=2u;apply_css(d);}else if(b_streq(name,"disabled")){if(remove)n->flags&=(uint16_t)~32u;else n->flags|=32u;}else if(b_streq(name,"checked")){if(remove)n->flags&=(uint16_t)~4u;else n->flags|=4u;}else if(b_streq(name,"style")){n->inline_style_off=off;apply_css(d);}else return -1;return 0;
}
