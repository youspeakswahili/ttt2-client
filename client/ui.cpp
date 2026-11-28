// TTT2 Client - the look: colours, fonts and the widgets every page is made of.
#include "ui.h"
#include "platform.h"
#include "imgui_internal.h"

#include <cmath>
#include <cstdio>

namespace ui {

Fonts F;
SDL_Texture* g_mark = nullptr;

void Text(ImFont* f, float size, ImU32 c, const char* s)
{
    ImGui::PushFont(f, size);
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextUnformatted(s);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}
void Wrapped(ImU32 c, const char* s)
{
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextWrapped("%s", s);
    ImGui::PopStyleColor();
}
void Small(ImU32 c, const char* s)
{
    ImGui::PushFont(F.body, 13.f);
    Wrapped(c, s);
    ImGui::PopFont();
}
void Tag(const char* s, ImU32 c)
{
    ImGui::PushFont(F.bold, 12.f);
    const ImVec2 sz = ImGui::CalcTextSize(s);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = sz.y + 6, w = sz.x + 18, k = 5;
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddQuadFilled({ p.x + k, p.y }, { p.x + w, p.y }, { p.x + w - k, p.y + h }, { p.x, p.y + h }, Alpha(c, 0.16f));
    d->AddText({ p.x + 9, p.y + 3 }, c, s);
    ImGui::Dummy({ w, h });
    ImGui::PopFont();
}
void PageHeader(const char* title, const char* subtitle)
{
    Text(F.heavy, 44.f, col::text, title);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 6);
    if (subtitle && *subtitle) Text(F.body, 15.f, col::dim, subtitle);
    ImGui::Dummy({ 0, 14 });
}

void BeginCard(const char* id, float height, const char* heading, const char* sub)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(col::card));
    ImGui::PushStyleColor(ImGuiCol_Border, V(col::line));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 24, 20 });
    ImGui::BeginChild(id, { 0, height },
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | (height == 0 ? ImGuiChildFlags_AutoResizeY : 0));
    if (heading) {
        Text(F.display, 22.f, col::text, heading);
        if (sub) { ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2); Small(col::dim, sub); }
        ImGui::Dummy({ 0, 6 });
    }
}
void EndCard()
{
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}
void Divider()
{
    ImGui::Dummy({ 0, 6 });
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(p, { p.x + ImGui::GetContentRegionAvail().x, p.y }, col::line);
    ImGui::Dummy({ 0, 8 });
}
void Gap(float h) { ImGui::Dummy({ 0, h }); }

void RowLabel(const char* label, const char* help, float controlWidth)
{
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + avail - controlWidth - 28);
    Text(F.bold, 15.f, col::text, label);
    if (help && *help) { ImGui::PushFont(F.body, 13.f); Wrapped(col::dim, help); ImGui::PopFont(); }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::SameLine(avail - controlWidth + ImGui::GetStyle().WindowPadding.x);
}

int Tabs(const char* id, int current, const char* const* labels, int count)
{
    ImGui::PushID(id);
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < count; ++i) {
        if (i) ImGui::SameLine(0, 26);
        ImGui::PushID(i);
        ImGui::PushFont(F.display, 20.f);
        const ImVec2 ts = ImGui::CalcTextSize(labels[i]);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("tab", { ts.x, ts.y + 14 })) current = i;
        const bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
        d->AddText(p, i == current ? col::text : hot ? col::text : col::dim, labels[i]);
        if (i == current) d->AddRectFilled({ p.x, p.y + ts.y + 8 }, { p.x + ts.x, p.y + ts.y + 11 }, col::accent, 2.f);
        ImGui::PopFont();
        ImGui::PopID();
    }
    const ImVec2 end = ImGui::GetCursorScreenPos();
    d->AddLine({ start.x, end.y }, { start.x + width, end.y }, col::line);
    ImGui::Dummy({ 0, 14 });
    ImGui::PopID();
    return current;
}

