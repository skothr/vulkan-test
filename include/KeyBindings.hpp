#pragma once

#include <GLFW/glfw3.h>
#include <functional>
#include <string>
#include <vector>

// ============================================================
// KeyBindings — maps (key + modifier) combinations to callbacks.
//
// Usage:
//   keys.bind(GLFW_KEY_Q, GLFW_MOD_CONTROL, "Ctrl+Q  Quit", [&]() { ... });
//   // In GLFW key callback:
//   keys.process(key, mods);
// ============================================================

class KeyBindings
{
  public:
  struct Binding
  {
    int                   key;
    int                   mods;
    std::string           description;
    std::function<void()> action;
  };

  void bind(int key, int mods, std::string description, std::function<void()> action)
  {
    bindings.push_back({ key, mods, std::move(description), std::move(action) });
  }

  // Execute the first matching binding. Returns true if one was found.
  // Strips Caps Lock / Num Lock from mods before matching.
  bool process(int key, int mods)
  {
    int m = mods & ~(GLFW_MOD_CAPS_LOCK | GLFW_MOD_NUM_LOCK);
    for (auto &b : bindings)
    {
      if (b.key == key && b.mods == m)
      {
        b.action();
        return true;
      }
    }
    return false;
  }

  // Print all registered bindings to stdout.
  void printHelp() const
  {
    std::printf("\n  Key Bindings:\n");
    for (auto &b : bindings) { std::printf("    %s\n", b.description.c_str()); }
    std::printf("\n");
  }

  const std::vector<Binding> &getBindings() const { return bindings; }

  private:
  std::vector<Binding> bindings;
};
