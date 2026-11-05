#include <cassert>
#include <cstring>
#include <iostream>
#include "webcore_dihos/navigation_controls.h"
using DihosNavigation::AddressControl;
static void normalized(const char* input,const char* expected){char out[768];assert(!dihscover_address_normalize(input,out,sizeof(out)));assert(!strcmp(out,expected));}
int main(){
 normalized(" example.com ","https://example.com");normalized("https://example.com/a?b=c","https://example.com/a?b=c");
 normalized("how to peel an apple","https://www.google.com/search?q=how+to+peel+an+apple");
 normalized("dihos","https://www.google.com/search?q=dihos");
 normalized("https://html.duckduckgo.com/html/?q=dihos","https://html.duckduckgo.com/html/?q=dihos");
 normalized("a&b + c","https://www.google.com/search?q=a%26b+%2B+c");
 normalized("\xc3\xa9","https://www.google.com/search?q=%C3%A9");
 char out[32];assert(dihscover_address_normalize("",out,sizeof(out)));assert(dihscover_address_normalize("https://x/\r\nheader",out,sizeof(out)));
 assert(dihscover_address_normalize("this query needs much more output space",out,sizeof(out)));
 AddressControl a;a.set("https://example.com/");a.focus();a.key(SACX_KEY_W,'w',0);assert(a.text=="w" && a.caret==1);
 a.key(SACX_KEY_W,'w',0);a.key(SACX_KEY_LEFT,0,SACX_WINDOW_SHIFT);a.key(SACX_KEY_BACKSPACE,0,0);assert(a.text=="w");
 a.key(SACX_KEY_HOME,0,0);a.key(SACX_KEY_DELETE,0,0);assert(a.text.empty());
 a.set(std::string(AddressControl::capacity,'a').c_str());a.key(SACX_KEY_B,'b',0);assert(a.text.size()==AddressControl::capacity);
 a.focus();a.key(SACX_KEY_B,'b',0);assert(a.text=="b");assert(a.key(SACX_KEY_ENTER,0,0));
 a.key(SACX_KEY_ESCAPE,0,0);assert(!a.focused);
 assert(sacx_window_key_text(SACX_KEY_A,0)=='a');assert(sacx_window_key_text(SACX_KEY_A,SACX_WINDOW_SHIFT)=='A');
 assert(!sacx_window_key_text(SACX_KEY_A,SACX_WINDOW_CTRL));assert(sacx_window_key_text(SACX_KEY_SLASH,SACX_WINDOW_SHIFT)=='?');
 std::cout<<"PASS: shared URLs/search, encoding, capacity, selection, editing and key translation\n";
}
