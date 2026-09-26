#include "ui/Theme.h"

namespace Theme
{
ImVec4 Hex(unsigned rgb, float a)
{
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}

void Apply()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 14; s.ChildRounding = 12; s.FrameRounding = 8; s.PopupRounding = 10;
    s.GrabRounding = 8; s.TabRounding = 8; s.ScrollbarRounding = 8;
    s.WindowPadding = ImVec2(18, 16); s.FramePadding = ImVec2(12, 7); s.ItemSpacing = ImVec2(10, 12);
    s.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    s.WindowBorderSize = 0; s.FrameBorderSize = 0; s.ChildBorderSize = 0; s.PopupBorderSize = 1;
    s.GrabMinSize = 16; s.ScrollbarSize = 12;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]              = Hex(0x2B3553);
    c[ImGuiCol_TextDisabled]      = Hex(0x8A93AD);
    c[ImGuiCol_WindowBg]          = Hex(kBackground);
    c[ImGuiCol_ChildBg]           = Hex(0xFFFFFF);
    c[ImGuiCol_PopupBg]           = Hex(0xFFFFFF);
    c[ImGuiCol_Border]            = Hex(0xDDE3F0);
    c[ImGuiCol_FrameBg]           = Hex(0xE8EEF9);
    c[ImGuiCol_FrameBgHovered]    = Hex(0xDCE6F8);
    c[ImGuiCol_FrameBgActive]     = Hex(0xCFDDF6);
    c[ImGuiCol_CheckMark]         = Hex(0x3F63B8);
    c[ImGuiCol_SliderGrab]        = Hex(kAccent);
    c[ImGuiCol_SliderGrabActive]  = Hex(0x3A5AA8);
    // Plain buttons (combo arrows, small controls) stay light; PrimaryButton is blue.
    c[ImGuiCol_Button]            = Hex(0xDCE6F8);
    c[ImGuiCol_ButtonHovered]     = Hex(0xCFDDF6);
    c[ImGuiCol_ButtonActive]      = Hex(0xC2D3F2);
    c[ImGuiCol_Header]            = Hex(0xDCE6F8);
    c[ImGuiCol_HeaderHovered]     = Hex(0xE6EDFA);
    c[ImGuiCol_HeaderActive]      = Hex(0xCFDDF6);
    c[ImGuiCol_Separator]         = Hex(0xE3E8F3);
    c[ImGuiCol_ScrollbarBg]       = Hex(0xFFFFFF, 0);
    c[ImGuiCol_ScrollbarGrab]     = Hex(0xD3DBEC);
    c[ImGuiCol_TableHeaderBg]     = Hex(0xEEF2FA);
    c[ImGuiCol_TableRowBg]        = Hex(0xFFFFFF, 0);
    c[ImGuiCol_TableRowBgAlt]     = Hex(0xF5F8FD);
    c[ImGuiCol_TableBorderLight]  = Hex(0xE8ECF5);
    c[ImGuiCol_TableBorderStrong] = Hex(0xE8ECF5);
    c[ImGuiCol_TextSelectedBg]    = Hex(kAccent, 0.25f);
}

bool PrimaryButton(const char* label, bool small)
{
    ImGui::PushStyleColor(ImGuiCol_Text, Hex(0xFFFFFF));
    ImGui::PushStyleColor(ImGuiCol_Button, Hex(kAccent));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Hex(0x5A7ED3));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Hex(0x3A5AA8));
    if (small) ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 4));
    bool pressed = small ? ImGui::SmallButton(label) : ImGui::Button(label);
    if (small) ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return pressed;
}

bool DangerButton(const char* label)
{
    ImGui::PushStyleColor(ImGuiCol_Text, Hex(kError));
    ImGui::PushStyleColor(ImGuiCol_Button, Hex(0xFBEAEA));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Hex(0xF6DADA));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Hex(0xF0C8C8));
    bool pressed = ImGui::Button(label);
    ImGui::PopStyleColor(4);
    return pressed;
}
}
