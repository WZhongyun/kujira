#pragma once

#include <map>
#include <string>

#include "core/FileUtil.h"

// What the pet does in one state. Empty fields mean "nothing".
struct StateAction
{
    std::string expression;  // expression name (file name without .exp3.json)
    std::string motion;      // motion name (file name without .motion3.json)
    float holdSeconds = 0;   // transient states: how long before falling back
};

struct Config
{
    // Window
    int windowX = -1;          // -1: place at the bottom-right corner
    int windowY = -1;
    int windowHeight = 320;    // logical pixels; width follows the model
    bool topmost = true;
    bool clickThrough = true;
    bool lookAtMouse = true;
    bool autostart = false;

    // Rendering
    int activeFps = 45;
    int idleFps = 12;
    int sleepMinutes = 5;

    // Model
    std::string modelDir;      // "" = first model under assets/models
    std::string idleMotion = "idle";

    // Agents
    int port = 38111;
    std::string token;         // shared secret sent by hooks
    std::string claudeHookMode = "http";  // "http" or "command"

    // State name -> action
    std::map<std::string, StateAction> actions;

    static fs::path FilePath();
    static Config Load();
    bool Save() const;

    // Fills in actions for any state missing from the map.
    void ApplyDefaultActions();
};
