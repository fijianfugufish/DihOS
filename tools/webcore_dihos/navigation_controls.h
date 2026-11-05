#pragma once
#include "../../sdk/sacx/apps/dihscover/address.h"
#include "../../sdk/sacx/include/sacx_window_keys.h"
#include <string>
#include <algorithm>

namespace DihosNavigation {
// Portable control state: no kernel widgets, callbacks, or engine pointers.
class AddressControl {
public:
    static constexpr size_t capacity=767;
    std::string text;
    size_t caret=0,anchor=0;
    bool focused=false;
    void set(const char* value){text=value?value:"";if(text.size()>capacity)text.resize(capacity);caret=anchor=text.size();}
    void focus(){focused=true;anchor=0;caret=text.size();}
    void blur(){focused=false;anchor=caret;}
    void eraseSelection(){auto a=std::min(anchor,caret),b=std::max(anchor,caret);text.erase(a,b-a);anchor=caret=a;}
    // True means Go. Selection and navigation remain available at capacity.
    bool key(unsigned usage,unsigned scalar,unsigned modifiers){
        bool shift=modifiers&SACX_WINDOW_SHIFT,ctrl=modifiers&SACX_WINDOW_CTRL;
        if(ctrl && usage==SACX_KEY_A){anchor=0;caret=text.size();return false;}
        if(usage==SACX_KEY_ENTER || usage==SACX_KEY_KP_ENTER)return true;
        if(usage==SACX_KEY_ESCAPE){blur();return false;}
        if(usage==SACX_KEY_LEFT || usage==SACX_KEY_RIGHT || usage==SACX_KEY_HOME || usage==SACX_KEY_END){
            if(!shift && anchor!=caret && (usage==SACX_KEY_LEFT || usage==SACX_KEY_RIGHT))
                caret=usage==SACX_KEY_LEFT?std::min(anchor,caret):std::max(anchor,caret);
            else if(usage==SACX_KEY_HOME)caret=0;
            else if(usage==SACX_KEY_END)caret=text.size();
            else if(usage==SACX_KEY_LEFT){if(caret)--caret;}
            else if(caret<text.size())++caret;
            if(!shift)anchor=caret;return false;
        }
        if(usage==SACX_KEY_BACKSPACE || usage==SACX_KEY_DELETE){
            if(caret!=anchor)eraseSelection();
            else if(usage==SACX_KEY_BACKSPACE && caret){text.erase(--caret,1);anchor=caret;}
            else if(usage==SACX_KEY_DELETE && caret<text.size())text.erase(caret,1);
        }else if(scalar>=32 && scalar<127 && !(modifiers&(SACX_WINDOW_CTRL|SACX_WINDOW_ALT|SACX_WINDOW_META))){
            if(text.size()-(std::max(anchor,caret)-std::min(anchor,caret))<capacity){
                eraseSelection();text.insert(caret,1,(char)scalar);anchor=++caret;
            }
        }
        return false;
    }
};
}
