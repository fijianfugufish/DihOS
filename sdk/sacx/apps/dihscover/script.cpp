#include "browser.h"

typedef struct script_timer{uint8_t used;uint64_t due;char source[512];}script_timer;
static browser_document*g_document;static uint8_t g_running;static script_timer g_timers[16];
static int js_space(char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}

static const char*find_text(const char*s,const char*n){uint32_t z=b_strlen(n);if(!s||!z)return 0;for(;*s;++s){uint32_t i=0;while(i<z&&s[i]==n[i])++i;if(i==z)return s;}return 0;}
static const char*quoted(const char*s,char*out,uint32_t cap){uint32_t n=0;char q;while(*s&&*s!='\''&&*s!='"')++s;if(!*s)return 0;q=*s++;while(*s&&*s!=q&&n+1<cap){if(*s=='\\'&&s[1])++s;out[n++]=*s++;}out[n]=0;return *s==q?s+1:0;}
static int between(const char*a,const char*b,const char*needle){const char*p=find_text(a,needle);return p&&p<b;}

static int execute_dom_updates(const char*source)
{
    int changed=0;while(source&&*source){const char*a=find_text(source,"getElementById");const char*q=find_text(source,"querySelector");const char*at=(!a||(q&&q<a))?q:a;if(!at)break;char id[96],value[256],property[64];const char*after=quoted(at,id,sizeof(id));if(!after)break;if(at==q&&id[0]=='#'){for(uint32_t i=0;id[i];++i)id[i]=id[i+1];}
        const char*equals=find_text(after,"=");if(!equals)break;const char*end=quoted(equals+1,value,sizeof(value));if(!end){source=equals+1;continue;}
        if(between(after,equals,".style.")){const char*p=find_text(after,".style.")+7;uint32_t n=0;while(p+n<equals&&!js_space(p[n])&&p[n]!='=')++n;b_copy_n(property,sizeof(property),p,n);if(browser_document_set_style_by_id(g_document,id,property,value)==0)++changed;}
        else if(between(after,equals,"textContent")||between(after,equals,"innerText")||between(after,equals,"innerHTML")){if(browser_document_set_text_by_id(g_document,id,value)==0)++changed;}
        source=end;
    }return changed;
}

static void schedule_timers(const char*source,uint64_t now)
{
    const char*p=source;while((p=find_text(p,"setTimeout"))!=0){const char*open=find_text(p,"{");if(!open)break;const char*close=open+1;uint32_t depth=1;char quote=0;while(*close&&depth){if(quote){if(*close=='\\'&&close[1])++close;else if(*close==quote)quote=0;}else if(*close=='\''||*close=='"')quote=*close;else if(*close=='{')++depth;else if(*close=='}')--depth;if(depth)++close;}if(depth)break;const char*comma=find_text(close,",");uint32_t delay=0;if(comma){++comma;while(js_space(*comma))++comma;while(*comma>='0'&&*comma<='9'){if(delay<600000u)delay=delay*10u+(uint32_t)(*comma-'0');++comma;}}
        for(uint32_t i=0;i<16;++i)if(!g_timers[i].used){uint32_t n=(uint32_t)(close-(open+1));if(n>=sizeof(g_timers[i].source))n=sizeof(g_timers[i].source)-1;b_copy_n(g_timers[i].source,sizeof(g_timers[i].source),open+1,n);g_timers[i].due=now+(delay?delay:1u);g_timers[i].used=1;break;}p=close+1;}
}

int browser_scripts_start(browser_document*doc,uint64_t now)
{
    int changed=0;uint32_t scripts=0,total=0;g_document=doc;g_running=1;b_memset(g_timers,0,sizeof(g_timers));if(!doc)return 0;for(uint32_t i=1;i<doc->node_count&&scripts<8u&&total<32768u;++i)if(doc->nodes[i].tag==B_TAG_SCRIPT&&doc->nodes[i].text_off){const char*s=browser_document_string(doc,doc->nodes[i].text_off);uint32_t n=b_strlen(s);if(n>4096u||(!find_text(s,"getElementById")&&!find_text(s,"querySelector")))continue;total+=n;++scripts;schedule_timers(s,now);if(!find_text(s,"setTimeout"))changed+=execute_dom_updates(s);}return changed;
}
int browser_scripts_pump(browser_document*doc,uint64_t now,uint32_t budget){(void)doc;if(!g_running)return 0;int changed=0;for(uint32_t i=0;i<16&&budget;++i)if(g_timers[i].used&&now>=g_timers[i].due){changed+=execute_dom_updates(g_timers[i].source);g_timers[i].used=0;--budget;}return changed;}
int browser_scripts_click(browser_document*doc,uint32_t index){if(!g_running||!doc||index>=doc->node_count||!doc->nodes[index].onclick_off)return 0;return execute_dom_updates(browser_document_string(doc,doc->nodes[index].onclick_off));}
void browser_scripts_stop(void){g_document=0;g_running=0;b_memset(g_timers,0,sizeof(g_timers));}
