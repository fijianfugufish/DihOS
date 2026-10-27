#include <cassert>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cstdio>
#include "../sdk/sacx/apps/dihscover/browser_numbers.inc"
int main(){
    const char *samples[]={"16","  -1.6px",".25e+2rest","00001.5","1e-100","1e308","-1e-324","1e+","+."};
    for(auto s:samples){char *actual,*expected;double a=browser_decimal(s,&actual),b=strtod(s,&expected);assert(actual-s==expected-s);assert(a==b||fabs((a-b)/b)<1e-12);}
    char *end;const char *huge="1e9999999999999999999999999999999999999999999999";
    assert(std::isinf(browser_decimal(huge,&end))&&!*end);
    assert(browser_decimal("0e9999999999999999999999999999",&end)==0&&!*end);
    assert(browser_decimal("1e-9999999999999999999999999999",&end)==0&&!*end);
    std::string compensated="1"+std::string(400,'0')+"e-400";
    assert(fabs(browser_decimal(compensated.c_str(),&end)-1)<1e-12&&!*end);
    assert(browser_decimal("1e-312",&end)>0);
    assert(browser_integer_pow(1,-9223372036854775808.0)==1);
    assert(browser_integer_pow(2,-8)==1.0/256);
    assert(std::isnan(browser_integer_pow(2,1e100)));
    puts("PASS: bounded large decimal exponents, signed conversion/end pointers, compensated long mantissas, subnormals and terminating minimum-integer powers");
}
