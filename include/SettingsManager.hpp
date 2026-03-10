#pragma once

#include <imgui.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

// ============================================================
// SettingsManager — generic registry for runtime-editable settings.
//
// Each setting is registered with a pointer to external storage,
// a default value, metadata (label, group, tooltip, format, range),
// and an optional widget hint. The manager can auto-render ImGui
// controls for all registered settings, grouped by section.
//
// Usage:
//   cfg::SettingsManager registry;
//   registry.add("ior", &ior, 1.5f)
//       .label("IOR").group("Material").range(1.0f, 3.0f);
//   registry.addColor3("tint", &tintR, 0.8f, 0.9f, 1.0f)
//       .label("Tint Color").group("Material");
//   registry.drawGroupHeader("Material"); // auto ImGui UI
// ============================================================

namespace cfg
{

// Widget rendering hint. AUTO infers from the value type.
enum class Widget { AUTO, SLIDER, DRAG, CHECKBOX, COLOR3, COLOR4, RADIO, COMBO, INPUT_TEXT };

// ── FieldBase ────────────────────────────────────────────────────────────────
// Type-erased interface for a single entry in the settings registry.

struct FieldBase
{
  std::string           key;
  std::string           label;
  std::string           group;
  std::string           tooltip;
  Widget                widget   = Widget::AUTO;
  bool                  sameLine = false;
  std::function<bool()> visible; // null => always visible

  virtual ~FieldBase() = default;
  virtual void draw()  = 0;
  virtual void reset() = 0;
};

// ── Field<T> ─────────────────────────────────────────────────────────────────
// Typed setting field pointing to external storage.

template <typename T>
struct Field : FieldBase
{
  T                       *ptr = nullptr;
  T                        defaultVal{};
  T                        minVal{};
  T                        maxVal{};
  std::string              format;
  std::vector<std::string> options; // for RADIO / COMBO

  void reset() override
  {
    if (ptr) { *ptr = defaultVal; }
  }
  void draw() override; // specialized per type below
};

// ── Color3Entry ──────────────────────────────────────────────────────────────
// Three consecutive floats rendered as a color picker with proper reset.

struct Color3Entry : FieldBase
{
  float               *ptr = nullptr;
  std::array<float, 3> defaults{};

  void draw() override { ImGui::ColorEdit3((label + "##" + key).c_str(), ptr); }
  void reset() override
  {
    if (ptr)
    {
      ptr[0] = defaults[0];
      ptr[1] = defaults[1];
      ptr[2] = defaults[2];
    }
  }
};

// ── Vec3Entry ─────────────────────────────────────────────────────────────────
// Three floats rendered as three labelled drag/slider inputs.

struct Vec3Entry : FieldBase
{
  float                     *ptr = nullptr;
  std::array<float, 3>       defaults{};
  std::array<float, 3>       minVals{};
  std::array<float, 3>       maxVals{};
  std::array<std::string, 3> compLabels = { "X", "Y", "Z" };
  std::string                format     = "%.2f";
  bool                       horizontal = false;

  void draw() override
  {
    if (horizontal)
    {
      float avail = ImGui::GetContentRegionAvail().x;
      float w     = (avail - ImGui::GetStyle().ItemInnerSpacing.x * 2.0f) / 3.0f;
      ImGui::PushItemWidth(w);
      for (int i = 0; i < 3; i++)
      {
        if (i > 0) { ImGui::SameLine(); }
        ImGui::DragFloat((compLabels[i] + "##" + key + std::to_string(i)).c_str(), &ptr[i], 0.01f, minVals[i], maxVals[i], format.c_str());
      }
      ImGui::PopItemWidth();
    }
    else
    {
      for (int i = 0; i < 3; i++)
      {
        ImGui::SliderFloat((compLabels[i] + "##" + key + std::to_string(i)).c_str(), &ptr[i], minVals[i], maxVals[i], format.c_str());
      }
    }
  }
  void reset() override
  {
    if (ptr)
    {
      ptr[0] = defaults[0];
      ptr[1] = defaults[1];
      ptr[2] = defaults[2];
    }
  }
};

// ── AngleEntry ────────────────────────────────────────────────────────────────
// A float angle with a slider, optional degree readout, and an interactive dial popup.

struct AngleEntry : FieldBase
{
  static constexpr float kDeg2Rad = 3.14159265f / 180.0f;
  static constexpr float kRad2Deg = 180.0f / 3.14159265f;

  float *ptr        = nullptr;
  float  defaultVal = 0.0f;
  float  minVal     = -3.14159265f;
  float  maxVal     = 3.14159265f;
  bool   showDeg    = true;
  bool   inDegrees  = false; // true = value stored in degrees

