#pragma once
#include "sacx_keys.h"
enum { SACX_WINDOW_SHIFT=1, SACX_WINDOW_CTRL=2, SACX_WINDOW_ALT=4, SACX_WINDOW_META=8, SACX_WINDOW_REPEAT=16 };
/* Initial US-layout translation, shared by both sides of the checked event ABI.
   HID usage remains available for shortcuts and non-text editing keys. */
static inline unsigned sacx_window_key_text(unsigned usage,unsigned modifiers)
{
    unsigned shift=modifiers&SACX_WINDOW_SHIFT;
    if(modifiers&(SACX_WINDOW_CTRL|SACX_WINDOW_ALT|SACX_WINDOW_META))return 0;
    if(usage>=SACX_KEY_A && usage<=SACX_KEY_Z)return (shift?'A':'a')+usage-SACX_KEY_A;
    if(usage>=SACX_KEY_1 && usage<=SACX_KEY_0){
        const char *s=shift?"!@#$%^&*()":"1234567890";return (unsigned char)s[usage-SACX_KEY_1];
    }
    switch(usage){
    case SACX_KEY_SPACE:return ' ';case SACX_KEY_MINUS:return shift?'_':'-';case SACX_KEY_EQUAL:return shift?'+':'=';
    case SACX_KEY_LEFTBRACE:return shift?'{':'[';case SACX_KEY_RIGHTBRACE:return shift?'}':']';case SACX_KEY_BACKSLASH:return shift?'|':'\\';
    case SACX_KEY_SEMICOLON:return shift?':':';';case SACX_KEY_APOSTROPHE:return shift?'"':'\'';case SACX_KEY_GRAVE:return shift?'~':'`';
    case SACX_KEY_COMMA:return shift?'<':',';case SACX_KEY_DOT:return shift?'>':'.';case SACX_KEY_SLASH:return shift?'?':'/';
    default:return 0;
    }
}
