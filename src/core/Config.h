#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/FileUtil.h"

// What the pet does in one state. Empty fields mean "nothing".
struct StateAction
{
    std::vector<std::string> expressions;  // expression names (file names without .exp3.json), stacked
    std::string motion;      // motion name (file name without .motion3.json), played over the idle motion
    bool loop = false;       // repeat the motion for as long as the state lasts
    float holdSeconds = 0;   // transient states: how long before falling back
};

struct Config
{
    // Window
    int windowX = -1;          // -1: place at the bottom-right corner
    int windowY = -1;
    int windowHeight = 320;    // logical pixels; width follows the model
    bool topmost = true;
    bool keepOnTop = true;         // re-raise above the taskbar after it is clicked
    bool hideForFullscreen = true; // hide while a fullscreen app is in front
    bool clickThrough = true;
    bool lookAtMouse = true;
    bool autostart = false;

    // Rendering
    int activeFps = 60;
    int idleFps = 60;
    int sleepMinutes = 5;

    // Speech bubble
    std::string bubbleMode = "all";  // "all", "important" (agent needs you / finished), "off"
    float bubbleSeconds = 5.0f;      // how long a line stays up
    int chatMinutes = 15;            // idle small talk every ~N minutes, 0 = never
    bool interactionText = true;     // lines when hovered, clicked, dragged
    bool quietMode = false;          // toolbar switch: no small talk or interaction lines
    bool showToolbar = true;         // settings / quiet / quit buttons on hover

    // Model
    std::string modelDir;      // "" = first model under assets/models
    std::string idleMotion = "idle";
    std::vector<std::string> outfit;  // expressions that stay on in every state (props, hair, stickers)

    // Agents
    int port = 38111;
    std::string token;         // shared secret sent by hooks
    std::string claudeHookMode = "app";  // "app" (Kujira --hook forwards) or "command" (bash + curl)
    bool launchWithAgent = false;        // "app" mode: a new agent session starts the pet if it isn't running

    // State name -> action
    std::map<std::string, StateAction> actions;
    // Version of the default actions the saved ones were based on; older sets are replaced.
    int actionsVersion = 0;
    static constexpr int kActionsVersion = 2;

    static fs::path FilePath();
    static Config Load();
    bool Save() const;

    // Fills in actions for any state missing from the map.
    void ApplyDefaultActions();
};