  void draw() override
  {
    std::string id = label + "##" + key;
    if (inDegrees) { ImGui::SliderFloat(id.c_str(), ptr, minVal, maxVal, "%.1f\xc2\xb0"); }
    else
    {
      ImGui::SliderFloat(id.c_str(), ptr, minVal, maxVal, "%.2f rad");
      if (showDeg)
      {
        ImGui::SameLine();
        ImGui::Text("(%.1f\xc2\xb0)", *ptr * kRad2Deg);
      }
    }
    // Dial button
    ImGui::SameLine();
    if (ImGui::SmallButton(("O##dial_" + key).c_str())) { ImGui::OpenPopup(("##angleDial_" + key).c_str()); }
    if (ImGui::BeginPopup(("##angleDial_" + key).c_str()))
    {
      const float radius = 55.0f;
      const float size   = radius * 2.0f + 10.0f;
      ImVec2      pos    = ImGui::GetCursorScreenPos();
      ImVec2      center(pos.x + size * 0.5f, pos.y + size * 0.5f);
      ImDrawList *dl = ImGui::GetWindowDrawList();

      // Invisible button for interaction
      ImGui::InvisibleButton(("##dialArea_" + key).c_str(), ImVec2(size, size));
      if (ImGui::IsItemActive())
      {
        ImVec2 mp    = ImGui::GetIO().MousePos;
        float  angle = std::atan2(mp.y - center.y, mp.x - center.x);
        // Convert from dial radians to stored unit
        if (inDegrees) { angle *= kRad2Deg; }
        if (angle < minVal) { angle = minVal; }
        if (angle > maxVal) { angle = maxVal; }
        *ptr = angle;
      }

      // Draw circle
      dl->AddCircle(center, radius, IM_COL32(180, 180, 180, 255), 64, 2.0f);
      // Draw arc and line in radians for rendering
      float aRad = inDegrees ? (*ptr * kDeg2Rad) : *ptr;
      if (aRad != 0.0f)
      {
        float startA = (aRad > 0.0f) ? 0.0f : aRad;
        float endA   = (aRad > 0.0f) ? aRad : 0.0f;
        dl->PathArcTo(center, radius * 0.4f, startA, endA, 32);
        dl->PathStroke(IM_COL32(100, 180, 255, 160), 0, 4.0f);
      }
      // Draw line from center to current angle
      ImVec2 tip(center.x + radius * std::cos(aRad), center.y + radius * std::sin(aRad));
      dl->AddLine(center, tip, IM_COL32(255, 200, 80, 255), 2.0f);
      // Center dot
      dl->AddCircleFilled(center, 3.0f, IM_COL32(255, 255, 255, 255));

      ImGui::EndPopup();
    }
  }
  void reset() override
  {
    if (ptr) { *ptr = defaultVal; }
  }
};

// ── Decorator entries ────────────────────────────────────────────────────────

struct SeparatorEntry : FieldBase
{
  void draw() override { ImGui::Separator(); }
  void reset() override {}
};

struct LabelEntry : FieldBase
{
  void draw() override { ImGui::TextDisabled("%s", label.c_str()); }
  void reset() override {}
};

struct CallbackEntry : FieldBase
{
  std::function<void()> callback;
  void                  draw() override
  {
    if (callback) { callback(); }
  }
  void reset() override {}
};

// ── SettingsManager ──────────────────────────────────────────────────────────

class SettingsManager
{
  public:
  // ── Builder (fluent typed-field registration) ────────────────────────────

  template <typename T>
  class Builder
  {
    public:
    Builder(SettingsManager &mgr, std::unique_ptr<Field<T>> f)
        : mgr_(mgr)
        , field_(std::move(f))
    {}
    ~Builder()
    {
      if (field_) { mgr_.push(std::move(field_)); }
    }
    Builder(Builder &&)            = default;
    Builder &operator=(Builder &&) = default;

    Builder &label(std::string l)
    {
      field_->label = std::move(l);
      return *this;
    }
    Builder &group(std::string g)
    {
      field_->group = std::move(g);
      return *this;
    }
    Builder &tooltip(std::string t)
    {
      field_->tooltip = std::move(t);
      return *this;
    }
    Builder &format(std::string f)
    {
      field_->format = std::move(f);
      return *this;
    }
    Builder &widget(Widget w)
    {
      field_->widget = w;
      return *this;
    }
    Builder &range(T lo, T hi)
    {
      field_->minVal = lo;
      field_->maxVal = hi;
      return *this;
    }
    Builder &sameLine(bool v = true)
    {
      field_->sameLine = v;
      return *this;
    }
    Builder &options(std::vector<std::string> o)
    {
      field_->options = std::move(o);
      return *this;
    }
    Builder &visibleWhen(std::function<bool()> pred)
    {
      field_->visible = std::move(pred);
      return *this;
    }

