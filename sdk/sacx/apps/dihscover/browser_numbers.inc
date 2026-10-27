/* Decimal conversion used by the freestanding port. Exponent magnitude must
 * not determine loop length: this file executes without compiler checkpoints. */
static double browser_decimal(const char *input,char **end)
{
    const char *s=input;while(*s==' '||*s=='\t'||*s=='\n'||*s=='\r'||*s=='\f'||*s=='\v')++s;
    bool negative=false;if(*s=='-'||*s=='+')negative=*s++=='-';
    double value=0;unsigned significant=0;int power=0;bool digits=false;
    while(*s>='0'&&*s<='9'){
        unsigned digit=(unsigned)(*s++-'0');digits=true;
        if(!significant&&!digit)continue;
        if(significant<18){value=value*10+digit;++significant;}
        else if(power<100000)++power;
    }
    if(*s=='.'){
        ++s;while(*s>='0'&&*s<='9'){
            unsigned digit=(unsigned)(*s++-'0');digits=true;
            if(significant<18){value=value*10+digit;if(power>-100000)--power;if(significant||digit)++significant;}
        }
    }
    if(!digits){if(end)*end=(char*)input;return 0;}
    if(*s=='e'||*s=='E'){
        const char *e=s+1;bool inverse=false;if(*e=='+'||*e=='-')inverse=*e++=='-';
        if(*e>='0'&&*e<='9'){
            int exponent=0;
            do {if(exponent<100000){exponent=exponent*10+(*e-'0');if(exponent>100000)exponent=100000;}++e;}while(*e>='0'&&*e<='9');
            power+=inverse?-exponent:exponent;s=e;
        }
    }
    if(end)*end=(char*)s;
    if(value){
        if(power>308)value=__builtin_huge_val();
        else if(power< -400)value=0;
        else {
            bool inverse=power<0;unsigned exponent=(unsigned)(inverse?-power:power);
            double factor=inverse?.1:10;
            while(exponent){if(exponent&1)value*=factor;exponent>>=1;if(exponent)factor*=factor;}
        }
    }
    return negative?-value:value;
}
static double browser_integer_pow(double base,double exponent)
{
    if(exponent!=exponent||exponent>=9223372036854775808.0||exponent< -9223372036854775808.0)return __builtin_nan("");
    long long whole=(long long)exponent;
    if(exponent!=(double)whole)return 0; /* existing port's fractional-power limitation */
    bool inverse=whole<0;
    uint64_t magnitude=inverse?(uint64_t)(-(whole+1))+1u:(uint64_t)whole;
    double result=1;
    while(magnitude){if(magnitude&1u)result*=base;base*=base;magnitude>>=1;}
    return inverse?1/result:result;
}
