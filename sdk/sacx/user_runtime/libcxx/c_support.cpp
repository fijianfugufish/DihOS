#include <stddef.h>
// Required by libc++ char_traits/string searches. No host libc is linked.
extern "C" void *memchr(const void *data,int value,size_t bytes){
    auto input=static_cast<const unsigned char*>(data);
    for(size_t i=0;i<bytes;++i)if(input[i]==static_cast<unsigned char>(value))
        return const_cast<unsigned char*>(input+i);
    return nullptr;
}