    private:
    SettingsManager          &mgr_;
    std::unique_ptr<Field<T>> field_;
  };

  // ── Color3Builder (fluent color registration) ────────────────────────────

  class Color3Builder
  {
    public:
    Color3Builder(SettingsManager &mgr, std::unique_ptr<Color3Entry> e)
        : mgr_(mgr)
        , entry_(std::move(e))
    {}
    ~Color3Builder()
    {
      if (entry_) { mgr_.push(std::move(entry_)); }
    }
    Color3Builder(Color3Builder &&)            = default;
    Color3Builder &operator=(Color3Builder &&) = default;

    Color3Builder &label(std::string l)
    {
      entry_->label = std::move(l);
      return *this;
    }
    Color3Builder &group(std::string g)
    {
      entry_->group = std::move(g);
      return *this;
    }
    Color3Builder &tooltip(std::string t)
    {
      entry_->tooltip = std::move(t);
      return *this;
    }
    Color3Builder &sameLine(bool v = true)
    {
      entry_->sameLine = v;
      return *this;
    }
    Color3Builder &visibleWhen(std::function<bool()> pred)
    {
      entry_->visible = std::move(pred);
      return *this;
    }

    private:
    SettingsManager             &mgr_;
    std::unique_ptr<Color3Entry> entry_;
  };

  // ── Vec3Builder (fluent vec3-field registration) ─────────────────────────

  class Vec3Builder
  {
    public:
    Vec3Builder(SettingsManager &mgr, std::unique_ptr<Vec3Entry> e)
        : mgr_(mgr)
        , entry_(std::move(e))
    {}
    ~Vec3Builder()
    {
      if (entry_) { mgr_.push(std::move(entry_)); }
    }
    Vec3Builder(Vec3Builder &&)            = default;
    Vec3Builder &operator=(Vec3Builder &&) = default;

    Vec3Builder &label(std::string l)
    {
      entry_->label = std::move(l);
      return *this;
    }
    Vec3Builder &group(std::string g)
    {
      entry_->group = std::move(g);
      return *this;
    }
    Vec3Builder &tooltip(std::string t)
    {
      entry_->tooltip = std::move(t);
      return *this;
    }
    Vec3Builder &format(std::string f)
    {
      entry_->format = std::move(f);
      return *this;
    }
    Vec3Builder &sameLine(bool v = true)
    {
      entry_->sameLine = v;
      return *this;
    }
    Vec3Builder &horizontal(bool v = true)
    {
      entry_->horizontal = v;
      return *this;
    }
    Vec3Builder &range(float lo, float hi)
    {
      entry_->minVals = { lo, lo, lo };
      entry_->maxVals = { hi, hi, hi };
      return *this;
    }
    Vec3Builder &rangeX(float lo, float hi)
    {
      entry_->minVals[0] = lo;
      entry_->maxVals[0] = hi;
      return *this;
    }
    Vec3Builder &rangeY(float lo, float hi)
    {
      entry_->minVals[1] = lo;
      entry_->maxVals[1] = hi;
      return *this;
    }
    Vec3Builder &rangeZ(float lo, float hi)
    {
      entry_->minVals[2] = lo;
      entry_->maxVals[2] = hi;
      return *this;
    }
    Vec3Builder &compLabels(std::string x, std::string y, std::string z)
    {
      entry_->compLabels = { std::move(x), std::move(y), std::move(z) };
      return *this;
    }
    Vec3Builder &visibleWhen(std::function<bool()> pred)
    {
      entry_->visible = std::move(pred);
      return *this;
    }

    private:
    SettingsManager           &mgr_;
    std::unique_ptr<Vec3Entry> entry_;
  };

  // ── AngleBuilder (fluent angle-field registration) ────────────────────────

  class AngleBuilder
  {
    public:
    AngleBuilder(SettingsManager &mgr, std::unique_ptr<AngleEntry> e)
        : mgr_(mgr)
        , entry_(std::move(e))
    {}
    ~AngleBuilder()
    {
      if (entry_) { mgr_.push(std::move(entry_)); }
    }
    AngleBuilder(AngleBuilder &&)            = default;
    AngleBuilder &operator=(AngleBuilder &&) = default;

