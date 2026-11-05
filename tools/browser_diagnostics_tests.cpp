#include "webcore_dihos/browser_diagnostics.h"
#include <cassert>
#include <cstring>
#include <string>
int main(){
    using namespace DihosNavigation;BrowserDiagnostics state;
    assert(!state.errors && !state.failures);
    state.console(false,"a warning");assert(!strcmp(state.summary(),"a warning"));
    state.failed("https://example.test/app.js",-75,"response too large");
    assert(strstr(state.summary(),"-75") && strstr(state.summary(),"app.js"));
    state.console(true,"ReferenceError\nmissing\tAPI");
    assert(!strcmp(state.summary(),"ReferenceError missing API"));
    state.console(false,"later warning");assert(strstr(state.summary(),"ReferenceError"));
    state.console(true,std::string(4096,'x'));assert(strlen(state.summary())==511);
    state.errors=0xffffffffu;state.console(true,"latest");assert(state.errors==0xffffffffu);
    state={};assert(!state.errors && !state.failures && !state.error[0] && !state.request[0]);
}
