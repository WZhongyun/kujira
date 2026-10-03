// Based on the Live2D Cubism SDK sample model class (LAppModel), which is
// Copyright(c) Live2D Inc. and governed by the Live2D Open Software license:
// https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html
#include "core/PetModel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#include <GL/glew.h>

#include <CubismDefaultParameterId.hpp>
#include <CubismModelSettingJson.hpp>
#include <Id/CubismIdManager.hpp>
#include <Motion/CubismBreathUpdater.hpp>
#include <Motion/CubismEyeBlinkUpdater.hpp>
#include <Motion/CubismLookUpdater.hpp>
#include <Motion/CubismMotion.hpp>
#include <Motion/CubismMotionQueueEntry.hpp>
#include <Motion/CubismPhysicsUpdater.hpp>
#include <Motion/CubismPoseUpdater.hpp>
#include <Rendering/OpenGL/CubismOffscreenManager_OpenGLES2.hpp>
#include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>

#include <nlohmann/json.hpp>

#include "stb_image.h"

using namespace Live2D::Cubism::Framework;
using namespace Live2D::Cubism::Framework::DefaultParameterId;

namespace
{
constexpr csmInt32 kPriorityIdle = 1;
constexpr csmInt32 kPriorityNormal = 2;
// Expression fade, close to the 0.1-0.5 s the model's VTube Studio hotkeys use.
constexpr float kExpressionFadeSeconds = 0.3f;

std::vector<csmByte> ReadBytes(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const std::streamsize size = in.tellg();
    std::vector<csmByte> data(static_cast<size_t>(std::max<std::streamsize>(size, 0)));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// "name.exp3.json" -> "name"
std::string StripSuffix(const fs::path& file, const std::string& suffix)
{
    std::string name = FileUtil::ToUtf8(file.filename());
    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
    {
        name.resize(name.size() - suffix.size());
    }
    return name;
}

bool EndsWith(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Files with the given suffix in dir and its direct subfolders, sorted.
std::vector<fs::path> FindFiles(const fs::path& dir, const std::string& suffix)
{
    std::vector<fs::path> out;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
    {
        if (it.depth() > 1)
        {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec) && EndsWith(FileUtil::ToUtf8(it->path().filename()), suffix))
        {
            out.push_back(it->path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}
}

// Applies the stacked expressions at the point of the update where the SDK applies
// its own expressions: after eye blink, before look-at, breath and physics.
class PetModel::ExpressionUpdater : public ICubismUpdater
{
public:
    explicit ExpressionUpdater(PetModel& owner) : ICubismUpdater(CubismUpdateOrder_Expression), _owner(owner) {}
    void OnLateUpdate(CubismModel* model, csmFloat32 dt) override { _owner.ApplyExpressions(model, dt); }

private:
    PetModel& _owner;
};

PetModel::PetModel() = default;

PetModel::~PetModel()
{
    // The state layer only holds motions we own (autoDelete off); it must go before them.
    CSM_DELETE(_stateMotions);
    for (auto& [name, motion] : _motions)
    {
        ACubismMotion::Delete(motion);
    }
    if (!_textures.empty())
    {
        glDeleteTextures(static_cast<GLsizei>(_textures.size()), _textures.data());
    }
    delete _setting;
}

bool PetModel::Load(const fs::path& dir, std::string* error)
{
    _dir = dir;
    auto settings = FindFiles(dir, ".model3.json");
    if (settings.empty())
    {
        if (error) *error = "文件夹里没有 .model3.json：" + FileUtil::ToUtf8(dir);
        return false;
    }
    auto settingBytes = ReadBytes(settings.front());
    _setting = new CubismModelSettingJson(settingBytes.data(), static_cast<csmSizeInt>(settingBytes.size()));
    const fs::path home = settings.front().parent_path();

    if (strcmp(_setting->GetModelFileName(), "") == 0)
    {
        if (error) *error = "model3.json 没有指定 moc3 文件";
        return false;
    }
    auto moc = ReadBytes(home / FileUtil::FromUtf8(_setting->GetModelFileName()));
    if (moc.empty())
    {
        if (error) *error = "无法读取 moc3 文件";
        return false;
    }
    LoadModel(moc.data(), static_cast<csmSizeInt>(moc.size()), true);
    if (!_model)
    {
        if (error) *error = "moc3 加载失败（文件损坏或 SDK 版本过旧）";
        return false;
    }
    _dir = home;

    LoadExpressions();

    if (strcmp(_setting->GetPhysicsFileName(), "") != 0)
    {
        auto bytes = ReadBytes(home / FileUtil::FromUtf8(_setting->GetPhysicsFileName()));
        if (!bytes.empty())
        {
            LoadPhysics(bytes.data(), static_cast<csmSizeInt>(bytes.size()));
            if (_physics) _updateScheduler.AddUpdatableList(CSM_NEW CubismPhysicsUpdater(*_physics));
        }
    }
    if (strcmp(_setting->GetPoseFileName(), "") != 0)
    {
        auto bytes = ReadBytes(home / FileUtil::FromUtf8(_setting->GetPoseFileName()));
        if (!bytes.empty())
        {
            LoadPose(bytes.data(), static_cast<csmSizeInt>(bytes.size()));
            if (_pose) _updateScheduler.AddUpdatableList(CSM_NEW CubismPoseUpdater(*_pose));
        }
    }

    for (csmInt32 i = 0; i < _setting->GetEyeBlinkParameterCount(); ++i)
    {
        _eyeBlinkIds.PushBack(_setting->GetEyeBlinkParameterId(i));
    }
    for (csmInt32 i = 0; i < _setting->GetLipSyncParameterCount(); ++i)
    {
        _lipSyncIds.PushBack(_setting->GetLipSyncParameterId(i));
    }
    if (_setting->GetEyeBlinkParameterCount() > 0)
    {
        _eyeBlink = CubismEyeBlink::Create(_setting);
        _updateScheduler.AddUpdatableList(CSM_NEW CubismEyeBlinkUpdater(_motionUpdated, *_eyeBlink));
    }

    CubismIdManager* ids = CubismFramework::GetIdManager();
    const CubismIdHandle angleX = ids->GetId(ParamAngleX);
    const CubismIdHandle angleY = ids->GetId(ParamAngleY);
    const CubismIdHandle angleZ = ids->GetId(ParamAngleZ);
    const CubismIdHandle bodyX = ids->GetId(ParamBodyAngleX);

    _breath = CubismBreath::Create();
    csmVector<CubismBreath::BreathParameterData> breath;
    breath.PushBack(CubismBreath::BreathParameterData(angleX, 0.0f, 15.0f, 6.5345f, 0.5f));
    breath.PushBack(CubismBreath::BreathParameterData(angleY, 0.0f, 8.0f, 3.5345f, 0.5f));
    breath.PushBack(CubismBreath::BreathParameterData(angleZ, 0.0f, 10.0f, 5.5345f, 0.5f));
    breath.PushBack(CubismBreath::BreathParameterData(bodyX, 0.0f, 4.0f, 15.5345f, 0.5f));
    breath.PushBack(CubismBreath::BreathParameterData(ids->GetId(ParamBreath), 0.5f, 0.5f, 3.2345f, 0.5f));
    _breath->SetParameters(breath);
    _updateScheduler.AddUpdatableList(CSM_NEW CubismBreathUpdater(*_breath));

    _look = CubismLook::Create();
    csmVector<CubismLook::LookParameterData> look;
    look.PushBack(CubismLook::LookParameterData(angleX, 30.0f));
    look.PushBack(CubismLook::LookParameterData(angleY, 0.0f, 30.0f));
    look.PushBack(CubismLook::LookParameterData(angleZ, 0.0f, 0.0f, -30.0f));
    look.PushBack(CubismLook::LookParameterData(bodyX, 10.0f));
    look.PushBack(CubismLook::LookParameterData(ids->GetId(ParamEyeBallX), 1.0f));
    look.PushBack(CubismLook::LookParameterData(ids->GetId(ParamEyeBallY), 0.0f, 1.0f));
    _look->SetParameters(look);
    _updateScheduler.AddUpdatableList(CSM_NEW CubismLookUpdater(*_look, *_dragManager));

    _updateScheduler.SortUpdatableList();

    csmMap<csmString, csmFloat32> layout;
    _setting->GetLayoutMap(layout);
    _modelMatrix->SetupFromLayout(layout);
    _model->SaveParameters();

    LoadMotions();
    LoadDisplayNames();
    _stateMotions = CSM_NEW CubismMotionManager();

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    CreateRenderer(std::max(viewport[2], 1), std::max(viewport[3], 1));
    SetupTextures();
    return true;
}

void PetModel::LoadExpressions()
{
    std::map<std::string, fs::path> files;
    for (csmInt32 i = 0; i < _setting->GetExpressionCount(); ++i)
    {
        files[_setting->GetExpressionName(i)] = _dir / FileUtil::FromUtf8(_setting->GetExpressionFileName(i));
    }
    for (const auto& path : FindFiles(_dir, ".exp3.json"))
    {
        files.emplace(StripSuffix(path, ".exp3.json"), path);
    }
    CubismIdManager* ids = CubismFramework::GetIdManager();
    for (const auto& [name, path] : files)
    {
        auto text = FileUtil::ReadText(path);
        auto j = nlohmann::json::parse(text ? *text : "", nullptr, false, true);
        if (!j.is_object() || !j.contains("Parameters") || !j["Parameters"].is_array()) continue;
        Expression e;
        for (const auto& p : j["Parameters"])
        {
            if (!p.is_object() || !p.contains("Id") || !p["Id"].is_string() || !p.contains("Value") || !p["Value"].is_number()) continue;
            const std::string blend = p.value("Blend", std::string("Add"));
            const csmInt32 index = _model->GetParameterIndex(ids->GetId(p["Id"].get<std::string>().c_str()));
            e.params.push_back({ index, blend == "Multiply" ? 1 : blend == "Overwrite" ? 2 : 0, p["Value"].get<float>() });
        }
        _expressions[name] = std::move(e);
        _expressionNames.push_back(name);
    }
    _updateScheduler.AddUpdatableList(CSM_NEW ExpressionUpdater(*this));
}

void PetModel::SetExpressions(const std::vector<std::string>& names)
{
    for (auto& [name, e] : _expressions)
    {
        e.on = std::find(names.begin(), names.end(), name) != names.end();
    }
}

void PetModel::ApplyExpressions(CubismModel* model, float dt)
{
    const float step = dt / kExpressionFadeSeconds;
    for (auto& [name, e] : _expressions)
    {
        e.weight = e.on ? std::min(1.0f, e.weight + step) : std::max(0.0f, e.weight - step);
        if (e.weight <= 0) continue;
        // Ease in and out like the SDK's expression fades.
        const float w = 0.5f - 0.5f * std::cos(e.weight * 3.14159265f);
        for (const auto& p : e.params)
        {
            switch (p.blend)
            {
            case 0: model->AddParameterValue(p.index, p.value, w); break;
            case 1: model->MultiplyParameterValue(p.index, p.value, w); break;
            default: model->SetParameterValue(p.index, p.value, w); break;
            }
        }
    }
}

void PetModel::LoadMotions()
{
    for (const auto& path : FindFiles(_dir, ".motion3.json"))
    {
        const std::string name = StripSuffix(path, ".motion3.json");
        if (_motions.count(name)) continue;
        auto bytes = ReadBytes(path);
        if (bytes.empty()) continue;
        auto* motion = static_cast<CubismMotion*>(LoadMotion(bytes.data(), static_cast<csmSizeInt>(bytes.size()), name.c_str()));
        if (!motion) continue;
        motion->SetEffectIds(_eyeBlinkIds, _lipSyncIds);
        // Soft 0.3 s fades, but never longer than a quarter of the motion: the water
        // spout lasts under half a second and would otherwise never show fully.
        const float fade = std::min(0.3f, motion->GetDuration() * 0.25f);
        motion->SetFadeInTime(fade);
        motion->SetFadeOutTime(fade);
        motion->SetLoopFadeIn(false);
        _motions[name] = motion;
        _motionNames.push_back(name);
    }
}

void PetModel::SetupTextures()
{
    for (csmInt32 i = 0; i < _setting->GetTextureCount(); ++i)
    {
        if (strcmp(_setting->GetTextureFileName(i), "") == 0) continue;
        auto bytes = ReadBytes(_dir / FileUtil::FromUtf8(_setting->GetTextureFileName(i)));
        int w = 0, h = 0, channels = 0;
        unsigned char* png = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, STBI_rgb_alpha);
        if (!png) continue;
        // Premultiply so the transparent window composites without dark or light fringes.
        for (int p = 0; p < w * h; ++p)
        {
            unsigned char* px = png + p * 4;
            const unsigned a = px[3];
            px[0] = static_cast<unsigned char>((px[0] * a + 127) / 255);
            px[1] = static_cast<unsigned char>((px[1] * a + 127) / 255);
            px[2] = static_cast<unsigned char>((px[2] * a + 127) / 255);
        }
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, png);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(png);
        _textures.push_back(tex);
        GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->BindTexture(i, tex);
    }
    GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->IsPremultipliedAlpha(true);
}

void PetModel::SetIdleMotion(const std::string& name)
{
    _idleMotion = name;
}

void PetModel::LoadDisplayNames()
{
    for (const auto& path : FindFiles(_dir, ".vtube.json"))
    {
        auto text = FileUtil::ReadText(path);
        auto j = nlohmann::json::parse(text ? *text : "", nullptr, false, true);
        if (!j.is_object() || !j.contains("Hotkeys") || !j["Hotkeys"].is_array()) continue;
        for (const auto& h : j["Hotkeys"])
        {
            if (!h.is_object()) continue;
            std::string file = h.value("File", std::string());
            const std::string name = h.value("Name", std::string());
            file = FileUtil::ToUtf8(FileUtil::FromUtf8(file).filename());
            for (const char* suffix : { ".exp3.json", ".motion3.json" })
            {
                if (EndsWith(file, suffix)) file.resize(file.size() - std::strlen(suffix));
            }
            if (!file.empty() && !name.empty() && name != file) _displayNames.emplace(file, name);
        }
    }
}

const std::string& PetModel::DisplayName(const std::string& name) const
{
    auto it = _displayNames.find(name);
    return it != _displayNames.end() ? it->second : name;
}

void PetModel::Update(float dt)
{
    _motionUpdated = false;
    _model->LoadParameters();
    if (_motionManager->IsFinished())
    {
        auto it = _motions.find(_idleMotion);
        if (it != _motions.end())
        {
            it->second->SetLoop(true);
            _motionManager->StartMotionPriority(it->second, false, kPriorityIdle);
        }
    }
    else
    {
        _motionUpdated = _motionManager->UpdateMotion(_model, dt);
    }
    _model->SaveParameters();
    if (_stateMotions && !_stateMotions->IsFinished())
    {
        // Parameters the state motion animates win over idle; the rest keep idling.
        // Applied after SaveParameters so nothing it sets (bubble gum, the omelette)
        // stays behind once it stops, the way VTube Studio resets after an animation.
        _motionUpdated |= _stateMotions->UpdateMotion(_model, dt);
    }
    _updateScheduler.OnLateUpdate(_model, dt);
    _model->Update();
}

void PetModel::LookAt(float x, float y)
{
    SetDragging(x, y);
}

bool PetModel::PlayMotion(const std::string& name, bool loop)
{
    auto it = _motions.find(name);
    if (it == _motions.end() || !_stateMotions) return false;
    it->second->SetLoop(loop);
    _stateMotions->SetReservePriority(kPriorityNormal);
    _stateMotion = _stateMotions->StartMotionPriority(it->second, false, kPriorityNormal);
    return true;
}

void PetModel::StopMotion()
{
    if (!_stateMotions || _stateMotions->IsFinished()) return;
    if (CubismMotionQueueEntry* entry = _stateMotions->GetCubismMotionQueueEntry(_stateMotion))
    {
        ACubismMotion* motion = entry->GetCubismMotion();
        entry->SetFadeout(motion ? std::max(motion->GetFadeOutTime(), 0.1f) : 0.3f);
    }
}

void PetModel::MeasureBounds()
{
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    CubismModelMatrix* m = GetModelMatrix();
    for (csmInt32 d = 0; d < _model->GetDrawableCount(); ++d)
    {
        if (!_model->GetDrawableDynamicFlagIsVisible(d) || _model->GetDrawableOpacity(d) < 0.01f) continue;
        const csmInt32 count = _model->GetDrawableVertexCount(d);
        const auto* v = _model->GetDrawableVertexPositions(d);
        for (csmInt32 i = 0; i < count; ++i)
        {
            const float x = m->TransformX(v[i].X);
            const float y = m->TransformY(v[i].Y);
            minX = std::min(minX, x); maxX = std::max(maxX, x);
            minY = std::min(minY, y); maxY = std::max(maxY, y);
        }
    }
    if (maxX <= minX || maxY <= minY) return;
    if (_framesSeen > 1)
    {
        minX = std::min(minX, _bounds[0]); minY = std::min(minY, _bounds[1]);
        maxX = std::max(maxX, _bounds[2]); maxY = std::max(maxY, _bounds[3]);
    }
    _bounds[0] = minX; _bounds[1] = minY; _bounds[2] = maxX; _bounds[3] = maxY;
}

void PetModel::Draw(int fbWidth, int fbHeight, float rectX, float rectY, float rectW, float rectH)
{
    if (!_model || fbWidth <= 0 || fbHeight <= 0) return;

    // Measure the visible area over the first second of animation, then freeze it.
    constexpr int kMeasureFrames = 45;
    if (_framesSeen < kMeasureFrames)
    {
        ++_framesSeen;
        MeasureBounds();
        if (_framesSeen == kMeasureFrames)
        {
            // Headroom for motions and props that were not visible while measuring.
            const float padX = (_bounds[2] - _bounds[0]) * 0.06f;
            const float padY = (_bounds[3] - _bounds[1]) * 0.06f;
            _bounds[0] -= padX; _bounds[2] += padX;
            _bounds[1] -= padY; _bounds[3] += padY;
            _boundsReady = true;
        }
    }

    const float bw = _bounds[2] - _bounds[0];
    const float bh = _bounds[3] - _bounds[1];
    const float cx = (_bounds[0] + _bounds[2]) * 0.5f;
    const float cy = (_bounds[1] + _bounds[3]) * 0.5f;
    // Fit the bounds into the target rectangle (framebuffer pixels, origin bottom-left).
    if (rectW <= 0 || rectH <= 0)
    {
        rectX = 0;
        rectY = 0;
        rectW = static_cast<float>(fbWidth);
        rectH = static_cast<float>(fbHeight);
    }
    const float pixelsPerUnit = std::min(rectW / bw, rectH / bh);
    const float sx = pixelsPerUnit / (fbWidth * 0.5f);
    const float sy = pixelsPerUnit / (fbHeight * 0.5f);
    const float ndcX = (rectX + rectW * 0.5f) / fbWidth * 2.0f - 1.0f;
    const float ndcY = (rectY + rectH * 0.5f) / fbHeight * 2.0f - 1.0f;

    float tr[16] = { sx, 0, 0, 0,
                     0, sy, 0, 0,
                     0, 0, 1, 0,
                     ndcX - cx * sx, ndcY - cy * sy, 0, 1 };
    CubismMatrix44 projection;
    projection.SetMatrix(tr);
    projection.MultiplyByMatrix(_modelMatrix);

    auto* offscreen = Rendering::CubismOffscreenManager_OpenGLES2::GetInstance();
    offscreen->BeginFrameProcess();
    auto* renderer = GetRenderer<Rendering::CubismRenderer_OpenGLES2>();
    renderer->SetMvpMatrix(&projection);
    renderer->DrawModel();
    offscreen->EndFrameProcess();
    offscreen->ReleaseStaleRenderTextures();
}
