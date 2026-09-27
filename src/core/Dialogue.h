#pragma once

#include <map>
#include <random>
#include <string>
#include <vector>

#include "core/FileUtil.h"

// Lines she says in the speech bubble, grouped by occasion. Every group ships
// with presets and can be edited in the settings window; edits are stored in
// dialogue.json next to config.json.
class Dialogue
{
public:
    struct Category
    {
        const char* key;
        const char* label;    // shown in the settings window
        const char* hint;     // when the line is used
        std::vector<std::string> defaults;
    };

    static const std::vector<Category>& Categories();
    static const Category* FindCategory(const std::string& key);

    static fs::path FilePath();
    static Dialogue Load();
    bool Save() const;

    const std::vector<std::string>& Lines(const std::string& key) const;
    void SetLines(const std::string& key, std::vector<std::string> lines);
    void ResetToDefault(const std::string& key);

    // Random line from a category, avoiding the one picked last time.
    // "{target}" is replaced with `target`. Empty if the category has no lines.
    std::string Pick(const std::string& key, const std::string& target = {});

    // Time-of-day category for local hour 0-23: morning, noon, afternoon, evening, night.
    static const char* TimeOfDayKey(int hour);

private:
    std::map<std::string, std::vector<std::string>> _lines;
    std::map<std::string, std::string> _last;
    std::mt19937 _rng{ std::random_device{}() };
};