    AngleBuilder &label(std::string l)
    {
      entry_->label = std::move(l);
      return *this;
    }
    AngleBuilder &group(std::string g)
    {
      entry_->group = std::move(g);
      return *this;
    }
    AngleBuilder &tooltip(std::string t)
    {
      entry_->tooltip = std::move(t);
      return *this;
    }
    AngleBuilder &sameLine(bool v = true)
    {
      entry_->sameLine = v;
      return *this;
    }
    AngleBuilder &showDegrees(bool v = true)
    {
      entry_->showDeg = v;
      return *this;
    }
    AngleBuilder &degrees(bool v = true)
    {
      entry_->inDegrees = v;
      return *this;
    }
    AngleBuilder &range(float lo, float hi)
    {
      entry_->minVal = lo;
      entry_->maxVal = hi;
      return *this;
    }
    AngleBuilder &visibleWhen(std::function<bool()> pred)
    {
      entry_->visible = std::move(pred);
      return *this;
    }

    private:
    SettingsManager            &mgr_;
    std::unique_ptr<AngleEntry> entry_;
  };

  // ── Registration ─────────────────────────────────────────────────────────

  // Register a typed setting. Returns a builder for chaining metadata.
  template <typename T>
  Builder<T> add(std::string key, T *ptr, T defaultVal)
  {
    auto f        = std::make_unique<Field<T>>();
    f->key        = key;
    f->label      = key;
    f->ptr        = ptr;
    f->defaultVal = defaultVal;
    return Builder<T>(*this, std::move(f));
  }

  // Register an enum class setting as an int field (for RADIO / COMBO).
  template <typename E>
  requires(std::is_enum_v<E> && sizeof(E) == sizeof(int)) Builder<int> addEnum(std::string key, E *ptr, E defaultVal)
  {
    return add<int>(std::move(key), reinterpret_cast<int *>(ptr), static_cast<int>(defaultVal));
  }

  // Register 3 consecutive floats as a color picker.
  Color3Builder addColor3(std::string key, float *rgb, float r, float g, float b)
  {
    auto e      = std::make_unique<Color3Entry>();
    e->key      = std::move(key);
    e->label    = e->key;
    e->ptr      = rgb;
    e->defaults = { r, g, b };
    return Color3Builder(*this, std::move(e));
  }

  // Register 3 consecutive floats as component drag/slider inputs.
  Vec3Builder addVec3(std::string key, float *xyz, float dx, float dy, float dz)
  {
    auto e      = std::make_unique<Vec3Entry>();
    e->key      = std::move(key);
    e->label    = e->key;
    e->ptr      = xyz;
    e->defaults = { dx, dy, dz };
    return Vec3Builder(*this, std::move(e));
  }

  // Register a float angle with a slider and interactive dial popup.
  AngleBuilder addAngle(std::string key, float *ptr, float defaultVal)
  {
    auto e        = std::make_unique<AngleEntry>();
    e->key        = std::move(key);
    e->label      = e->key;
    e->ptr        = ptr;
    e->defaultVal = defaultVal;
    return AngleBuilder(*this, std::move(e));
  }

  void addSeparator(std::string group, std::function<bool()> vis = {})
  {
    auto e     = std::make_unique<SeparatorEntry>();
    e->group   = std::move(group);
    e->visible = std::move(vis);
    push(std::move(e));
  }

  void addLabel(std::string text, std::string group, std::function<bool()> vis = {})
  {
    auto e     = std::make_unique<LabelEntry>();
    e->label   = std::move(text);
    e->group   = std::move(group);
    e->visible = std::move(vis);
    push(std::move(e));
  }

  void addCallback(std::string group, std::function<void()> cb, std::function<bool()> vis = {})
  {
    auto e      = std::make_unique<CallbackEntry>();
    e->group    = std::move(group);
    e->callback = std::move(cb);
    e->visible  = std::move(vis);
    push(std::move(e));
  }

  // ── Access ───────────────────────────────────────────────────────────────

  template <typename T>
  T &get(const std::string &key)
  {
    auto *f = findField<T>(key);
    assert(f && f->ptr);
    return *f->ptr;
  }

  template <typename T>
  T *ptr(const std::string &key)
  {
    auto *f = findField<T>(key);
    return f ? f->ptr : nullptr;
  }

  bool has(const std::string &key) const { return index_.contains(key); }

  // ── Reset ────────────────────────────────────────────────────────────────

  void reset(const std::string &key)
  {
    if (auto it = index_.find(key); it != index_.end()) { entries_[it->second]->reset(); }
  }

  void resetAll()
  {
    for (auto &e : entries_) { e->reset(); }
  }

