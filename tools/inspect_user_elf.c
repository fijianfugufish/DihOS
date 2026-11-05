/* Host-only inspector using the production guest validator; never executes ELF. */
#include <stdio.h>
#include <stdlib.h>
#include "process/dihos_user_elf.h"
int main(int argc,char **argv){
    if(argc!=2)return 2;
    FILE *file=fopen(argv[1],"rb");if(!file)return 2;
    if(fseek(file,0,SEEK_END)){fclose(file);return 2;}
    long length=ftell(file);if(length<0 || fseek(file,0,SEEK_SET)){fclose(file);return 2;}
    void *bytes=malloc(length?length:1);if(!bytes){fclose(file);return 2;}
    if(fread(bytes,1,length,file)!=(size_t)length){free(bytes);fclose(file);return 2;}
    fclose(file);dihos_user_elf_plan plan={0};
    int result=dihos_user_elf_validate(bytes,length,&plan);
    printf("{\"result\":%d,\"file_bytes\":%llu,\"image_bytes\":%llu,\"segments\":%u,\"relocations\":%u,\"tls_bytes\":%llu,\"file_limit\":%u,\"image_limit\":%u}\n",
        result,(unsigned long long)length,(unsigned long long)plan.bytes,plan.count,
        plan.relocation_count,(unsigned long long)plan.tls_memory_bytes,
        DIHOS_USER_ELF_MAX_BYTES,DIHOS_USER_ELF_MAX_IMAGE_BYTES);
    free(bytes);return result?1:0;
}
