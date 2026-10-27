#include "preferences.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
browser_preferences browser_prefs={0,1,2,8,100};
static const char *path="0:/Documents/Dihscover.ini";
void browser_preferences_load(const sacx_api *api) {
    uint32_t handle=0,got=0;char data[513];
    if(api->file_open(path,SACX_FILE_READ,&handle))return;
    int rc=api->file_read(handle,data,512,&got);api->file_close(handle);if(rc||got>512)return;data[got]=0;
    for(char *line=data;line&&*line;) {
        char *end=strchr(line,'\n');if(end)*end++=0;char *eq=strchr(line,'=');
        if(eq){*eq++=0;int v=atoi(eq);
            if(v>=0&&v<3){if(!strcmp(line,"sans"))browser_prefs.sans=v;else if(!strcmp(line,"serif"))browser_prefs.serif=v;else if(!strcmp(line,"mono"))browser_prefs.mono=v;}
            if(!strcmp(line,"minimum")&&v>=6&&v<=32)browser_prefs.minimum=v;
            if(!strcmp(line,"zoom")&&v>=50&&v<=300)browser_prefs.zoom=v;
        }line=end;
    }
}
int browser_preferences_save(const sacx_api *api) {
    char data[160];int length=snprintf(data,sizeof data,"sans=%u\nserif=%u\nmono=%u\nminimum=%u\nzoom=%u\n",browser_prefs.sans,browser_prefs.serif,browser_prefs.mono,browser_prefs.minimum,browser_prefs.zoom);
    (void)api->file_mkdir("0:/Documents");
    uint32_t handle=0,written=0;if(length<0||api->file_open(path,SACX_FILE_WRITE|SACX_FILE_CREATE|SACX_FILE_TRUNC,&handle))return -1;
    int rc=api->file_write(handle,data,(uint32_t)length,&written);api->file_close(handle);return rc||written!=(uint32_t)length?-1:0;
}
