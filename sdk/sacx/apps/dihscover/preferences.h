#pragma once
#include "dihscover.h"
struct browser_preferences {uint32_t sans,serif,mono,minimum,zoom;};
extern browser_preferences browser_prefs;
void browser_preferences_load(const sacx_api*);
int browser_preferences_save(const sacx_api*);
