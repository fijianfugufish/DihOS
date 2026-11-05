#pragma once
#ifndef SACX_USER_ABI_VERSION
#include "sacx_user_syscall.h"
#endif
#define SACX_USER_CAP_SURFACE (1ull<<4)
#define SACX_USER_WINDOW_MAX_WIDTH 1920u
#define SACX_USER_WINDOW_MAX_HEIGHT 1080u
#define SACX_USER_WINDOW_WRITE_MAX 16384u
#define SACX_USER_WINDOW_EVENTS 64u
enum { SACX_USER_WINDOW_CREATE=33,SACX_USER_WINDOW_WRITE=34,SACX_USER_WINDOW_PRESENT=35,SACX_USER_WINDOW_EVENT=36,SACX_USER_WINDOW_CLOSE=37 };
// Append-only ABI: query operation bit 41 before using region transactions.
enum { SACX_USER_WINDOW_REGION=41 };
typedef struct sacx_user_window_region {uint32_t x,y,width,height;} sacx_user_window_region;
enum { SACX_WINDOW_MOVE=1,SACX_WINDOW_DOWN,SACX_WINDOW_UP,SACX_WINDOW_WHEEL,SACX_WINDOW_CLOSE,SACX_WINDOW_RESET,SACX_WINDOW_KEY_DOWN,SACX_WINDOW_KEY_UP };
// Key events: x = USB HID usage, y = Unicode scalar (zero for non-text),
// delta = SACX_WINDOW_* modifiers from sacx_window_keys.h; buttons/reserved zero.
// RESET also releases all held keys after focus loss or queue overflow.
typedef struct sacx_user_window_desc {uint32_t width,height;char title[64];} sacx_user_window_desc;
typedef struct sacx_user_window_event {uint32_t type,buttons;int32_t x,y,delta;uint32_t reserved;uint64_t time_ms;} sacx_user_window_event;
#if defined(__aarch64__)
static inline int64_t sacx_user_window_create(const sacx_user_window_desc *d){return sacx_user_call(SACX_USER_WINDOW_CREATE,(uintptr_t)d,sizeof(*d),0);}
// Full frames are uploaded in consecutive bounded chunks, starting at zero.
// The compositor sees only kernel-owned completed frames, never user pointers.
static inline int sacx_user_window_write(uint64_t h,const uint32_t *pixels,uint32_t offset,uint32_t count){return (int)sacx_user_call(SACX_USER_WINDOW_WRITE,h,(uintptr_t)pixels,((uint64_t)offset<<32)|count);}
static inline int sacx_user_window_present(uint64_t h){return (int)sacx_user_call(SACX_USER_WINDOW_PRESENT,h,0,0);}
// Begin a packed, row-major update of at most WRITE_MAX pixels. WRITE offsets
// are relative to this region; PRESENT exposes it only after every pixel arrives.
static inline int sacx_user_window_region_begin(uint64_t h,const sacx_user_window_region *r){return (int)sacx_user_call(SACX_USER_WINDOW_REGION,h,(uintptr_t)r,sizeof(*r));}
static inline int sacx_user_window_event_next(uint64_t h,sacx_user_window_event *e){return (int)sacx_user_call(SACX_USER_WINDOW_EVENT,h,(uintptr_t)e,sizeof(*e));}
static inline int sacx_user_window_close(uint64_t h){return (int)sacx_user_call(SACX_USER_WINDOW_CLOSE,h,0,0);}
#endif
