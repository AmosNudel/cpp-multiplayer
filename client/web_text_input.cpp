#include "client/web_text_input.hpp"

#if defined(PLATFORM_WEB)

#include <emscripten.h>

namespace {

bool gSubmitRequested = false;

EM_JS(void, JsShowTextInput, (float x, float y, float w, float h, const char* value, int maxLength), {
    let input = Module._gameTextInput;
    if (!input) {
        input = document.createElement('input');
        input.type = 'text';
        input.autocomplete = 'off';
        input.autocapitalize = 'off';
        input.spellcheck = false;
        input.style.position = 'absolute';
        input.style.zIndex = '1000';
        input.style.margin = '0';
        input.style.padding = '0 6px';
        input.style.fontSize = '16px';
        input.style.fontFamily = 'system-ui, sans-serif';
        input.style.border = '1px solid rgba(130, 136, 152, 0.9)';
        input.style.borderRadius = '4px';
        input.style.outline = 'none';
        input.style.background = 'rgba(30, 34, 42, 0.96)';
        input.style.color = '#ffe066';
        input.style.boxSizing = 'border-box';
        input.addEventListener('keydown', (event) => {
            if (event.key === 'Enter') {
                event.preventDefault();
                Module.ccall('NetNameInputSubmit', null, [], []);
            }
        });
        document.body.appendChild(input);
        Module._gameTextInput = input;
    }

    input.maxLength = maxLength;
    input.style.left = x + 'px';
    input.style.top = y + 'px';
    input.style.width = w + 'px';
    input.style.height = h + 'px';
    const wasHidden = input.style.display === 'none';
    input.style.display = 'block';
    if (wasHidden) {
        input.value = UTF8ToString(value);
        input.focus();
        input.select();
    }
});

EM_JS(void, JsHideTextInput, (), {
    const input = Module._gameTextInput;
    if (!input) {
        return;
    }
    input.style.display = 'none';
    input.blur();
});

EM_JS(int, JsCopyTextInputValue, (char* buffer, int bufferSize), {
    const input = Module._gameTextInput;
    if (!input || input.style.display === 'none') {
        return 0;
    }
    stringToUTF8(input.value, buffer, bufferSize);
    return 1;
});

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
void NetNameInputSubmit() {
    gSubmitRequested = true;
}

}  // extern "C"

void WebTextInputShow(Rectangle screenRect, const char* value, int maxLength) {
    gSubmitRequested = false;
    JsShowTextInput(screenRect.x, screenRect.y, screenRect.width, screenRect.height, value,
                    maxLength);
}

void WebTextInputHide() {
    gSubmitRequested = false;
    JsHideTextInput();
}

std::string WebTextInputGetValue() {
    char buffer[32] = {};
    if (JsCopyTextInputValue(buffer, static_cast<int>(sizeof(buffer))) != 0) {
        return buffer;
    }
    return {};
}

bool WebTextInputConsumeSubmit() {
    if (!gSubmitRequested) {
        return false;
    }
    gSubmitRequested = false;
    return true;
}

#endif  // PLATFORM_WEB
