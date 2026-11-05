/* Private sparse arena and bounded scatter operations. Kernel root is inert. */
static int vm_hole_identity(uint64_t desc,uint32_t level,uint64_t va,
                            uint32_t normal,unsigned *budget){
    if(!*budget)return -95;--*budget;
    if(!(desc&A64_DESC_VALID))return 0;
    if(level<3 && (desc&A64_DESC_TABLE)){
        const uint64_t *table=pmem_phys_to_virt(desc&A64_ADDR_MASK);
        if(!table)return -14;
        for(unsigned i=0;i<512;++i){
            int rc=vm_hole_identity(table[i],level+1,va+(uint64_t)i*level_size(level+1),normal,budget);
            if(rc)return rc;
        }
        return 0;
    }
    if((desc&A64_ADDR_MASK)!=va || (desc&(1ull<<6)) || ((desc>>2)&7)!=normal)return -14;
    return 0;
}
int aarch64_user_vm_claim_sparse_arena(aarch64_user_vm *vm,uint64_t base,uint64_t bytes){
    const uint64_t gb=1ull<<30;
    if(!vm || !vm->root_phys || vm->va_bits<39 || vm->va_bits>48 ||
       !bytes || ((base|bytes)&(gb-1)) || bytes>16*gb || base>=512*gb || bytes>512*gb-base ||
       !pmem_user_arena_available(base,bytes))return -22;
    uint64_t *root=pmem_phys_to_virt(vm->root_phys),*level1=root;
    if(!root || !table_is_owned(vm,vm->root_phys))return -14;
    if(vm_start_level(vm->va_bits)==0){
        uint64_t d=root[0];
        if((d&3)!=3)return -14;
        level1=pmem_phys_to_virt(d&A64_ADDR_MASK);
    }
    if(!level1)return -14;
    unsigned budget=16384; /* Includes a full 16 GiB of 2 MiB firmware leaves. */
    for(uint64_t va=base;va<base+bytes;va+=gb){
        int rc=vm_hole_identity(level1[level_index(va,1)],1,va,vm->normal_memory_attr_index,&budget);
        if(rc)return rc;
    }
    if(vm_start_level(vm->va_bits)==0){
        level1=vm_private_child(vm,root,0,0);if(!level1)return -12;
    }
    for(uint64_t va=base;va<base+bytes;va+=gb)level1[level_index(va,1)]=0;
    return aarch64_user_vm_publish(vm);
}
static int vm_scatter_valid(aarch64_user_vm *vm,uint64_t va,const uint64_t *physical,unsigned pages){
    if(!vm || !vm->root_phys || !table_is_owned(vm,vm->root_phys) || !physical || !pages || pages>AARCH64_USER_VM_SCATTER_MAX ||
       (va&4095) || vm->va_bits<39 || vm->va_bits>48 || va>=(1ull<<vm->va_bits) ||
       (uint64_t)pages*4096>(1ull<<vm->va_bits)-va)return -22;
    for(unsigned i=0;i<pages;++i)if((physical[i]&4095) || (physical[i]&~A64_ADDR_MASK))return -22;
    return 0;
}
int aarch64_user_vm_map_scatter(aarch64_user_vm *vm,uint64_t va,const uint64_t *physical,unsigned pages,uint32_t permissions){
    int rc=vm_scatter_valid(vm,va,physical,pages);if(rc)return rc;
    if(permissions!=AARCH64_USER_VM_READ && permissions!=(AARCH64_USER_VM_READ|AARCH64_USER_VM_WRITE))return -22;
    for(unsigned i=0;i<pages;++i)if(!vm_address_is_unmapped(vm,va+(uint64_t)i*4096))return -17;
    struct {uint64_t *at,old;} undo[AARCH64_USER_VM_MAX_TABLES+AARCH64_USER_VM_SCATTER_MAX];
    unsigned changes=0,initial=vm->owned_table_count;
    for(unsigned i=0;i<pages;++i){
        uint64_t address=va+(uint64_t)i*4096,*table=pmem_phys_to_virt(vm->root_phys);
        for(unsigned level=vm_start_level(vm->va_bits);level<3;++level){
            uint64_t *at=&table[level_index(address,level)],old=*at;
            table=vm_private_child(vm,table,level_index(address,level),level);
            if(*at!=old){undo[changes].at=at;undo[changes++].old=old;}
            if(!table)goto rollback;
        }
        uint64_t *at=&table[level_index(address,3)];
        undo[changes].at=at;undo[changes++].old=*at;
        *at=make_user_page_desc(physical[i],permissions,vm->normal_memory_attr_index);
    }
    return aarch64_user_vm_publish(vm);
rollback:
    while(changes){--changes;*undo[changes].at=undo[changes].old;}
    while(vm->owned_table_count>initial){
        --vm->owned_table_count;pmem_free_pages(vm->owned_tables[vm->owned_table_count],1);
        vm->owned_tables[vm->owned_table_count]=0;
    }
    aarch64_user_vm_publish(vm);return -12;
}
static uint64_t *vm_sparse_leaf(aarch64_user_vm *vm,uint64_t va,uint64_t physical){
    uint64_t *table=pmem_phys_to_virt(vm->root_phys);
    for(unsigned level=vm_start_level(vm->va_bits);level<3;++level){
        uint64_t d=table[level_index(va,level)];
        if((d&3)!=3 || !table_is_owned(vm,d&A64_ADDR_MASK))return 0;
        table=pmem_phys_to_virt(d&A64_ADDR_MASK);
    }
    uint64_t *leaf=&table[level_index(va,3)];
    return ((*leaf&3)==3 && (*leaf&A64_ADDR_MASK)==physical)?leaf:0;
}
static void vm_prune_sparse(aarch64_user_vm *vm,uint64_t va){
    uint64_t *tables[4]={0};unsigned start=vm_start_level(vm->va_bits);
    tables[start]=pmem_phys_to_virt(vm->root_phys);
    for(unsigned level=start;level<3;++level){
        uint64_t d=tables[level][level_index(va,level)];
        if((d&3)!=3 || !table_is_owned(vm,d&A64_ADDR_MASK))return;
        tables[level+1]=pmem_phys_to_virt(d&A64_ADDR_MASK);
    }
    for(unsigned level=3;level>start;--level){
        for(unsigned i=0;i<512;++i)if(tables[level][i]&1)return;
        tables[level-1][level_index(va,level-1)]=0;
        for(unsigned i=0;i<vm->owned_table_count;++i)if(vm->owned_tables[i]==tables[level]){
            for(unsigned j=i+1;j<vm->owned_table_count;++j)vm->owned_tables[j-1]=vm->owned_tables[j];
            vm->owned_tables[--vm->owned_table_count]=0;break;
        }
        pmem_free_pages(tables[level],1);
    }
}
static int vm_update_scatter(aarch64_user_vm *vm,uint64_t va,const uint64_t *physical,unsigned pages,uint32_t permissions,int unmap){
    int rc=vm_scatter_valid(vm,va,physical,pages);if(rc)return rc;
    if(permissions && permissions!=AARCH64_USER_VM_READ && permissions!=(AARCH64_USER_VM_READ|AARCH64_USER_VM_WRITE))return -22;
    uint64_t *leaves[AARCH64_USER_VM_SCATTER_MAX];
    for(unsigned i=0;i<pages;++i){
        leaves[i]=vm_sparse_leaf(vm,va+(uint64_t)i*4096,physical[i]);if(!leaves[i])return -14;
    }
    for(unsigned i=0;i<pages;++i){
        uint64_t d=make_user_page_desc(physical[i],permissions,vm->normal_memory_attr_index);
        if(!permissions)d&=~(3ull<<6);
        *leaves[i]=unmap?0:d;
    }
    if(unmap)for(unsigned i=0;i<pages;++i)vm_prune_sparse(vm,va+(uint64_t)i*4096);
    return aarch64_user_vm_publish(vm);
}
int aarch64_user_vm_protect_scatter(aarch64_user_vm *vm,uint64_t va,const uint64_t *pa,unsigned n,uint32_t p){return vm_update_scatter(vm,va,pa,n,p,0);}
int aarch64_user_vm_unmap_scatter(aarch64_user_vm *vm,uint64_t va,const uint64_t *pa,unsigned n){return vm_update_scatter(vm,va,pa,n,0,1);}
