// Private musl FILE layout, native SACX synchronization and diagnostic streams.
// No descriptors, host environment, file access or Linux syscall implementation.
#include "stdio_impl.h"
#include <errno.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#undef weak
#include "sacx_user_sync.hpp"
extern "C" {FILE __stdin_FILE{},__stdout_FILE{},__stderr_FILE{};}

namespace {
thread_local char errorText[48];
struct Console {FILE *file;sacx_user_recursive_mutex mutex{};unsigned char buffer[1]{};bool closed=false;};
Console input{&__stdin_FILE},output{&__stdout_FILE},errorOutput{&__stderr_FILE};
[[noreturn]] void invalidStream(){
    static const char text[]="C stdio: invalid native stream or lock";
    sacx_user_call(SACX_USER_LOG,reinterpret_cast<uintptr_t>(text),sizeof(text)-1,0);
    sacx_user_call(SACX_USER_EXIT,201,0,0);
    for(;;)sacx_user_call(SACX_USER_YIELD,0,0,0);
}
Console& console(FILE *file){
    if(file==input.file)return input;
    if(file==output.file)return output;
    if(file==errorOutput.file)return errorOutput;
    invalidStream();
}
size_t emit(const unsigned char *data,size_t bytes){
    size_t done=0;
    while(done<bytes){
        // Diagnostic text devices ignore NUL; split so later bytes are retained.
        if(!data[done]){++done;continue;}
        size_t count=0;
        while(count<SACX_USER_BUFFER_MAX && count<bytes-done && data[done+count])++count;
        auto status=sacx_user_call(SACX_USER_LOG,reinterpret_cast<uintptr_t>(data+done),count,0);
        if(status<0){errno=static_cast<int>(-status);return done;}
        done+=count;
    }
    return done;
}
size_t writeConsole(FILE *file,const unsigned char *data,size_t bytes){
    auto& stream=console(file);
    if(stream.closed || file==input.file){file->flags|=F_ERR;errno=EBADF;return 0;}
    size_t buffered=file->wbase?file->wpos-file->wbase:0;
    size_t flushed=emit(file->wbase,buffered);
    file->wpos=file->wbase=file->buf;file->wend=file->buf+file->buf_size;
    if(flushed!=buffered){file->flags|=F_ERR;return 0;}
    size_t sent=emit(data,bytes);if(sent!=bytes)file->flags|=F_ERR;
    return sent;
}
size_t readConsole(FILE *file,unsigned char*,size_t){
    if(console(file).closed || file!=input.file){errno=EBADF;file->flags|=F_ERR;}
    else file->flags|=F_EOF; // The script profile's standard input is EOF.
    return 0;
}
struct Initialize {
    Initialize(){
        Console *streams[]={&input,&output,&errorOutput};
        for(auto *stream:streams){
            stream->file->flags=F_PERM | (stream==&input?F_NOWR:F_NORD);
            stream->file->buf=stream->buffer;stream->file->buf_size=0;
            stream->file->read=readConsole;stream->file->write=writeConsole;
            stream->file->lbf=-1;stream->file->fd=-1;stream->file->lock=0;
        }
    }
} initialize;
}

#undef feof
#undef ferror
#undef stdin
#undef stdout
#undef stderr
extern "C" {
FILE *const stdin=&__stdin_FILE;
FILE *const stdout=&__stdout_FILE;
FILE *const stderr=&__stderr_FILE;
int *___errno_location(){return __errno_location();}
int __lockfile(FILE *file){if(sacx_recursive_lock(&console(file).mutex))invalidStream();return 1;}
void __unlockfile(FILE *file){if(sacx_recursive_unlock(&console(file).mutex))invalidStream();}
void flockfile(FILE *file){(void)__lockfile(file);}
void funlockfile(FILE *file){__unlockfile(file);}
int ftrylockfile(FILE *file){return sacx_recursive_trylock(&console(file).mutex)?-1:0;}
FILE *fopen(const char*,const char*){errno=EACCES;return nullptr;}
int fflush(FILE *file){
    if(!file){int a=output.closed?0:fflush(stdout),b=errorOutput.closed?0:fflush(stderr);return a || b?EOF:0;}
    __lockfile(file);int result=0;
    if(console(file).closed){errno=EBADF;result=EOF;}
    else if(file!=stdin){writeConsole(file,nullptr,0);if(file->flags&F_ERR)result=EOF;}
    __unlockfile(file);return result;
}
int fclose(FILE *file){
    __lockfile(file);auto& stream=console(file);
    if(stream.closed){errno=EBADF;__unlockfile(file);return EOF;}
    int status=fflush(file);stream.closed=true;file->flags|=F_NOWR|F_NORD;
    __unlockfile(file);return status;
}
int fseek(FILE *file,long,int){__lockfile(file);errno=console(file).closed?EBADF:ESPIPE;__unlockfile(file);return -1;}
long ftell(FILE *file){__lockfile(file);errno=console(file).closed?EBADF:ESPIPE;__unlockfile(file);return -1;}
int setvbuf(FILE *file,char*,int mode,size_t){
    __lockfile(file);int status=0;
    if(console(file).closed){errno=EBADF;status=-1;}
    else if(mode!=_IONBF){errno=EINVAL;status=-1;}
    // Already unbuffered; full/line buffering requests fail explicitly.
    __unlockfile(file);return status;
}
void setbuf(FILE *file,char *buffer){(void)setvbuf(file,buffer,buffer?_IOFBF:_IONBF,BUFSIZ);}
int feof(FILE *file){__lockfile(file);int result=!!(file->flags&F_EOF);__unlockfile(file);return result;}
int ferror(FILE *file){__lockfile(file);int result=!!(file->flags&F_ERR);__unlockfile(file);return result;}
void clearerr(FILE *file){__lockfile(file);file->flags&=~(F_EOF|F_ERR);__unlockfile(file);}
char *strerror(int error){
    static const char prefix[]="DihOS error ";memcpy(errorText,prefix,sizeof(prefix)-1);
    unsigned n=static_cast<unsigned>(error);size_t at=sizeof(prefix)-1;
    if(error<0){errorText[at++]='-';n=0u-n;}
    char reverse[10];unsigned count=0;do{reverse[count++]=char('0'+n%10);n/=10;}while(n);
    while(count)errorText[at++]=reverse[--count];errorText[at]=0;return errorText;
}
int wctomb(char *out,wchar_t value){
    if(!out)return 0;
    if(static_cast<unsigned>(value)>127){errno=EILSEQ;return -1;}
    *out=static_cast<char>(value);return 1;
}
size_t wcrtomb(char *out,wchar_t value,mbstate_t *state){
    if(state)memset(state,0,sizeof(*state));
    if(!out)return 1;
    int n=wctomb(out,value);return n<0?size_t(-1):size_t(n);
}
size_t mbrtowc(wchar_t *out,const char *text,size_t bytes,mbstate_t *state){
    if(!text){if(state)memset(state,0,sizeof(*state));return 0;}
    if(!bytes)return size_t(-2);
    unsigned char value=*text;
    if(value>127){errno=EILSEQ;return size_t(-1);}
    if(out)*out=value;if(state)memset(state,0,sizeof(*state));return value?1:0;
}
int mbsinit(const mbstate_t *state){
    if(!state)return 1;
    auto bytes=reinterpret_cast<const unsigned char*>(state);
    for(size_t i=0;i<sizeof(*state);++i)if(bytes[i])return 0;
    return 1;
}
int iswspace(wint_t value){return value==' ' || (value>='\t' && value<='\r');}
}
