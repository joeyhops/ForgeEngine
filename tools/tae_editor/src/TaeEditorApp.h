#pragma once
#include <forge/Application.h>
#include <forge/Animator.h>
#include <forge/AnimationClip.h>
#include <forge/AssetManager.h>
#include <forge/Camera.h>
#include <forge/Renderer.h>
#include <forge/WeaponDef.h>
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

  void setStartupChar(const std::string& charPath);

  const std::string& animDir() const { return m_animDir; }
  void setAnimDir(const std::string& dir);

  // Accessors used by UI
  forge::AnimationClip* clip() { return m_clip.get(); }
  forge::Animator& animator() { return m_animator; }
  std::vector<forge::AnimEvent>& events() { return m_editedEvents; }
  std::vector<forge::SyncEvent>& editedSync() { return m_editedSync; }
  const std::vector<forge::Bone>& skeleton() { return m_skeleton; }
  float modelUnitScale() const { return m_modelUnitScale; }
  void setModelUnitScale(float s) { m_modelUnitScale = s; saveConfig(); }
  float scrubTime() const { return m_scrubTime; }
  int& selectedEvent() { return m_selectedEvent; }
  int& selectedSync() { return m_selectedSync; }
  const std::vector<std::string>& knownBones() const { return m_knownBones; }
  const std::vector<std::string>& knownComboKeys() const { return m_knownComboKeys; }
  bool isLoaded() const { return m_clip != nullptr; }
  bool hasModel() const { return m_skinnedModel.valid(); }
  bool isPlaying() const { return m_playing; }
  void togglePlaying() { m_playing = !m_playing; }
  bool showSkeletonOverlay() const { return m_showSkeletonOverlay; }
  void setShowSkeletonOverlay(bool v) { m_showSkeletonOverlay = v; }

  const std::string& charPath() const { return m_charPath; }
  const std::string& clipPath() const { return m_clipPath; }
  const std::string& activeMovesetId() const { return m_activeMovesetId; }
  const std::string& activeActionKey() const { return m_activeActionKey; }

  struct MovesetEntry {
    std::string actionKey;
    std::string clipPath;
  };

  struct WeaponOffsetEdit {
    glm::vec3 pos = { 0.0f, 0.0f, 0.0f };
    glm::vec3 euler = {0.0f, 0.0f, 0.0f};
    glm::vec3 scale = {0.0f, 0.0f, 0.0f};
    bool dirty = false;
  };

  std::vector<MovesetEntry>& editableMoveset() { return m_editableMoveset; }
  const std::string& editableMovesetId() const { return m_editableMovesetId; }
  const std::string& editableMovesetOutputPath() const { return m_editableMovesetOutputPath; }
  void setEditableMovesetOutputPath(const std::string& p) { m_editableMovesetOutputPath = p; }
  bool hasEditableMoveset() const { return !m_editableMovesetId.empty(); }
  bool isEditableMovesetDirty() const { return m_editableMovesetDirty; }

  void loadModel(const std::string& charPath);
  void loadClipFromPath(const std::string& clipPath, const std::string& overrideKey = "");

  void loadClipAndSkeleton(const std::string& charPath,
                           const std::string& clipPath);

  void loadMovesetAction(const std::string& charPath,
                         const std::string& movesetId,
                         const std::string& actionKey);

  void newMoveset(const std::string& id, const std::string& outputPath);
  void loadMovesetForEditing(const std::string& movesetId);
  void addEntryToMoveset(const std::string& actionKey, const std::string& clipPath);
  void removeEntryFromMoveset(int idx);
  void renameEntryKey(int idx, const std::string& newKey);

  void saveMovesetToJson(const std::string& outputPath = "");

  void loadWeapon(const std::string& weaponId);
  void clearWeapon();
  const forge::WeaponDef* getActiveWeapon() const { return m_weaponDef; }
  int getWeaponBoneIndex() const { return m_weaponBoneIndex; }
  WeaponOffsetEdit& getOffsetEdit() { return m_offsetEdit; }
  void resetWeaponOffset();
  void saveWeaponOffset();

  void setScrubTime(float t);

  std::string toSidecarPath(const std::string& clipRelPath) const;
  void saveToSidecar();

protected:
  void onInit() override;
  void onUpdate(float dt) override;
  void onRender() override;
  void onShutdown() override;

private:
  forge::SkinnedModelData m_skinnedModel;
  std::vector<forge::Bone> m_skeleton;
  std::shared_ptr<forge::AnimationClip> m_clip;
  forge::Animator m_animator;
  std::vector<forge::AnimEvent> m_editedEvents; // Working copy
  int m_selectedEvent = -1;

  std::vector<forge::SyncEvent> m_editedSync; // working copy of clip->syncTrack.events
  int m_selectedSync = -1;

  // Playback
  float m_scrubTime = 0.0f;
  bool m_playing = false;
  bool m_showSkeletonOverlay = false;

  // File paths
  std::string m_sidecarPath;
  std::string m_charPath;
  std::string m_clipPath;
  std::string m_animDir;
  std::string m_activeMovesetId;
  std::string m_activeActionKey;
  std::string m_startupCharPath;
  std::string m_startupClipPath;
  float m_modelUnitScale = 1.0f;

  // weapon preview
  forge::ModelData m_weaponModel;
  const forge::WeaponDef* m_weaponDef = nullptr;
  int m_weaponBoneIndex = -1;
  WeaponOffsetEdit m_offsetEdit;

  // Payload validation
  std::vector<std::string> m_knownBones; // Boneattach from weapons.json
  std::vector<std::string> m_knownComboKeys; // clip keys from all movesets

  std::string m_editableMovesetId;
  std::string m_editableMovesetOutputPath;
  std::vector<MovesetEntry> m_editableMoveset;
  bool m_editableMovesetDirty = false;

  // Orbit camera
  struct OrbitCamera {
    float azimuth = 0.0f;// degrees
    float elevation = 15.0f; // degrees above horizon
    float radius = 3.0f;
    glm::vec3 target = { 0.0f, 0.9f, 0.0f }; // approx chest height
    
    forge::Camera toForgeCamera(float aspect) const;

    glm::mat4 viewMatrix() const;
    glm::mat4 projMatrix(float aspect) const;
    void handleMouse(float dx, float dy, float scroll);
  } m_camera;
  
  // UI
  std::unique_ptr<TaeEditorUI> m_ui;
  char m_charPathBuff[1024] = {};
  char m_clipPathBuff[1024] = {};

  // Camera input state
  double m_prevMouseX = 0.0;
  double m_prevMouseY = 0.0;
  bool m_orbiting = false;

  void renderSkinnedModel();
  void renderWeapon();
  void renderSkeleton();
  void renderActiveHitboxes();
  void handleCameraInput();
  void loadPayloadValidationData();
  void resolveWeaponBone();

  glm::mat4 buildOffsetMatrix() const;
  void decomposeOffset(const glm::mat4& m);

  void loadConfig();
  void saveConfig();

};
