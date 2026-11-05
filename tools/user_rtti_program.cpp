#include "sacx_user_api.h"
#include <typeinfo>
struct Root {virtual ~Root()=default;int root=1;};
struct Left:virtual Root {int left=2;};
struct Right:virtual Root {int right=3;};
struct Diamond:Left,Right {int value=4;};
struct Other:Root {};
struct Hidden:private Root {Root *view(){return this;}};
__attribute__((noinline)) static int check(Root *root,Left *left,void *complete){
    if(!dynamic_cast<Diamond*>(root) || !dynamic_cast<Right*>(left))return 1;
    if(dynamic_cast<Other*>(root) || dynamic_cast<void*>(root)!=complete)return 2;
    if(typeid(*root)!=typeid(Diamond) || typeid(*root)==typeid(Other))return 3;
    return 0;
}
extern "C" int dihos_user_main(const sacx_api *api){
    Diamond object;Root *null=nullptr;Hidden hidden;
    if(check(&object,&object,&object) || dynamic_cast<Diamond*>(null) || dynamic_cast<Hidden*>(hidden.view()))
        sacx_user_exit(164,"SACX RTTI cast/type identity failure");
    api->log("SACX RTTI: virtual diamond, cross-cast, rejection and type identity OK");
    return 88;
}
