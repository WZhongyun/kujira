#pragma once

#include <map>
#include <string>
#include <vector>

#include <CubismFramework.hpp>
#include <ICubismModelSetting.hpp>
#include <Model/CubismUserModel.hpp>
#include <Motion/CubismMotionManager.hpp>

#include "core/FileUtil.h"

// Wraps the official Framework: loading, motions, expressions, look-at, drawing.
// Unlike the SDK samples it does not rely on model3.json listing motions and
// expressions: it also picks up every *.motion3.json / *.exp3.json in the folder
// (many VTube Studio models only list them in their .vtube.json).
class PetModel : public Csm::CubismUserModel
{
public:
    PetModel();
    ~PetModel() override;

    // Loads the first *.model3.json found in `dir`. Needs a current GL context.
    bool Load(const fs::path& dir, std::string* error);

    void Update(float deltaSeconds);
    // Draws the model fitted to the framebuffer (bounds computed on the first frames).
    // Draws the model fitted into a rectangle of the framebuffer (pixels, origin
    // bottom-left); an empty rectangle means the whole framebuffer.
    void Draw(int framebufferWidth, int framebufferHeight, float rectX = 0, float rectY = 0, float rectW = 0, float rectH = 0);

    // -1..1 in both axes, where the model should look.
    void LookAt(float x, float y);
    // Expressions to show, stacked like toggles in VTube Studio. Others fade out;
    // unknown names are ignored. An empty list fades back to the neutral face.
    void SetExpressions(const std::vector<std::string>& names);
    // Plays a motion on a layer above the idle motion, so idle keeps animating the
    // parameters the motion leaves alone. Returns false if unknown.
    bool PlayMotion(const std::string& name, bool loop = false);
    // Fades out the motion started by PlayMotion.
    void StopMotion();
    bool IsMotionPlaying() const { return _stateMotions && !_stateMotions->IsFinished(); }
    void SetIdleMotion(const std::string& name);

    const std::vector<std::string>& ExpressionNames() const { return _expressionNames; }
    const std::vector<std::string>& MotionNames() const { return _motionNames; }
    // Name the model's author gave an expression or motion (its VTube Studio hotkey),
    // or the file name itself.
    const std::string& DisplayName(const std::string& name) const;
    const fs::path& Directory() const { return _dir; }

    // Width / height of the visible model area, available after the first Draw.
    float Aspect() const { return _boundsReady ? (_bounds[2] - _bounds[0]) / (_bounds[3] - _bounds[1]) : 1.0f; }
    bool BoundsReady() const { return _boundsReady; }

private:
    struct Expression
    {
        struct Param
        {
            Csm::csmInt32 index;
            int blend;  // 0 add, 1 multiply, 2 overwrite
            float value;
        };
        std::vector<Param> params;
        float weight = 0;  // current fade 0..1
        bool on = false;
    };
    class ExpressionUpdater;

    void ApplyExpressions(Csm::CubismModel* model, float dt);
    void LoadExpressions();
    void LoadMotions();
    void LoadDisplayNames();
    void SetupTextures();
    void MeasureBounds();

    fs::path _dir;
    Csm::ICubismModelSetting* _setting = nullptr;
    std::map<std::string, Expression> _expressions;
    std::map<std::string, Csm::ACubismMotion*> _motions;
    Csm::CubismMotionManager* _stateMotions = nullptr;  // layer above the idle motion
    Csm::CubismMotionQueueEntryHandle _stateMotion = nullptr;
    std::vector<std::string> _expressionNames;
    std::vector<std::string> _motionNames;
    std::map<std::string, std::string> _displayNames;
    std::string _idleMotion;
    std::vector<unsigned int> _textures;
    Csm::csmVector<Csm::CubismIdHandle> _eyeBlinkIds;
    Csm::csmVector<Csm::CubismIdHandle> _lipSyncIds;
    bool _motionUpdated = false;

    // Visible area in model-matrix space: minX, minY, maxX, maxY.
    float _bounds[4] = { -1, -1, 1, 1 };
    bool _boundsReady = false;
    int _framesSeen = 0;
};
