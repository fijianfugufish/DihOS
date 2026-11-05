// Included inside namespace bmalloc after the common VM validation helpers.
inline size_t vmPageSize(){return 4096;}
inline size_t vmPageSizePhysical(){return 4096;}
inline void* tryVMAllocate(size_t size,VMTag usage){
    BUNUSED(usage);vmValidate(size);
    void *base=dihos_user_vm_reserve(size,4096,0);
    if(base && dihos_user_vm_commit(base,size,1)){
        RELEASE_BASSERT(!dihos_user_vm_release(base,size));return nullptr;
    }
    return base;
}
inline void vmDeallocate(void *base,size_t size){vmValidate(base,size);RELEASE_BASSERT(!dihos_user_vm_release(base,size));}
inline void vmRevokePermissions(void *base,size_t size){vmValidate(base,size);RELEASE_BASSERT(!dihos_user_vm_protect(base,size,0,0));}
inline void vmZeroAndPurge(void *base,size_t size,VMTag usage){BUNUSED(usage);vmValidate(base,size);RELEASE_BASSERT(!dihos_user_vm_zero(base,size));}
inline void vmDeallocatePhysicalPages(void *base,size_t size){vmValidatePhysical(base,size);RELEASE_BASSERT(!dihos_user_vm_decommit(base,size));}
inline void vmAllocatePhysicalPages(void *base,size_t size){vmValidatePhysical(base,size);RELEASE_BASSERT(!dihos_user_vm_commit(base,size,1));}
