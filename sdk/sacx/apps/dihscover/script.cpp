#include "browser.h"

typedef struct script_timer { uint8_t used; uint64_t due; char source[768]; } script_timer;
typedef struct script_handler { uint8_t used; uint32_t node; char source[768]; } script_handler;

static browser_document *g_document;
static uint8_t g_running;
static script_timer g_timers[24];
static script_handler g_click_handlers[48];

static int js_space(char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}
static const char*find_text(const char*s,const char*n){uint32_t z=b_strlen(n);if(!s||!z)return 0;for(;*s;++s){uint32_t i=0;while(i<z&&s[i]==n[i])++i;if(i==z)return s;}return 0;}
static const char*quoted(const char*s,char*out,uint32_t cap){uint32_t n=0;char q;while(*s&&*s!='\''&&*s!='"')++s;if(!*s)return 0;q=*s++;while(*s&&*s!=q&&n+1u<cap){if(*s=='\\'&&s[1])++s;out[n++]=*s++;}out[n]=0;return *s==q?s+1:0;}
static const char*statement_end(const char*s){uint32_t paren=0u,brace=0u;char quote=0;for(;*s;++s){char c=*s;if(quote){if(c=='\\'&&s[1])++s;else if(c==quote)quote=0;}else if(c=='\''||c=='"')quote=c;else if(c=='(')++paren;else if(c==')'&&paren)--paren;else if(c=='{')++brace;else if(c=='}'&&brace)--brace;else if(c==';'&&!paren&&!brace)return s;}return s;}
static int between(const char*a,const char*b,const char*needle){const char*p=find_text(a,needle);return p&&p<b;}

static int target_from_call(const char*at,int query,const char**after)
{
    char selector[192];const char*end=quoted(at,selector,sizeof(selector));if(!end)return -1;if(!query){char id[192]="#";b_copy(id+1u,sizeof(id)-1u,selector);b_copy(selector,sizeof(selector),id);}if(after)*after=end;return browser_document_find_selector(g_document,selector);
}

static int execute_dom_updates(const char*source)
{
    int changed=0;uint32_t guard=0u;
    while(source&&*source&&guard++<128u){const char*a=find_text(source,"getElementById");const char*q=find_text(source,"querySelector");const char*at=(!a||(q&&q<a))?q:a;if(!at)break;int query=at==q;const char*after=0;int target=target_from_call(at,query,&after);if(!after){source=at+1;continue;}const char*end=statement_end(after);if(target<0){source=*end?end+1:end;continue;}
        const char*class_list=find_text(after,".classList.");
        if(class_list&&class_list<end){class_list+=11;uint32_t op=0xffffffffu;if(b_starts(class_list,"add"))op=0u;else if(b_starts(class_list,"remove"))op=1u;else if(b_starts(class_list,"toggle"))op=2u;if(op!=0xffffffffu){char name[128];if(quoted(class_list,name,sizeof(name))&&browser_document_set_class(g_document,(uint32_t)target,name,op)==0)++changed;}source=*end?end+1:end;continue;}
        const char*set_attr=find_text(after,".setAttribute");const char*remove_attr=find_text(after,".removeAttribute");
        if((set_attr&&set_attr<end)||(remove_attr&&remove_attr<end)){uint8_t remove=(uint8_t)(!set_attr||(remove_attr&&remove_attr<set_attr));const char*call=remove?remove_attr:set_attr;char name[96],value[256]="";const char*next=quoted(call,name,sizeof(name));if(next&&(!remove||quoted(next,value,sizeof(value)))&&browser_document_set_attribute(g_document,(uint32_t)target,name,value,remove)==0)++changed;source=*end?end+1:end;continue;}
        const char*equals=find_text(after,"=");if(!equals||equals>=end){source=*end?end+1:end;continue;}char value[512];const char*value_end=quoted(equals+1,value,sizeof(value));if(!value_end){const char*p=equals+1;while(js_space(*p))++p;if(b_starts(p,"true"))b_copy(value,sizeof(value),"true");else if(b_starts(p,"false")||b_starts(p,"null"))b_copy(value,sizeof(value),"false");else{source=*end?end+1:end;continue;}}
        if(between(after,equals,".style.")){const char*p=find_text(after,".style.")+7;char property[80];uint32_t n=0u;while(p+n<equals&&!js_space(p[n])&&p[n]!='=')++n;b_copy_n(property,sizeof(property),p,n);if(browser_document_set_style(g_document,(uint32_t)target,property,value)==0)++changed;}
        else if(between(after,equals,"textContent")||between(after,equals,"innerText")||between(after,equals,"innerHTML")){if(browser_document_set_text(g_document,(uint32_t)target,value)==0)++changed;}
        else if(between(after,equals,".value")){if(browser_document_set_attribute(g_document,(uint32_t)target,"value",value,0u)==0)++changed;}
        else if(between(after,equals,".hidden")){if(browser_document_set_attribute(g_document,(uint32_t)target,"hidden","",(uint8_t)b_streq(value,"false"))==0)++changed;}
        source=*end?end+1:end;
    }
    return changed;
}

