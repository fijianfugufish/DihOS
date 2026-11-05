#include "config.h"
#include "DihOSImageDecoder.h"
#include "SharedBuffer.h"
#include <wtf/TZoneMallocInlines.h>
#include <atomic>
#include <mutex>
#include <cairo.h>
#include <cstdlib>
#include <cstring>
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_MAX_DIMENSIONS 8192
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"
#define SIMPLEWEBP_DISABLE_STDIO
#define SIMPLEWEBP_IMPLEMENTATION
#include "simplewebp.h"
namespace WebCore {
using namespace WTF;
static constexpr size_t maxEncoded=8u*1024u*1024u,maxPixels=16u*1024u*1024u,maxDecoded=64u*1024u*1024u;
static std::atomic<size_t> decodedBytes;
static cairo_user_data_key_t pixelKey;
struct OwnedPixels {unsigned char* data;size_t bytes;};
static void releasePixels(void* raw){auto* p=static_cast<OwnedPixels*>(raw);free(p->data);decodedBytes.fetch_sub(p->bytes);free(p);}
static bool reserve(size_t bytes){size_t n=decodedBytes.load();do{if(bytes>maxDecoded-n)return false;}while(!decodedBytes.compare_exchange_weak(n,n+bytes));return true;}
class DihOSBitmapDecoder final:public ImageDecoder {
    WTF_MAKE_TZONE_ALLOCATED(DihOSBitmapDecoder);
public:
    DihOSBitmapDecoder(const String& mime):m_extension(mime=="image/jpeg"_s?"jpg"_s:mime=="image/gif"_s?"gif"_s:mime=="image/webp"_s?"webp"_s:mime=="image/bmp"_s?"bmp"_s:"png"_s){}
    size_t bytesDecodedToDetermineProperties() const final {std::lock_guard lock(m_mutex);return m_data?m_data->size():0;}
    EncodedDataStatus encodedDataStatus() const final {std::lock_guard lock(m_mutex);return m_status;}
    IntSize size() const final {std::lock_guard lock(m_mutex);return m_size;}
    size_t frameCount() const final {std::lock_guard lock(m_mutex);return m_size.isEmpty()?0:1;}
    RepetitionCount repetitionCount() const final {return RepetitionCountNone;}
    String filenameExtension() const final {return m_extension;}
    std::optional<IntPoint> hotSpot() const final {return std::nullopt;}
    IntSize frameSizeAtIndex(size_t index,SubsamplingLevel) const final {return index?IntSize{}:size();}
    bool frameIsCompleteAtIndex(size_t index) const final {std::lock_guard lock(m_mutex);return !index && m_all && m_status!=EncodedDataStatus::Error;}
    Seconds frameDurationAtIndex(size_t) const final {return 0_s;}
    bool frameHasAlphaAtIndex(size_t) const final {std::lock_guard lock(m_mutex);return m_alpha;}
    bool isAllDataReceived() const final {std::lock_guard lock(m_mutex);return m_all;}
    void clearFrameBufferCache(size_t) final {std::lock_guard lock(m_mutex);m_image=nullptr;}
    void setData(const FragmentedSharedBuffer& data,bool all) final {
        std::lock_guard lock(m_mutex);m_all=all;
        if(data.size()>maxEncoded){m_status=EncodedDataStatus::Error;return;}
        m_data=data.makeContiguous();auto bytes=m_data->span();
        m_webp=bytes.size()>=12 && !memcmp(bytes.data(),"RIFF",4) && !memcmp(bytes.data()+8,"WEBP",4);
        int w=0,h=0,n=4;bool known=false;
        if(m_webp && all){simplewebp* decoder=nullptr;
            if(simplewebp_load_from_memory(const_cast<uint8_t*>(bytes.data()),bytes.size(),nullptr,&decoder)==SIMPLEWEBP_NO_ERROR && decoder){size_t width,height;simplewebp_get_dimensions(decoder,&width,&height);if(width<=8192 && height<=8192){w=width;h=height;known=true;}simplewebp_unload(decoder);}
        }else if(!m_webp && !bytes.empty())known=stbi_info_from_memory(bytes.data(),bytes.size(),&w,&h,&n);
        if(known && w>0 && h>0 && w<=8192 && h<=8192 && (size_t)w*h<=maxPixels){m_size={w,h};m_alpha=n==2 || n==4;m_status=all?EncodedDataStatus::Complete:EncodedDataStatus::SizeAvailable;}
        else m_status=all?EncodedDataStatus::Error:EncodedDataStatus::TypeAvailable;
    }
    PlatformImagePtr createFrameImageAtIndex(size_t index,SubsamplingLevel,const DecodingOptions&) final {
        std::lock_guard lock(m_mutex);
        if(index || !m_all || m_size.isEmpty() || m_status==EncodedDataStatus::Error || !m_data)return nullptr;
        if(m_image)return m_image;
        size_t count=(size_t)m_size.width()*m_size.height(),size=count*4;
        if(!reserve(size))return nullptr;
        auto bytes=m_data->span();unsigned char* pixels=nullptr;int w=m_size.width(),h=m_size.height(),n=0;
        if(m_webp){simplewebp* decoder=nullptr;
            if(simplewebp_load_from_memory(const_cast<uint8_t*>(bytes.data()),bytes.size(),nullptr,&decoder)==SIMPLEWEBP_NO_ERROR && decoder){pixels=(unsigned char*)malloc(size);if(pixels && simplewebp_decode(decoder,pixels,nullptr)!=SIMPLEWEBP_NO_ERROR){free(pixels);pixels=nullptr;}simplewebp_unload(decoder);}
        }else pixels=stbi_load_from_memory(bytes.data(),bytes.size(),&w,&h,&n,4);
        if(!pixels || w!=m_size.width() || h!=m_size.height()){free(pixels);decodedBytes.fetch_sub(size);m_status=EncodedDataStatus::Error;return nullptr;}
        // Cairo owns premultiplied ARGB32 pixels. Transfer ownership with the
        // surface; NativeImage can outlive both this decoder and encoded data.
        auto* argb=reinterpret_cast<uint32_t*>(pixels);
        for(size_t i=0;i<count;++i){unsigned r=pixels[i*4],g=pixels[i*4+1],b=pixels[i*4+2],a=pixels[i*4+3];argb[i]=(a<<24)|(((r*a+127)/255)<<16)|(((g*a+127)/255)<<8)|((b*a+127)/255);}
        auto* owner=(OwnedPixels*)malloc(sizeof(OwnedPixels));
        if(!owner){free(pixels);decodedBytes.fetch_sub(size);return nullptr;}*owner={pixels,size};
        auto* image=cairo_image_surface_create_for_data(pixels,CAIRO_FORMAT_ARGB32,w,h,w*4);
        if(cairo_surface_status(image)!=CAIRO_STATUS_SUCCESS || cairo_surface_set_user_data(image,&pixelKey,owner,releasePixels)!=CAIRO_STATUS_SUCCESS){cairo_surface_destroy(image);releasePixels(owner);return nullptr;}
        m_image=adoptRef(image);return m_image;
    }
private:
    mutable std::mutex m_mutex;
    RefPtr<SharedBuffer> m_data;
    PlatformImagePtr m_image;
    IntSize m_size;
    String m_extension;
    EncodedDataStatus m_status{EncodedDataStatus::Unknown};
    bool m_all{false},m_alpha{true},m_webp{false};
};
WTF_MAKE_TZONE_ALLOCATED_IMPL(DihOSBitmapDecoder);
RefPtr<ImageDecoder> createDihOSImageDecoder(FragmentedSharedBuffer& data,const String& mime,AlphaOption,GammaAndColorProfileOption){
    if(data.size()>maxEncoded)return nullptr;
    auto decoder=adoptRef(*new DihOSBitmapDecoder(mime));decoder->setData(data,false);return decoder;
}
}