  void resetGroup(const std::string &group)
  {
    for (auto &e : entries_)
    {
      if (e->group == group) { e->reset(); }
    }
  }

  // ── ImGui rendering ─────────────────────────────────────────────────────

  // Draw all entries for a group (no collapsing header — caller manages layout).
  void drawGroup(const std::string &group)
  {
    for (auto &e : entries_)
    {
      if (e->group != group) { continue; }
      if (e->visible && !e->visible()) { continue; }
      if (e->sameLine) { ImGui::SameLine(); }
      e->draw();
      if (!e->tooltip.empty() && ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", e->tooltip.c_str()); }
    }
  }

  // Draw a group with a collapsing header. Returns true if header is open.
  bool drawGroupHeader(const std::string &group, bool defaultOpen = true)
  {
    ImGuiTreeNodeFlags flags = defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags(0);
    if (!ImGui::CollapsingHeader(group.c_str(), flags)) { return false; }
    drawGroup(group);
    return true;
  }

  // Draw all groups, each with its own collapsing header.
  void drawAll()
  {
    std::string lastGroup;
    bool        groupOpen = false;
    for (auto &e : entries_)
    {
      if (e->group != lastGroup)
      {
        lastGroup = e->group;
        groupOpen = ImGui::CollapsingHeader(lastGroup.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
      }
      if (!groupOpen) { continue; }
      if (e->visible && !e->visible()) { continue; }
      if (e->sameLine) { ImGui::SameLine(); }
      e->draw();
      if (!e->tooltip.empty() && ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", e->tooltip.c_str()); }
    }
  }

  // Ordered list of unique group names.
  std::vector<std::string> groups() const
  {
    std::vector<std::string> result;
    for (auto &e : entries_)
    {
      if (!e->group.empty() && (result.empty() || result.back() != e->group)) { result.push_back(e->group); }
    }
    return result;
  }

  const std::vector<std::unique_ptr<FieldBase>> &entries() const { return entries_; }

  private:
  std::vector<std::unique_ptr<FieldBase>> entries_;
  std::unordered_map<std::string, size_t> index_;

  void push(std::unique_ptr<FieldBase> e)
  {
    if (!e->key.empty()) { index_[e->key] = entries_.size(); }
    entries_.push_back(std::move(e));
  }

  template <typename T>
  Field<T> *findField(const std::string &key)
  {
    auto it = index_.find(key);
    if (it == index_.end()) { return nullptr; }
    return dynamic_cast<Field<T> *>(entries_[it->second].get());
  }
};

// ── Field<T>::draw() specializations ─────────────────────────────────────────

template <>
inline void Field<float>::draw()
{
  Widget      w   = (widget == Widget::AUTO) ? Widget::SLIDER : widget;
  const char *fmt = format.empty() ? "%.2f" : format.c_str();
  std::string id  = label + "##" + key;
  switch (w)
  {
  case Widget::COLOR3: ImGui::ColorEdit3(id.c_str(), ptr); break;
  case Widget::COLOR4: ImGui::ColorEdit4(id.c_str(), ptr); break;
  case Widget::DRAG: ImGui::DragFloat(id.c_str(), ptr, 0.01f, minVal, maxVal, fmt); break;
  default: ImGui::SliderFloat(id.c_str(), ptr, minVal, maxVal, fmt); break;
  }
}

template <>
inline void Field<int>::draw()
{
  Widget      w  = (widget == Widget::AUTO) ? Widget::SLIDER : widget;
  std::string id = label + "##" + key;
  switch (w)
  {
  case Widget::RADIO:
    for (size_t i = 0; i < options.size(); i++)
    {
      if (i > 0) { ImGui::SameLine(); }
      ImGui::RadioButton((options[i] + "##" + key).c_str(), ptr, static_cast<int>(i));
    }
    break;
  case Widget::COMBO:
  {
    std::string items;
    for (auto &o : options)
    {
      items += o;
      items += '\0';
    }
    items += '\0';
    ImGui::Combo(id.c_str(), ptr, items.c_str());
    break;
  }
  default: ImGui::SliderInt(id.c_str(), ptr, minVal, maxVal); break;
  }
}

template <>
inline void Field<bool>::draw()
{
  ImGui::Checkbox((label + "##" + key).c_str(), ptr);
}

template <>
inline void Field<std::string>::draw()
{
  char buf[256];
  std::strncpy(buf, ptr->c_str(), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::InputText((label + "##" + key).c_str(), buf, sizeof(buf))) { *ptr = buf; }
}

} // namespace cfg