bool BigButton(const char* label, ImVec2 size, bool enabled, ImU32 color)
{
    ImGui::PushID(label);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (!enabled) ImGui::BeginDisabled();
    const bool pressed = ImGui::InvisibleButton("##b", size);
    if (!enabled) ImGui::EndDisabled();
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float k = size.y * 0.32f;
    const ImU32 fill = !enabled ? col::cardHi : hot ? color : Alpha(color, 0.88f);
    d->AddQuadFilled({ p.x + k, p.y }, { p.x + size.x, p.y }, { p.x + size.x - k, p.y + size.y }, { p.x, p.y + size.y }, fill);
    if (hot && enabled)
        d->AddQuad({ p.x + k - 3, p.y - 3 }, { p.x + size.x + 3, p.y - 3 }, { p.x + size.x - k + 3, p.y + size.y + 3 },
                   { p.x - 3, p.y + size.y + 3 }, Alpha(color, 0.5f), 2.f);
    ImGui::PushFont(F.heavy, size.y * 0.48f);
    const ImVec2 ts = ImGui::CalcTextSize(label, nullptr, true);
    d->AddText({ p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f }, enabled ? col::ink : col::faint, label,
               ImGui::FindRenderedTextEnd(label));
    ImGui::PopFont();
    ImGui::PopID();
    return pressed && enabled;
}
bool Button(const char* label, ImVec2 size, bool enabled)
{
    if (!enabled) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, V(col::cardHi));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, V(col::hover));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, V(col::accentDeep));
    ImGui::PushFont(F.bold, 14.f);
    const bool r = ImGui::Button(label, size);
    ImGui::PopFont();
    ImGui::PopStyleColor(3);
    if (!enabled) ImGui::EndDisabled();
    return r && enabled;
}
bool LinkButton(const char* label)
{
    ImGui::PushFont(F.bold, 13.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 ts = ImGui::CalcTextSize(label, nullptr, true);
    const bool r = ImGui::InvisibleButton(label, ts);
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddText(p, hot ? col::blue : col::accent, label, ImGui::FindRenderedTextEnd(label));
    if (hot) d->AddLine({ p.x, p.y + ts.y }, { p.x + ts.x, p.y + ts.y }, col::blue);
    ImGui::PopFont();
    return r;
}
bool Segmented(const char* id, int* value, const char* const* labels, int count, float width)
{
    ImGui::PushID(id);
    bool changed = false;
    const float w = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    const float each = (w - 4.f * float(count - 1)) / float(count);
    for (int i = 0; i < count; ++i) {
        if (i) ImGui::SameLine(0, 4);
        const bool on = *value == i;
        ImGui::PushStyleColor(ImGuiCol_Button, V(on ? col::accent : col::cardHi));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, V(on ? col::accent : col::hover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, V(col::accentDeep));
        ImGui::PushStyleColor(ImGuiCol_Text, V(on ? col::ink : col::text));
        ImGui::PushFont(F.bold, 13.5f);
        if (ImGui::Button(labels[i], { each, 34 }) && !on) { *value = i; changed = true; }
        ImGui::PopFont();
        ImGui::PopStyleColor(4);
    }
    ImGui::PopID();
    return changed;
}
bool Toggle(const char* id, bool* value)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = 52, h = 28;
    bool changed = false;
    if (ImGui::InvisibleButton(id, { w, h })) { *value = !*value; changed = true; }
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, { p.x + w, p.y + h }, *value ? col::accent : hot ? col::hover : col::cardHi, h * 0.5f);
    const float r = h * 0.5f - 4;
    const float cx = *value ? p.x + w - h * 0.5f : p.x + h * 0.5f;
    d->AddCircleFilled({ cx, p.y + h * 0.5f }, r, *value ? col::ink : col::dim, 24);
    return changed;
}
bool Percent(const char* id, int* value, int max, float width)
{
    ImGui::SetNextItemWidth(width);
    ImGui::PushFont(F.bold, 14.f);
    const bool changed = ImGui::SliderInt(id, value, 0, max, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    ImGui::PopFont();
    return changed;
}
void Check(bool ok, const char* label, const char* detail)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 c = { p.x + 10, p.y + 11 };
    d->AddCircleFilled(c, 10, Alpha(ok ? col::green : col::amber, 0.15f));
    if (ok) {
        d->AddLine({ c.x - 4.5f, c.y }, { c.x - 1, c.y + 3.5f }, col::green, 2.2f);
        d->AddLine({ c.x - 1, c.y + 3.5f }, { c.x + 5, c.y - 3.5f }, col::green, 2.2f);
    } else {
        d->AddLine({ c.x, c.y - 5 }, { c.x, c.y + 1.5f }, col::amber, 2.2f);
        d->AddCircleFilled({ c.x, c.y + 5 }, 1.4f, col::amber);
    }
    ImGui::Dummy({ 30, 22 });
    ImGui::SameLine();
    ImGui::BeginGroup();
    Text(F.bold, 14.5f, col::text, label);
    if (detail && *detail) Small(col::dim, detail);
    ImGui::EndGroup();
    ImGui::Dummy({ 0, 4 });
}
void ConnectionBadge(int conn, int rtt)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const int filled = rtt <= 0 ? 0 : rtt <= 50 ? 4 : rtt <= 90 ? 3 : rtt <= 140 ? 2 : 1;
    const ImU32 on = filled >= 3 ? col::green : filled == 2 ? col::amber : col::red, off = col::line;
    for (int i = 0; i < 4; ++i) {
        const float h = 6.f + 4.f * float(i), x = p.x + float(i) * 6.f;
        d->AddRectFilled({ x, p.y + 20.f - h }, { x + 4.f, p.y + 20.f }, i < filled ? on : off, 1.f);
    }
    const float ix = p.x + 34.f, cy = p.y + 10.f;
    if (conn == 1) {   // wired: a plug
        d->AddRectFilled({ ix, cy - 4.f }, { ix + 16.f, cy + 5.f }, col::text, 2.f);
        for (int k = 0; k < 3; ++k) d->AddRectFilled({ ix + 3.f + 4.f * float(k), cy - 8.f }, { ix + 5.f + 4.f * float(k), cy - 4.f }, col::text);
        d->AddRectFilled({ ix + 6.f, cy + 5.f }, { ix + 10.f, cy + 10.f }, col::text);
    } else if (conn == 2) {   // Wi-Fi: arcs
        const ImVec2 c{ ix + 8.f, cy + 8.f };
        for (int k = 1; k <= 3; ++k) d->PathArcTo(c, 4.f * float(k), -2.36f, -0.79f, 12), d->PathStroke(col::text, 0, 2.f);
        d->AddCircleFilled(c, 1.8f, col::text);
    } else {
        d->AddText(F.bold, 16.f, { ix + 3.f, cy - 9.f }, col::faint, "?");
    }
    ImGui::Dummy({ 58.f, 22.f });
}
void Pill(const char* s, ImU32 c)
{
    ImGui::PushFont(F.bold, 12.f);
    const ImVec2 ts = ImGui::CalcTextSize(s);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, { p.x + ts.x + 18, p.y + ts.y + 8 }, Alpha(c, 0.16f), (ts.y + 8) * 0.5f);
    d->AddText({ p.x + 9, p.y + 4 }, c, s);
    ImGui::Dummy({ ts.x + 18, ts.y + 8 });
    ImGui::PopFont();
}

