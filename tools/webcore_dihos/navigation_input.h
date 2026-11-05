#pragma once
#include "PlatformKeyboardEvent.h"
#include "Editor.h"
#include "LocalFrameInlines.h"
#include "EventHandler.h"
#include "../../sdk/sacx/include/sacx_user_window.h"
#include "../../sdk/sacx/include/sacx_window_keys.h"
#include <string>
namespace DihosNavigation {
inline void documentKey(WebCore::LocalFrame& frame,const sacx_user_window_event& event){
using namespace WebCore;using namespace WTF;
bool down=event.type==SACX_WINDOW_KEY_DOWN;unsigned key=event.x,mods=event.delta;
OptionSet<PlatformEvent::Modifier> modifiers;
if(mods&SACX_WINDOW_SHIFT)modifiers.add(PlatformEvent::Modifier::ShiftKey);
if(mods&SACX_WINDOW_CTRL)modifiers.add(PlatformEvent::Modifier::ControlKey);
if(mods&SACX_WINDOW_ALT)modifiers.add(PlatformEvent::Modifier::AltKey);
if(mods&SACX_WINDOW_META)modifiers.add(PlatformEvent::Modifier::MetaKey);
unsigned scalar=event.y;const char* name="Unidentified";int virtualKey=0;
switch(key){
case SACX_KEY_ENTER:case SACX_KEY_KP_ENTER:name="Enter";virtualKey=13;scalar='\r';break;
case SACX_KEY_BACKSPACE:name="Backspace";virtualKey=8;break;case SACX_KEY_DELETE:name="Delete";virtualKey=46;break;
case SACX_KEY_TAB:name="Tab";virtualKey=9;scalar='\t';break;
case SACX_KEY_LEFT:name="ArrowLeft";virtualKey=37;break;case SACX_KEY_RIGHT:name="ArrowRight";virtualKey=39;break;
case SACX_KEY_UP:name="ArrowUp";virtualKey=38;break;case SACX_KEY_DOWN:name="ArrowDown";virtualKey=40;break;
case SACX_KEY_HOME:name="Home";virtualKey=36;break;case SACX_KEY_END:name="End";virtualKey=35;break;
case SACX_KEY_PAGEUP:name="PageUp";virtualKey=33;break;case SACX_KEY_PAGEDOWN:name="PageDown";virtualKey=34;break;
}
if(key>=SACX_KEY_A && key<=SACX_KEY_Z)virtualKey='A'+key-SACX_KEY_A;
String text=scalar && scalar<128 && down?String::fromUTF8(std::string(1,(char)scalar).c_str()):String();
unsigned keyScalar=sacx_window_key_text(key,mods&SACX_WINDOW_SHIFT);
if(scalar>=32 && scalar<127)keyScalar=scalar;
String value=keyScalar?String::fromUTF8(std::string(1,(char)keyScalar).c_str()):String::fromUTF8(name);
std::string code=name;
if(key>=SACX_KEY_A && key<=SACX_KEY_Z)code=std::string("Key")+(char)('A'+key-SACX_KEY_A);
else if(key>=SACX_KEY_1 && key<=SACX_KEY_0)code=std::string("Digit")+(key==SACX_KEY_0?'0':(char)('1'+key-SACX_KEY_1));
PlatformKeyboardEvent keyboard(down?PlatformEvent::Type::KeyDown:PlatformEvent::Type::KeyUp,text,text,value,
    String::fromUTF8(code.c_str()),String::fromUTF8(name),virtualKey,mods&SACX_WINDOW_REPEAT,key==SACX_KEY_KP_ENTER,false,
    modifiers,MonotonicTime::fromRawSeconds(event.time_ms/1000.0));
bool handled=frame.eventHandler().keyEvent(keyboard);
// Let DOM keydown/keypress and form default handlers run first.
// A prevented event must never trigger a second editing action.
if(down && !handled && frame.editor().canEdit()){
    const char* command=nullptr;
    if(mods&SACX_WINDOW_CTRL){if(key==SACX_KEY_A)command="SelectAll";}
    else if(!(mods&(SACX_WINDOW_ALT|SACX_WINDOW_META))){
        switch(key){
        case SACX_KEY_BACKSPACE:command="DeleteBackward";break;case SACX_KEY_DELETE:command="DeleteForward";break;
        case SACX_KEY_ENTER:case SACX_KEY_KP_ENTER:command="InsertNewline";break;
        case SACX_KEY_LEFT:command=mods&SACX_WINDOW_SHIFT?"MoveLeftAndModifySelection":"MoveLeft";break;
        case SACX_KEY_RIGHT:command=mods&SACX_WINDOW_SHIFT?"MoveRightAndModifySelection":"MoveRight";break;
        case SACX_KEY_HOME:command=mods&SACX_WINDOW_SHIFT?"MoveToBeginningOfLineAndModifySelection":"MoveToBeginningOfLine";break;
        case SACX_KEY_END:command=mods&SACX_WINDOW_SHIFT?"MoveToEndOfLineAndModifySelection":"MoveToEndOfLine";break;
        }
    }
    if(command)frame.editor().command(String::fromUTF8(command)).execute();
    else if(!(mods&(SACX_WINDOW_CTRL|SACX_WINDOW_ALT|SACX_WINDOW_META)) && scalar>=32 && scalar<127)
        frame.editor().insertText(text,nullptr);
}
}
}
