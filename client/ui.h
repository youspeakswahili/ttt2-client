// TTT2 Client - the look: colours, fonts and the widgets every page is made of.
#pragma once
#include "imgui.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <string>

namespace ui {

// ---------------------------------------------------------------- colours
namespace col {
inline constexpr ImU32 bg = IM_COL32(9, 9, 12, 255);
inline constexpr ImU32 side = IM_COL32(13, 13, 17, 255);
inline constexpr ImU32 card = IM_COL32(19, 19, 25, 255);
inline constexpr ImU32 cardHi = IM_COL32(28, 28, 36, 255);
inline constexpr ImU32 hover = IM_COL32(40, 40, 52, 255);
inline constexpr ImU32 line = IM_COL32(38, 38, 48, 255);
inline constexpr ImU32 text = IM_COL32(236, 236, 240, 255);
inline constexpr ImU32 dim = IM_COL32(146, 146, 162, 255);
inline constexpr ImU32 faint = IM_COL32(92, 92, 106, 255);
inline constexpr ImU32 accent = IM_COL32(70, 160, 255, 255);      // electric blue
inline constexpr ImU32 accentDeep = IM_COL32(30, 90, 200, 255);
inline constexpr ImU32 ink = IM_COL32(6, 12, 24, 255);            // text on the accent
inline constexpr ImU32 amber = IM_COL32(233, 180, 76, 255);       // needs attention
inline constexpr ImU32 red = IM_COL32(224, 48, 60, 255);
inline constexpr ImU32 green = IM_COL32(64, 214, 128, 255);
inline constexpr ImU32 blue = IM_COL32(150, 215, 255, 255);
} // namespace col
inline ImVec4 V(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
inline ImU32 Alpha(ImU32 c, float a) { return (c & 0x00FFFFFF) | (ImU32(std::clamp(a, 0.f, 1.f) * 255.f) << 24); }

// ---------------------------------------------------------------- fonts (made by App::Init)
struct Fonts { ImFont* body = nullptr; ImFont* bold = nullptr; ImFont* display = nullptr; ImFont* heavy = nullptr; };
extern Fonts F;
extern SDL_Texture* g_mark;   // the client's mark (res/mark.h)

// ---------------------------------------------------------------- text
void Text(ImFont* f, float size, ImU32 c, const char* s);
void Wrapped(ImU32 c, const char* s);
void Small(ImU32 c, const char* s);                  // 13 px body text, wrapped
void Tag(const char* s, ImU32 c);                    // a slanted label: READY, IN GAME...
void PageHeader(const char* title, const char* subtitle);

// ---------------------------------------------------------------- layout
// A card: a rounded panel with an optional heading; EndCard() closes it.
void BeginCard(const char* id, float height = 0, const char* heading = nullptr, const char* sub = nullptr);
void EndCard();
void Divider();
void Gap(float h = 12);
// A setting row: label + help on the left, the control (controlWidth wide) on the right
void RowLabel(const char* label, const char* help, float controlWidth);
// Tabs along the top of a page; returns the chosen one
int Tabs(const char* id, int current, const char* const* labels, int count);

// ---------------------------------------------------------------- controls
bool BigButton(const char* label, ImVec2 size, bool enabled = true, ImU32 color = col::accent);
bool Button(const char* label, ImVec2 size = { 0, 0 }, bool enabled = true);
bool LinkButton(const char* label);
bool Segmented(const char* id, int* value, const char* const* labels, int count, float width = 0);
bool Toggle(const char* id, bool* value);            // an on/off switch
bool Percent(const char* id, int* value, int max, float width);   // a 0..max % slider; true while it changes
void Check(bool ok, const char* label, const char* detail);
void ConnectionBadge(int conn, int rtt);
void Pill(const char* s, ImU32 c);                   // a small rounded status chip

// A Windows virtual-key code as a player knows it: "Caps Lock", "Mouse 4 (back)", "F12"...
std::string VkName(int vk);

} // namespace ui
