#include "process/dihos_user_elf.h"
static uint64_t get(const uint8_t *p,unsigned bytes){uint64_t v=0;for(unsigned i=0;i<bytes;++i)v|=(uint64_t)p[i]<<(i*8);return v;}
static int range(uint64_t at,uint64_t size,uint64_t bytes){return at<=bytes && size<=bytes-at;}
static int file_offset(const dihos_user_elf_plan *plan,uint64_t address,uint64_t bytes,uint64_t *offset){
    for(unsigned i=0;i<plan->count;++i){const dihos_user_elf_segment *s=&plan->segments[i];
        if(address>=s->address && range(address-s->address,bytes,s->file_bytes)){
            *offset=s->offset+address-s->address;return 0;
        }
    }
    return -1;
}
static int relocation_target(const dihos_user_elf_plan *plan,uint64_t address){
    if(address&7)return 0;
    for(unsigned i=0;i<plan->count;++i){const dihos_user_elf_segment *s=&plan->segments[i];
        if(!(s->flags&1) && address>=s->address && range(address-s->address,8,s->memory_bytes))return 1;
    }
    return 0;
}
static int parse(const void *data,uint64_t bytes,dihos_user_elf_plan *out,int deep){
    if(!out)return -1;*out=(dihos_user_elf_plan){0};
    if(!data || bytes<64 || bytes>DIHOS_USER_ELF_MAX_BYTES)return -1;
    const uint8_t *p=data;
    if(p[0]!=127 || p[1]!='E' || p[2]!='L' || p[3]!='F' || p[4]!=2 || p[5]!=1 || p[6]!=1 ||
       get(p+16,2)!=3 || get(p+18,2)!=183 || get(p+20,4)!=1 || get(p+52,2)!=64 || get(p+54,2)!=56)return -2;
    uint64_t table=get(p+32,8),entry=get(p+24,8);unsigned count=(unsigned)get(p+56,2);
    if(!count || count>16 || table<64 || !range(table,(uint64_t)count*56,bytes))return -3;
    dihos_user_elf_plan plan={0};int entry_ok=0,starts_zero=0;
    const uint8_t *dynamic=0;uint64_t dynamic_bytes=0,dynamic_address=0;
    const uint8_t *tls=0;
    for(unsigned i=0;i<count;++i){
        const uint8_t *h=p+table+i*56;uint32_t type=(uint32_t)get(h,4),flags=(uint32_t)get(h+4,4);
        if(!type)continue;
        if(type==0x6474e551u){if(flags&1)return -4;continue;}
        if(type==7){
            if(tls || flags!=4)return -10;tls=h;continue;
        }
        if(type==2){
            uint64_t offset=get(h+8,8);dynamic_address=get(h+16,8);dynamic_bytes=get(h+32,8);
            if(dynamic || flags!=6 || !dynamic_bytes || dynamic_bytes>64*16 || (dynamic_bytes&15) ||
               (dynamic_address&7) || get(h+40,8)!=dynamic_bytes || !range(offset,dynamic_bytes,bytes))return -9;
            dynamic=p+offset;continue;
        }
        if(type!=1 || plan.count==DIHOS_USER_ELF_MAX_SEGMENTS)return -4;
        dihos_user_elf_segment s={get(h+8,8),get(h+16,8),get(h+32,8),get(h+40,8),flags};
        uint64_t align=get(h+48,8);
        if(!(flags&4) || (flags&~7u) || ((flags&3)==3) || !s.memory_bytes ||
           s.file_bytes>s.memory_bytes || !range(s.offset,s.file_bytes,bytes) ||
           !range(s.address,s.memory_bytes,DIHOS_USER_ELF_MAX_IMAGE_BYTES) || (s.address&4095) ||
           (s.offset&4095) || (align!=4096 && align!=1))return -5;
        uint64_t end=(s.address+s.memory_bytes+4095)&~4095ull;
        for(unsigned n=0;n<plan.count;++n){
            const dihos_user_elf_segment *other=&plan.segments[n];uint64_t other_end=(other->address+other->memory_bytes+4095)&~4095ull;
            if(s.address<other_end && other->address<end)return -6;
        }
        if(!s.address)starts_zero=1;
        if((flags&1) && !(entry&3) && entry>=s.address && entry-s.address<s.file_bytes)entry_ok=1;
        if(end>plan.bytes)plan.bytes=end;
        plan.segments[plan.count++]=s;
    }
    uint64_t tls_address=0;
    if(tls){
        uint64_t offset;
        plan.tls_offset=get(tls+8,8);tls_address=get(tls+16,8);
        plan.tls_file_bytes=get(tls+32,8);plan.tls_memory_bytes=get(tls+40,8);plan.tls_alignment=get(tls+48,8);
        if(!plan.tls_memory_bytes || plan.tls_memory_bytes>4080 || plan.tls_file_bytes>plan.tls_memory_bytes ||
           !plan.tls_alignment || plan.tls_alignment>16 || (plan.tls_alignment&(plan.tls_alignment-1)) ||
           (tls_address&(plan.tls_alignment-1)) || !range(plan.tls_offset,plan.tls_file_bytes,bytes))return -10;
        if(plan.tls_file_bytes){if(file_offset(&plan,tls_address,plan.tls_file_bytes,&offset) || offset!=plan.tls_offset)return -10;}
        else {int located=0;for(unsigned i=0;i<plan.count;++i){const dihos_user_elf_segment *s=&plan.segments[i];
            if(tls_address>=s->address && range(tls_address-s->address,0,s->memory_bytes))located=1;}
            if(!located)return -10;}
    }
    uint64_t rela=0,rela_bytes=0,rela_offset=0;
    if(dynamic){
        uint64_t offset;
        if(file_offset(&plan,dynamic_address,dynamic_bytes,&offset) || p+offset!=dynamic)return -9;
        static const uint64_t tags[]={4,5,6,7,8,9,10,11,21,25,27,30,0x6ffffef5,0x6ffffff9,0x6ffffffb};
        uint64_t values[15]={0};uint32_t seen=0;int terminated=0;
        for(uint64_t n=0;n<dynamic_bytes;n+=16){
            uint64_t tag=get(dynamic+n,8),value=get(dynamic+n+8,8);
            if(!tag){if(value)return -9;terminated=1;continue;}
            if(terminated)return -9;
            unsigned index=0;while(index<15 && tags[index]!=tag)++index;
            if(index==15 || (seen&(1u<<index)))return -9;
            seen|=1u<<index;values[index]=value;
        }
        if(!terminated || (values[11]&~2ull) || (values[14]&~0x8000000ull) || values[8])return -9;
        if((seen&(1u<<7)) && values[7]!=24)return -9;
        const unsigned pointers[]={0,1,2,12};
        for(unsigned i=0;i<4;++i){unsigned at=pointers[i];
            uint64_t size=at==0?8:at==2?24:at==12?16:1;
            if((seen&(1u<<at)) && file_offset(&plan,values[at],size,&offset))return -9;
        }
        if((seen&(1u<<6)) && (!(seen&(1u<<1)) || !values[6] || file_offset(&plan,values[1],values[6],&offset)))return -9;
        if(seen&((1u<<9)|(1u<<10))){
            if((seen&((1u<<9)|(1u<<10)))!=((1u<<9)|(1u<<10)) || (values[10]&7) ||
               file_offset(&plan,values[9],values[10],&offset))return -9;
        }
        if(seen&((1u<<3)|(1u<<4)|(1u<<5)|(1u<<13))){
            if((seen&((1u<<3)|(1u<<4)|(1u<<5)))!=((1u<<3)|(1u<<4)|(1u<<5)) || values[5]!=24 ||
               !values[4] || values[4]%24 || values[4]/24>DIHOS_USER_ELF_MAX_RELOCATIONS ||
               (values[3]&7) || file_offset(&plan,values[3],values[4],&rela_offset))return -9;
            rela=values[3];rela_bytes=values[4];
            plan.relocation_count=(uint32_t)(rela_bytes/24);
            if((seen&(1u<<13)) && values[13]!=plan.relocation_count)return -9;
            plan.relocation_offset=rela_offset;
            for(unsigned i=0;deep && i<plan.relocation_count;++i){
                const uint8_t *r=p+rela_offset+i*24;
                uint64_t address=get(r,8),info=get(r+8,8),addend=get(r+16,8);
                if(info!=1027 || addend>=plan.bytes || !relocation_target(&plan,address))return -9;
                if(tls && address<tls_address+plan.tls_memory_bytes && tls_address<address+8)return -10;
                if(plan.relocation_count>DIHOS_USER_ELF_INLINE_RELOCATIONS){
                    if(i && address<=get(p+rela_offset+(i-1)*24,8))return -9;
                }else for(unsigned j=0;j<i;++j)if(plan.relocations[j].address==address)return -9;
                if(i<DIHOS_USER_ELF_INLINE_RELOCATIONS)plan.relocations[i]=(dihos_user_elf_relocation){address,addend};
            }
        }
    }
    /* Dynamic records are authoritative; reject extra REL/RELR or unrelated
     * nonempty RELA sections rather than silently ignoring fixups. */
    uint64_t sections=get(p+40,8);unsigned section_count=(unsigned)get(p+60,2);
    if(!section_count && sections)return -7; /* extended numbering unsupported */
    if(section_count){
        if(section_count>512 || get(p+58,2)!=64 || !range(sections,(uint64_t)section_count*64,bytes))return -7;
        for(unsigned i=0;i<section_count;++i){const uint8_t *s=p+sections+i*64;uint64_t type=get(s+4,4);
            if((type==9 || type==19) && get(s+32,8))return -7;
            if(type==4 && get(s+32,8) && (!rela_bytes || get(s+16,8)!=rela || get(s+24,8)!=rela_offset ||
               get(s+32,8)!=rela_bytes || get(s+56,8)!=24))return -7;
        }
    }
    if(!starts_zero || !entry_ok || !plan.count)return -8;
    plan.entry=entry;plan.tls_address=tls_address;plan.source=p;plan.source_bytes=bytes;*out=plan;return 0;
}
int dihos_user_elf_headers(const void *data,uint64_t bytes,dihos_user_elf_plan *out){return parse(data,bytes,out,0);}
int dihos_user_elf_validate(const void *data,uint64_t bytes,dihos_user_elf_plan *out){return parse(data,bytes,out,1);}
static int record(const dihos_user_elf_plan *p,unsigned i,dihos_user_elf_relocation *out){
    if(!p || i>=p->relocation_count || p->relocation_count>DIHOS_USER_ELF_MAX_RELOCATIONS ||
       !p->count || p->count>DIHOS_USER_ELF_MAX_SEGMENTS || !p->source ||
       p->source_bytes>DIHOS_USER_ELF_MAX_BYTES || (uintptr_t)p->source>UINT64_MAX-p->source_bytes ||
       !range(p->relocation_offset,(uint64_t)p->relocation_count*24,p->source_bytes))return -9;
    const uint8_t *at=p->source+p->relocation_offset+(uint64_t)i*24;
    uint64_t address=get(at,8),addend=get(at+16,8);
    if(get(at+8,8)!=1027 || addend>=p->bytes || !relocation_target(p,address))return -9;
    if(p->tls_memory_bytes && address<p->tls_address+p->tls_memory_bytes && p->tls_address<address+8)return -10;
    if(p->relocation_count>DIHOS_USER_ELF_INLINE_RELOCATIONS && i && address<=get(at-24,8))return -9;
    *out=(dihos_user_elf_relocation){address,addend};return 0;
}
int dihos_user_elf_validate_step(dihos_user_elf_plan *p,unsigned first,unsigned count){
    if(!p || count>256 || first>p->relocation_count || count>p->relocation_count-first)return -9;
    for(unsigned i=first;i<first+count;++i){dihos_user_elf_relocation r;int rc=record(p,i,&r);if(rc)return rc;
        if(p->relocation_count<=DIHOS_USER_ELF_INLINE_RELOCATIONS){
            for(unsigned j=0;j<i;++j)if(p->relocations[j].address==r.address)return -9;
            p->relocations[i]=r;
        }
    }return 0;
}
static dihos_user_elf_relocation relocated_record(const dihos_user_elf_plan *p,unsigned i){
    if(p->relocation_count<=DIHOS_USER_ELF_INLINE_RELOCATIONS)return p->relocations[i];
    const uint8_t *at=p->source+p->relocation_offset+(uint64_t)i*24;
    return (dihos_user_elf_relocation){get(at,8),get(at+16,8)};
}
int dihos_user_elf_relocate(const dihos_user_elf_plan *plan,void *image,uint64_t bytes){
    if(!plan || !image || !plan->bytes || plan->bytes>bytes || plan->bytes>DIHOS_USER_ELF_MAX_IMAGE_BYTES ||
       plan->count>DIHOS_USER_ELF_MAX_SEGMENTS || plan->relocation_count>DIHOS_USER_ELF_MAX_RELOCATIONS ||
       (uintptr_t)image>UINT64_MAX-plan->bytes)return -9;
    for(unsigned i=0;i<plan->relocation_count;++i){
        dihos_user_elf_relocation value;
        if(plan->relocation_count>DIHOS_USER_ELF_INLINE_RELOCATIONS){if(record(plan,i,&value))return -9;}
        else value=relocated_record(plan,i);
        const dihos_user_elf_relocation *r=&value;
        if(!range(r->address,8,plan->bytes) || r->addend>=plan->bytes || !relocation_target(plan,r->address))return -9;
        if(plan->relocation_count<=DIHOS_USER_ELF_INLINE_RELOCATIONS)for(unsigned j=0;j<i;++j)if(plan->relocations[j].address==r->address)return -9;
    }
    for(unsigned i=0;i<plan->relocation_count;++i){
        dihos_user_elf_relocation r=relocated_record(plan,i);uint64_t value=(uintptr_t)image+r.addend;
        for(unsigned j=0;j<8;++j)((uint8_t*)image)[r.address+j]=(uint8_t)(value>>(j*8));
    }
    return 0;
}
int dihos_user_elf_relocate_step(const dihos_user_elf_plan *p,void *image,uint64_t bytes,unsigned first,unsigned count){
    if(!p || !image || p->bytes>bytes || p->bytes>DIHOS_USER_ELF_MAX_IMAGE_BYTES || count>256 ||
       first>p->relocation_count || count>p->relocation_count-first || (uintptr_t)image>UINT64_MAX-p->bytes)return -9;
    for(unsigned i=first;i<first+count;++i){dihos_user_elf_relocation r;if(record(p,i,&r))return -9;
        if(p->relocation_count<=DIHOS_USER_ELF_INLINE_RELOCATIONS &&
           (r.address!=p->relocations[i].address || r.addend!=p->relocations[i].addend))return -9;}
    for(unsigned i=first;i<first+count;++i){dihos_user_elf_relocation r=relocated_record(p,i);uint64_t value=(uintptr_t)image+r.addend;
        for(unsigned n=0;n<8;++n)((uint8_t*)image)[r.address+n]=(uint8_t)(value>>(8*n));}
    return 0;
}
