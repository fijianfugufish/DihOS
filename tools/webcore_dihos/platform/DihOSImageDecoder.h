#pragma once
#include "ImageDecoder.h"
namespace WebCore {
RefPtr<ImageDecoder> createDihOSImageDecoder(FragmentedSharedBuffer&,const String&,AlphaOption,GammaAndColorProfileOption);
}