std::string VkName(int vk)
{
    if (vk >= 'A' && vk <= 'Z') return std::string(1, char(vk));
    if (vk >= '0' && vk <= '9') return std::string(1, char(vk));
    if (vk >= 0x60 && vk <= 0x69) return "Num " + std::to_string(vk - 0x60);
    if (vk >= 0x70 && vk <= 0x87) return "F" + std::to_string(vk - 0x70 + 1);
    switch (vk) {
    case 0x04: return "Middle mouse button";
    case 0x05: return "Mouse 4 (back)";
    case 0x06: return "Mouse 5 (forward)";
    case 0x08: return "Backspace";
    case 0x09: return "Tab";
    case 0x0C: return "Clear";
    case 0x0D: return "Enter";
    case 0x13: return "Pause";
    case 0x14: return "Caps Lock";
    case 0x20: return "Space";
    case 0x21: return "Page Up";
    case 0x22: return "Page Down";
    case 0x23: return "End";
    case 0x24: return "Home";
    case 0x25: return "Left arrow";
    case 0x26: return "Up arrow";
    case 0x27: return "Right arrow";
    case 0x28: return "Down arrow";
    case 0x2D: return "Insert";
    case 0x2E: return "Delete";
    case 0x6A: return "Num *";
    case 0x6B: return "Num +";
    case 0x6D: return "Num -";
    case 0x6E: return "Num .";
    case 0x6F: return "Num /";
    case 0x90: return "Num Lock";
    case 0x91: return "Scroll Lock";
    case 0xA0: return "Left Shift";
    case 0xA1: return "Right Shift";
    case 0xA2: return "Left Ctrl";
    case 0xA3: return "Right Ctrl";
    case 0xA4: return "Left Alt";
    case 0xA5: return "Right Alt";
    default: break;
    }
    const std::string n = plat::KeyName(vk);   // the layout's own name: ; ' [ ] ö ä ...
    if (!n.empty()) return n;
    char t[16];
    std::snprintf(t, sizeof t, "Key %02X", vk);
    return t;
}

} // namespace ui
