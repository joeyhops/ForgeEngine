#pragma once
#include <forge/Application.h>
#include <forge/Animator.h>
#include <forge/AnimationClip.h>
#include <forge/AssetManager.h>
#include <forge/Renderer.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

class TaeEditorUI;

class TaeEditorApp : public forge::Application {
public:
  TaeEditorApp(int w, int h, const char* title);
  ~TaeEditorApp();

  void setStartupPaths(const std::string& charPath, const std::string& clipPath) {
    m_startupCharPath = charPath;
    m_startupClipPath = clipPath;
  }

  // Accessors used by UI
  forge::AnimationClip* clip() { return m_clip.get(); }
  forge::Animator& animator() { return m_animator; }
  std::vector<forge::AnimEvent>& events() { return m_editedEvents; }
  const std::vector<forge::Bone>& skeleton() { return m_skeleton; }
  float scrubTime() const { return m_scrubTime; }
  int& selectedEvent() { return m_selectedEvent; }
  const std::vector<std::string>& knownBones() const { return m_knownBones; }
  const std::vector<std::string>& knownComboKeys() const { return m_knownComboKeys; }
  bool isLoaded() const { return m_clip != nullptr; }
  bool isPlaying() const { return m_playing; }
  void togglePlaying() { m_playing = !m_playing; }
  const std::string& charPath() const { return m_charPath; }
  const std::string& clipPath() const { return m_clipPath; }
  const std::string& activeMovesetId() const { return m_activeMovesetId; }
  const std::string& activeActionKey() const { return m_activeActionKey; }

  void setScrubTime(float t);
  void loadClipAndSkeleton(const std::string& charPath,
                           const std::string& clipPath);

  void loadMovesetAction(const std::string& charPath,
                         const std::string& movesetId,
                         const std::string& actionKey);

  std::string toSidecarPath(const std::string& clipRelPath) const;
  void saveToSidecar();

protected:
  void onInit() override;
  void onUpdate(float dt) override;
  void onRender() override;
  void onShutdown() override;

private:
  std::shared_ptr<forge::AnimationClip> m_clip;
  std::vector<forge::Bone> m_skeleton;
  forge::Animator m_animator;
  std::vector<forge::AnimEvent> m_editedEvents; // Working copy
  int m_selectedEvent = -1;

  // Playback
  float m_scrubTime = 0.0f;
  bool m_playing = false;

  // File paths
  std::string m_sidecarPath;
  std::string m_charPath;
  std::string m_clipPath;
  std::string m_activeMovesetId;
  std::string m_activeActionKey;
  std::string m_startupCharPath;
  std::string m_startupClipPath;

  // Payload validation
  std::vector<std::string> m_knownBones; // Boneattach from weapons.json
  std::vector<std::string> m_knownComboKeys; // clip keys from all movesets

  // Orbit camera
  struct OrbitCamera {
    float azimuth = 0.0f;// degrees
    float elevation = 15.0f; // degrees above horizon
    float radius = 3.0f;
    glm::vec3 target = { 0.0f, 0.9f, 0.0f }; // approx chest height

    glm::mat4 viewMatrix() const;
    glm::mat4 projMatrix(float aspect) const;
    void handleMouse(float dx, float dy, float scroll);
  } m_camera;
  
  // UI
  std::unique_ptr<TaeEditorUI> m_ui;
  char m_charPathBuff[1024] = {};
  char m_clipPathBuff[1024] = {};

  void renderSkeleton();
  void renderActiveHitboxes();
  void handleCameraInput();
  void loadPayloadValidationData();

};
