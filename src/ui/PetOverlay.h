#pragma once

#include <string>

#include "platform/Platform.h"

struct GLFWwindow;
struct ImGuiContext;
struct ImFont;

// Speech bubble and hover toolbar, drawn with Dear ImGui into the pet window
// on top of the model. Input is handled by the App; this class only draws and
// answers hit tests. All coordinates are window coordinates.
class PetOverlay
{
public:
    enum class Priority
    {
        Chatter,      // idle small talk
        Interaction,  // reactions to the mouse
        Status,       // what the agent is doing
        Result,       // the agent finished
        Attention,    // the agent waits for the user
    };

    enum class Button
    {
        None = -1,
        Settings,
        Quiet,
        Quit,
        Count
    };

    struct Rect
    {
        float x = 0, y = 0, w = 0, h = 0;
        bool Contains(double px, double py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    };

    struct Layout
    {
        Rect model;            // where the model is drawn
        float bubbleTop = 0;   // top of the space reserved for the bubble
        float bubbleMaxW = 0;
        float scale = 1;       // window coordinates per logical pixel
        bool toolbar = false;
    };

    ~PetOverlay();

    bool Init(GLFWwindow* window);
    void Shutdown();

    // Shows `text`, replacing the current bubble unless that one is more important.
    // seconds <= 0 keeps it until ClearSticky() or a click.
    void Say(const std::string& text, float seconds, Priority priority, double now);
    void ClearSticky();
    void Dismiss();
    bool BubbleVisible(double now) const;
    Priority CurrentPriority() const { return _priority; }

    // Draws into the current framebuffer. `showToolbar` fades the toolbar in or out.
    void Render(const Layout& layout, double now, bool showToolbar, bool quiet, double cursorX, double cursorY);
    bool Animating() const { return _animating; }

    Button ButtonAt(double x, double y) const;
    bool BubbleContains(double x, double y) const { return _bubbleAlpha > 0.05f && _bubbleRect.Contains(x, y); }

private:
    Rect ButtonRect(const Layout& layout, int index) const;

    GLFWwindow* _window = nullptr;
    ImGuiContext* _imgui = nullptr;
    Platform::MappedFile _fontFile;
    double _lastRender = 0;

    std::string _text;
    Priority _priority = Priority::Chatter;
    double _shownAt = -100;
    double _until = -100;   // < 0 with _sticky: forever
    bool _sticky = false;
    std::string _pendingText;  // agent status that arrived while the previous one was still fresh
    float _pendingSeconds = 0;
    Rect _bubbleRect;
    float _bubbleAlpha = 0;

    float _toolbarAlpha = 0;
    Rect _buttonRects[static_cast<int>(Button::Count)];
    bool _animating = false;
};
