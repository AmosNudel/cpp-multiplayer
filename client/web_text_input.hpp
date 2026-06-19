#pragma once

#include "raylib.h"

#include <string>

#if defined(PLATFORM_WEB)
void WebTextInputShow(Rectangle screenRect, const char* value, int maxLength);
void WebTextInputHide();
std::string WebTextInputGetValue();
bool WebTextInputConsumeSubmit();
#else
inline void WebTextInputShow(Rectangle, const char*, int) {}
inline void WebTextInputHide() {}
inline std::string WebTextInputGetValue() { return {}; }
inline bool WebTextInputConsumeSubmit() { return false; }
#endif
