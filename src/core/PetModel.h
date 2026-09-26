#pragma once

#include <map>
#include <string>
#include <vector>

#include <CubismFramework.hpp>
#include <ICubismModelSetting.hpp>
#include <Model/CubismUserModel.hpp>

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
    void Draw(int framebufferWidth, int framebufferHeight);

    // -1..1 in both axes, where the model should look.
    void LookAt(float x, float y);
    // "" fades back to the neutral face.
    void SetExpressionByName(const std::string& name);
    // Plays a motion once over the idle loop. Returns false if unknown.
    bool PlayMotion(const std::string& name);
    void SetIdleMotion(const std::string& name);

    const std::vector<std::string>& ExpressionNames() const { return _expressionNames; }
    const std::vector<std::string>& MotionNames() const { return _motionNames; }
    const fs::path& Directory() const { return _dir; }

    // Width / height of the visible model area, available after the first Draw.
    float Aspect() const { return _boundsReady ? (_bounds[2] - _bounds[0]) / (_bounds[3] - _bounds[1]) : 1.0f; }
    bool BoundsReady() const { return _boundsReady; }

private:
    void LoadExpressions();
    void LoadMotions();
    void SetupTextures();
    void MeasureBounds();

    fs::path _dir;
    Csm::ICubismModelSetting* _setting = nullptr;
    Csm::csmMap<Csm::csmString, Csm::ACubismMotion*> _expressions;
    std::map<std::string, Csm::ACubismMotion*> _motions;
    std::vector<std::string> _expressionNames;
    std::vector<std::string> _motionNames;
    Csm::ACubismMotion* _neutralExpression = nullptr;
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
