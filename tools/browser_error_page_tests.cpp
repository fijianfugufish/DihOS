#include "../tools/webcore_dihos/error_page.h"
#include <cassert>
#include <cstdio>
int main(){
    using namespace DihosNavigation;
    auto html=errorPage("https://example.com/?q=\"<script>&'","TLS <failed> & closed",-13);
    assert(html.find("<title>Unable to load page</title>")!=std::string::npos);
    assert(html.find("TLS &lt;failed&gt; &amp; closed")!=std::string::npos);
    assert(html.find("<script>")==std::string::npos);
    assert(html.find("href=\"https://example.com/?q=&quot;&lt;script&gt;&amp;&#39;\"")!=std::string::npos);
    assert(html.find("Network error -13")!=std::string::npos);
    assert(errorPage("javascript:alert(1)","",-22).find("href=\"about:blank\"")!=std::string::npos);
    assert(errorPage("https://example.com/",std::string(10000,'x'),-5).size()<3000);
    puts("PASS: local error page, escaped URL/reason, safe retry scheme and bounded text");
}
