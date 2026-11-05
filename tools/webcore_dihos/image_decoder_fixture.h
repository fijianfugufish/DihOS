// Test-only; compiled against the shipping WebCore decoder and Cairo objects.
#include "platform/DihOSImageDecoder.h"
#include "DecodingOptions.h"
#include "../browser_fixtures/images/fixtures.inc"
static void checkImageDecoder(const sacx_api* api){
 using namespace WebCore;using namespace WTF;
 auto buffer=SharedBuffer::create(std::span<const uint8_t>{image_alpha,sizeof(image_alpha)});
 auto decoder=createDihOSImageDecoder(buffer.get(),"image/png"_s,static_cast<AlphaOption>(0),static_cast<GammaAndColorProfileOption>(0));
 check(!!decoder,"Images: factory");
 decoder->setData(buffer.get(),true);
 check(decoder->size()==IntSize(1,1) && decoder->frameCount()==1,"Images: metadata");
 auto image=decoder->createFrameImageAtIndex(0,static_cast<SubsamplingLevel>(0),DecodingOptions{});
 check(!!image,"Images: alpha decode");cairo_surface_flush(image.get());
 check(*reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(image.get()))==0x80643219,"Images: premultiplied ARGB");
 decoder->clearFrameBufferCache(0);decoder=nullptr;
 check(*reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(image.get()))==0x80643219,"Images: surface owns pixels beyond decoder lifetime");
 for(unsigned n: {0u,1u,7u,20u}){
  auto bad=SharedBuffer::create(std::span{image_png}.first(n));
  auto d=createDihOSImageDecoder(bad.get(),"image/png"_s,static_cast<AlphaOption>(0),static_cast<GammaAndColorProfileOption>(0));
  check(!!d,"Images: truncated factory");d->setData(bad.get(),true);
  check(!d->createFrameImageAtIndex(0,static_cast<SubsamplingLevel>(0),DecodingOptions{}),"Images: truncated data rejected");
 }
 std::vector<unsigned char> chunk(65536);
 auto segment=SharedBuffer::create(std::span<const uint8_t>{chunk.data(),chunk.size()});
 SharedBufferBuilder builder;for(unsigned i=0;i<129;++i)builder.append(segment.get());
 auto bad=builder.takeBuffer();
 check(!createDihOSImageDecoder(bad.get(),"image/png"_s,static_cast<AlphaOption>(0),static_cast<GammaAndColorProfileOption>(0)),"Images: encoded limit");
 api->log("Images: premultiplication, retained surface ownership, truncation and size limits OK");
}
