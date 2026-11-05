#include "fenv.h"
namespace {
uint64_t control(){uint64_t v;__asm__ volatile("mrs %0,fpcr":"=r"(v));return v;}
uint64_t status(){uint64_t v;__asm__ volatile("mrs %0,fpsr":"=r"(v));return v;}
void setControl(uint64_t v){__asm__ volatile("msr fpcr,%0; isb"::"r"(v):"memory");}
void setStatus(uint64_t v){__asm__ volatile("msr fpsr,%0"::"r"(v):"memory");}
}
extern "C" {
int fegetround(){return control()&0xc00000;}
int fesetround(int mode){if(mode&~0xc00000)return 1;setControl((control()&~0xc00000ull)|mode);return 0;}
int fegetenv(fenv_t *out){if(!out)return 1;*out={control(),status()};return 0;}
int fesetenv(const fenv_t *in){
    if(!in)return 1;
    if(in==FE_DFL_ENV){setControl(0);setStatus(0);return 0;}
    // This runtime does not provide user signal handlers for enabled FP traps.
    if(in->fpcr&0x9f00)return 1;
    setControl(in->fpcr);setStatus(in->fpsr);return 0;
}
int feclearexcept(int flags){setStatus(status()&~(flags&FE_ALL_EXCEPT));return 0;}
int fetestexcept(int flags){return status()&flags&FE_ALL_EXCEPT;}
int fegetexceptflag(fexcept_t *out,int flags){if(!out)return 1;*out=fetestexcept(flags);return 0;}
int fesetexceptflag(const fexcept_t *in,int flags){
    if(!in)return 1;flags&=FE_ALL_EXCEPT;setStatus((status()&~(uint64_t)flags)|(*in&flags));return 0;
}
int feraiseexcept(int flags){
    flags&=FE_ALL_EXCEPT;
    if(control()&((uint64_t)flags<<8))return 1;
    setStatus(status()|flags);return 0;
}
int feholdexcept(fenv_t *out){if(fegetenv(out))return 1;setControl(control()&~0x9f00ull);feclearexcept(FE_ALL_EXCEPT);return 0;}
int feupdateenv(const fenv_t *in){int flags=fetestexcept(FE_ALL_EXCEPT);if(fesetenv(in))return 1;return feraiseexcept(flags);}
}
