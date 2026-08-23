#pragma once
#include "../Config/Config.h"
#include <Windows.h>
#include <cstdint>
#include <string>

namespace Render {
namespace ClickGUITheme {

enum class Style { Solid, LiquidGlass, Minimal };

inline Style style() {
  const std::string &s = Config::getClickGuiTheme();
  if (s == "LiquidGlass") return Style::LiquidGlass;
  if (s == "Minimal")     return Style::Minimal;
  return Style::Solid;
}

inline DWORD accent() { return Config::getThemeColor() | 0xFF000000; }
#define THEME_NAVY (Render::ClickGUITheme::accent())

inline DWORD panelBg() {
  switch (style()) {
  case Style::LiquidGlass: return 0x2210131C;
  case Style::Minimal:     return 0xF409090B;
  default:                 return 0xF209090B;
  }
}
inline DWORD sidebarBg() {
  switch (style()) {
  case Style::LiquidGlass: return 0x22161929;
  case Style::Minimal:     return 0xFF0C0C0E;
  default:                 return 0xF60C0C0E;
  }
}
inline DWORD cardBg() {
  switch (style()) {
  case Style::LiquidGlass: return 0x301F2334;
  case Style::Minimal:     return 0xFF101012;
  default:                 return 0xFF101012;
  }
}
inline DWORD cardHover() {
  switch (style()) {
  case Style::LiquidGlass: return 0x452A3148;
  case Style::Minimal:     return 0xFF18181B;
  default:                 return 0xFF18181B;
  }
}
inline DWORD border() {
  switch (style()) {
  case Style::LiquidGlass: return 0x22FFFFFF;
  case Style::Minimal:     return 0xFF27272A;
  default:                 return 0xFF27272A;
  }
}
inline DWORD textPrimary()   { return 0xFFFAFAFA; }
inline DWORD textSecondary() { return 0xFFA1A1AA; }
inline DWORD textMuted()     { return 0xFF71717A; }

inline DWORD surface1()       { return 0xFF141416; }
inline DWORD surface2()       { return 0xFF27272A; }
inline DWORD hairline()       { return 0xFF27272A; }
inline DWORD hairlineStrong() { return 0xFF3F3F46; }
inline DWORD inset()          { return 0xFF0C0C0E; }
inline DWORD online()         { return 0xFF22C55E; }
inline DWORD danger()         { return 0xFFEF4444; }

inline float panelRadius() {
  return style() == Style::LiquidGlass ? 18.0f : 12.0f;
}
inline float cardRadius() {
  return style() == Style::LiquidGlass ? 14.0f : 8.0f;
}
inline float buttonRadius() {
  return style() == Style::LiquidGlass ? 10.0f : 6.0f;
}
inline float controlRadius() { return buttonRadius(); }
inline float windowRadius()  { return 8.0f; }
inline float pillRadius()    { return style() == Style::LiquidGlass ? 10.0f : 6.0f; }
inline float chipRadius()    { return 4.0f; }

inline float shadowSpread() {
  return style() == Style::LiquidGlass ? 20.0f : 6.0f;
}
inline bool hasGlassDecorations() { return style() == Style::LiquidGlass; }
// The pulsing radial gradients are gone: seven of them were being redrawn every
// frame behind a menu nobody was looking past.
inline bool hasAnimatedBackground() { return false; }

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline uint32_t applyAlpha(uint32_t color, float alpha) {
  uint8_t a = (uint8_t)(((color >> 24) & 0xFF) * alpha);
  return (uint32_t)((a << 24) | (color & 0x00FFFFFF));
}

inline DWORD accentSoft()   { return (accent() & 0x00FFFFFF) | 0x1F000000; }
inline DWORD accentBorder() { return (accent() & 0x00FFFFFF) | 0x4D000000; }
inline DWORD accentGlow()   { return (accent() & 0x00FFFFFF) | 0x59000000; }

inline bool isHovered(float mx, float my, float x, float y, float w, float h) {
  return mx >= x && mx <= x + w && my >= y && my <= y + h;
}

inline float ease(float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  return t * t * (3.0f - 2.0f * t);
}

inline float easeOut(float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  float u = 1.0f - t;
  return 1.0f - u * u * u;
}

} // namespace ClickGUITheme
} // namespace Render

#define THEME_BG         (Render::ClickGUITheme::panelBg())
#define THEME_SIDEBAR    (Render::ClickGUITheme::sidebarBg())
#define THEME_CARD       (Render::ClickGUITheme::cardBg())
#define THEME_CARD_HOVER (Render::ClickGUITheme::cardHover())
#define THEME_BORDER     (Render::ClickGUITheme::border())

namespace Render {
using ClickGUITheme::lerp;
using ClickGUITheme::applyAlpha;
using ClickGUITheme::isHovered;
}
