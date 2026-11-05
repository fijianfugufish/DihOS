#include "fcint.h"
#include "memory.h"
FcConfig* dihos_fc_config_create_memory(void){
    FcConfig* config=FcConfigCreate();
    if(!config)return NULL;
    FcFontSet* system=FcFontSetCreate();
    FcFontSet* application=FcFontSetCreate();
    if(!system || !application){
        if(system)FcFontSetDestroy(system);
        if(application)FcFontSetDestroy(application);
        FcConfigDestroy(config);return NULL;
    }
    FcConfigSetFonts(config,system,FcSetSystem);
    FcConfigSetFonts(config,application,FcSetApplication);
    if(!FcConfigSetRescanInterval(config,0)){FcConfigDestroy(config);return NULL;}
    return config;
}
FcBool dihos_fc_config_add_pattern(FcConfig* config,FcPattern* pattern){
    if(!config || !pattern)return FcFalse;
    FcFontSet* set=FcConfigGetFonts(config,FcSetApplication);
    if(!set)return FcFalse;
    FcPatternReference(pattern);
    if(!FcFontSetAdd(set,pattern)){FcPatternDestroy(pattern);return FcFalse;}
    return FcTrue;
}