static void register_click_handlers(const char*source)
{
    const char*p=source;uint32_t guard=0u;while(p&&*p&&guard++<64u){const char*a=find_text(p,"getElementById");const char*q=find_text(p,"querySelector");const char*at=(!a||(q&&q<a))?q:a;if(!at)break;const char*after=0;int target=target_from_call(at,at==q,&after);const char*listen=after?find_text(after,"addEventListener"):0;if(target<0||!listen||listen-after>256){p=after?after:at+1;continue;}char event[32];const char*event_end=quoted(listen,event,sizeof(event));if(!event_end||!b_streq(event,"click")){p=listen+1;continue;}const char*open=find_text(event_end,"{");if(!open){p=event_end;continue;}const char*close=open+1;uint32_t depth=1u;char quote=0;while(*close&&depth){char c=*close;if(quote){if(c=='\\'&&close[1])++close;else if(c==quote)quote=0;}else if(c=='\''||c=='"')quote=c;else if(c=='{')++depth;else if(c=='}')--depth;if(depth)++close;}if(depth)break;for(uint32_t i=0u;i<48u;++i)if(!g_click_handlers[i].used){uint32_t n=(uint32_t)(close-open-1);if(n>=sizeof(g_click_handlers[i].source))n=sizeof(g_click_handlers[i].source)-1u;b_copy_n(g_click_handlers[i].source,sizeof(g_click_handlers[i].source),open+1,n);g_click_handlers[i].node=(uint32_t)target;g_click_handlers[i].used=1u;break;}p=close+1;
    }
}

static void schedule_timers(const char*source,uint64_t now)
{
    const char*p=source;while((p=find_text(p,"setTimeout"))!=0){const char*open=find_text(p,"{");if(!open)break;const char*close=open+1;uint32_t depth=1u;char quote=0;while(*close&&depth){if(quote){if(*close=='\\'&&close[1])++close;else if(*close==quote)quote=0;}else if(*close=='\''||*close=='"')quote=*close;else if(*close=='{')++depth;else if(*close=='}')--depth;if(depth)++close;}if(depth)break;const char*comma=find_text(close,",");uint32_t delay=0u;if(comma){++comma;while(js_space(*comma))++comma;while(*comma>='0'&&*comma<='9'){if(delay<600000u)delay=delay*10u+(uint32_t)(*comma-'0');++comma;}}
        for(uint32_t i=0u;i<24u;++i)if(!g_timers[i].used){uint32_t n=(uint32_t)(close-open-1);if(n>=sizeof(g_timers[i].source))n=sizeof(g_timers[i].source)-1u;b_copy_n(g_timers[i].source,sizeof(g_timers[i].source),open+1,n);g_timers[i].due=now+(delay?delay:1u);g_timers[i].used=1u;break;}p=close+1;}
}

int browser_scripts_start(browser_document*doc,uint64_t now)
{
    int changed=0;uint32_t scripts=0u,total=0u;g_document=doc;g_running=1u;b_memset(g_timers,0,sizeof(g_timers));b_memset(g_click_handlers,0,sizeof(g_click_handlers));if(!doc)return 0;
    for(uint32_t i=1u;i<doc->node_count&&scripts<16u&&total<65536u;++i)if(doc->nodes[i].tag==B_TAG_SCRIPT&&doc->nodes[i].text_off){const char*s=browser_document_string(doc,doc->nodes[i].text_off);uint32_t n=b_strlen(s);if(n>8192u||(!find_text(s,"getElementById")&&!find_text(s,"querySelector")))continue;total+=n;++scripts;register_click_handlers(s);schedule_timers(s,now);if(!find_text(s,"setTimeout")&&!find_text(s,"addEventListener"))changed+=execute_dom_updates(s);}
    return changed;
}
int browser_scripts_pump(browser_document*doc,uint64_t now,uint32_t budget){(void)doc;if(!g_running)return 0;int changed=0;for(uint32_t i=0u;i<24u&&budget;++i)if(g_timers[i].used&&now>=g_timers[i].due){changed+=execute_dom_updates(g_timers[i].source);g_timers[i].used=0u;--budget;}return changed;}
int browser_scripts_click(browser_document*doc,uint32_t index)
{
    if(!g_running||!doc||index>=doc->node_count)return 0;int changed=0;for(int32_t node=(int32_t)index;node>0;node=doc->nodes[node].parent){if(doc->nodes[node].onclick_off)changed+=execute_dom_updates(browser_document_string(doc,doc->nodes[node].onclick_off));for(uint32_t i=0u;i<48u;++i)if(g_click_handlers[i].used&&g_click_handlers[i].node==(uint32_t)node)changed+=execute_dom_updates(g_click_handlers[i].source);}return changed;
}
void browser_scripts_stop(void){g_document=0;g_running=0u;b_memset(g_timers,0,sizeof(g_timers));b_memset(g_click_handlers,0,sizeof(g_click_handlers));}
