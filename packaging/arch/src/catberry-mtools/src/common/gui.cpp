#include "gui.h"

#include <cctype>

TextField::Result TextField::key(const TypedKey &press) {
    switch (press.special) {
    case TypedKey::Enter:
    case TypedKey::Tab:
    case TypedKey::Escape:
        return Committed;
    case TypedKey::Backspace:
        if (text.empty()) return Ignored;
        text.pop_back();
        return Changed;
    case TypedKey::None:
        break;
    }
    char c = press.ch;
    if (!c || text.size() >= maxLen) return Ignored;
    bool ok = false;
    switch (kind) {
    case Decimal:
        ok = std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && text.find('.') == std::string::npos);
        break;
    case Integer: ok = std::isdigit(static_cast<unsigned char>(c)); break;
    case Name: ok = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; break;
    }
    if (!ok) return Ignored;
    text += c;
    return Changed;
}

void TextField::draw(Canvas &canvas) const {
    canvas.button(rect, focused ? text + "_" : text, focused ? Color::Bg : Color::Panel, Color::Text);
}
