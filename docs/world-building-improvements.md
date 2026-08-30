# World Building & Level Systems — Design Investigation

*ForgeEngine — Post-Refactor Planning Document*

---

## Executive Summary

The brush-based authoring workflow via TrenchBroom is more capable than it looks, and
abandoning it prematurely would be a mistake. Dark Souls 1 — arguably the gold standard
of interconnected world design — was built with geometry not fundamentally different from
what Q2 .map brushwork produces. The bottleneck is not the authoring format. It is the
**runtime systems** that currently don't exist: there is no level streaming, no visibility
culling, no LOD, a hard light cap of 8 point lights, no navmesh, and no world-connection
graph. Adding those systems while keeping TrenchBroom as the level editor is the right
call for the near term.

That said, open mountain traversal à la Sekiro and true open-air biomes will eventually
exhaust what brush geometry can do without becoming painful to author. A lightweight
terrain layer on top of the existing system — not replacing it — is the long-term answer
for outdoor zones.

The eight systems below, in rough priority order, are what it actually takes to build a
Souls / Sekiro caliber world in ForgeEngine.

---

## Current State Snapshot

| System | Status |
|---|---|
| Level authoring | TrenchBroom .map brushwork — good |
| Level loading | Single full load; no streaming |
| Frustum culling | None |
| Occlusion culling | None |
| LOD | None |
| Lighting | 8 point lights hard-capped (`LightEnvironment::k_maxPointLights`) |
| Pathfinding | Waypoint chains only; no navmesh |
| Terrain | None; everything is brush geometry |
| World connection | Lua `loadLevel()` swaps the whole scene |
| Persistent state | `FlagManager` + `saves/flags.json` — solid foundation |

---

## System 1 — Zone-Based Level Streaming

### The Problem

`ForgeGame::setupLevel` loads an entire .map file, generates all geometry, uploads all
GPU buffers, and configures all physics in one synchronous call. There is no concept of
adjacent zones or background loading. Transitioning between areas requires unloading
everything and reloading from scratch, which forces a hard cut (or a loading screen).

Dark Souls achieves the illusion of a seamless world through **loading corridors** — narrow
passages, fog gates, and stairwells that are long enough to stream the next zone in the
background before the player arrives. The zones themselves are discrete; the seams are
hidden by geometry.

### Verdict

Zone-based streaming is the correct model for a Souls-like. True open-world distance
streaming (Elden Ring) is a separate problem we don't need to solve yet. The goal is:
multiple named zones can exist simultaneously in memory, transitions between adjacent
zones happen in the background while the player is in a loading corridor, and zones
unload when they are far enough from the player.

### Implementation Plan

**New types:**

```cpp
// engine/include/forge/world/Zone.h
struct Zone {
    std::string id;
    std::string mapPath;
    MapScene    scene;           // render objects
    PhysicsWorld::BodyGroup bodies; // static collision bodies for this zone
    glm::vec3   worldOffset;    // zone's position in world space
    bool        loaded = false;
};

// engine/include/forge/world/ZoneGraph.h
// Loaded from a JSON world manifest at startup
struct ZoneConnection {
    std::string from;
    std::string to;
    glm::vec3   offset; // 'to' origin relative to 'from' origin
};

class ZoneGraph {
public:
    void loadManifest(const std::string& path);
    std::vector<std::string> getAdjacentZones(const std::string& id) const;
    glm::vec3 getRelativeOffset(const std::string& from, const std::string& to) const;
private:
    std::unordered_map<std::string, std::vector<ZoneConnection>> m_adjacency;
};
```

**World manifest (JSON):**

```json
{
  "zones": [
    { "id": "firelink",   "map": "levels/firelink.map",   "offset": [0,0,0] },
    { "id": "undead_burg","map": "levels/undead_burg.map", "offset": [120,0,0] }
  ],
  "connections": [
    { "from": "firelink", "to": "undead_burg", "entry_offset": [5,0,2] }
  ]
}
```

**StreamingManager** lives between `ForgeGame` and the individual zone loaders. It:

1. Holds the active zone and a set of "warm" adjacent zones.
2. When the player enters a **streaming trigger** (a new `stream_trigger` entity in the
   FGD), kicks off a background thread (or async job) to load the target zone into a
   `Zone` object offset by `worldOffset`.
3. Once loaded, merges the zone's render objects and static bodies into the live scene
   without a frame hitch.
4. When the player crosses the actual **zone boundary**, swaps the "active" zone pointer
   and schedules the old zone for eviction after a hold-off delay.
5. Eviction tears down GPU buffers and removes static physics bodies.

**New FGD entities needed:**

- `stream_trigger` — a brush entity that fires the background load for a target zone.
  Properties: `target_zone` (string), `evict_zone` (string, optional).
- `zone_boundary` — a brush entity that commits the active zone swap and repositions
  the world origin if needed.

**Physics note:** Bullet bodies for the old zone must be removed from
`btDiscreteDynamicsWorld` before the zone's memory is freed. `PhysicsWorld` needs a
`removeBodyGroup(id)` method so zone bodies can be batch-removed without iterating the
full world.

**Estimated scope:** Medium-large. Core streaming manager ~400 LOC. New FGD entities
and JSON manifest loader are small. The main complexity is thread safety around physics
body insertion/removal and ensuring GPU uploads happen on the render thread.

---

## System 2 — Frustum Culling

### The Problem

`Renderer::drawBrushScene` submits every `MapRenderObject` in the `MapScene`
unconditionally. No test is performed to check whether an object is inside the camera
frustum. As scene complexity grows, this becomes the dominant render CPU bottleneck.

### Verdict

Frustum culling is table stakes for any 3D game. It should be added before the world
gets significantly larger. The implementation is straightforward given that the `Camera`
already exposes a view-projection matrix.

### Implementation Plan

**Step 1 — Axis-Aligned Bounding Boxes on MapRenderObject**

Extend `MapRenderObject` with a precomputed AABB:

```cpp
struct MapRenderObject {
    std::shared_ptr<Mesh> mesh;
    Material              material;
    glm::vec3             aabbMin;
    glm::vec3             aabbMax;
};
```

`GeometryGenerator` already has all vertex positions at build time; computing the AABB
there is trivial.

**Step 2 — Frustum extraction**

Add a `Frustum` struct to `Camera.h`:

```cpp
struct Frustum {
    glm::vec4 planes[6]; // left, right, bottom, top, near, far (Gribb/Hartmann)
    static Frustum fromViewProjection(const glm::mat4& vp);
    bool intersectsAABB(const glm::vec3& min, const glm::vec3& max) const;
};
```

Gribb/Hartmann plane extraction is six dot products against the VP matrix columns — no
inverse needed, works directly with the cached `m_viewProj` in `Renderer`.

**Step 3 — Cull in drawBrushScene**

```cpp
void Renderer::drawBrushScene(const MapScene& scene) {
    Frustum frustum = Frustum::fromViewProjection(m_viewProj);
    for (const auto& obj : scene.renderObjects) {
        if (!frustum.intersectsAABB(obj.aabbMin, obj.aabbMax)) continue;
        // existing draw call
    }
}
```

Same logic applies to `drawMesh` and `drawSkinnedMesh` — static props should carry
AABBs too.

**Estimated scope:** Small. ~150 LOC total. Zero architectural change.

---

## System 3 — Distance-Based LOD for Static Props

### The Problem

Static props (`static_prop` entities) loaded via `AssetManager::loadModel` render at
full triangle count regardless of distance from the camera. A torch sconce 200m away
draws as many triangles as one 2m away.

### Verdict

Brush geometry is already fairly low-poly by nature, so LOD is less urgent for level
surfaces than for static props and decorations. LOD for props should be implemented once
we have enough props in a level to feel the cost.

### Implementation Plan

**LOD levels convention:**

Assets follow a naming convention:
- `torch_sconce.glb` — full detail (LOD0)
- `torch_sconce_lod1.glb` — medium (~50% tris)
- `torch_sconce_lod2.glb` — low (~15% tris)

**LODGroup in AssetManager:**

```cpp
struct LODGroup {
    struct Level {
        std::shared_ptr<ModelData> model;
        float maxDist; // switch to next LOD beyond this
    };
    std::vector<Level> levels; // sorted by maxDist ascending
};
```

`AssetManager::loadLODGroup(basePath)` auto-discovers `_lod1`, `_lod2` variants.

**Selection at draw time:**

Static prop draw submissions carry a world position. `Renderer::drawMesh` (or a new
`drawLODGroup`) computes `length(objPos - m_cameraPos)` and selects the appropriate
`Level`. Because `m_cameraPos` is already cached per-frame, this is a single distance
check per prop draw call.

**Distance thresholds** are configurable per asset via a `.lod.json` sidecar, defaulting
to sensible values (e.g., 20m / 50m / 100m).

**Estimated scope:** Small-medium. ~200 LOC. No rendering architecture changes — purely
additive data and a selection step.

---

## System 4 — Dynamic Light Budget

### The Problem

`LightEnvironment::k_maxPointLights = 8` is a compile-time constant backed by a fixed
UBO layout. A single bonfire room already consumes most of the budget. A world with
dozens of torches, bonfires, and ambient fill lights is impossible.

### Verdict

The right solution is not just raising the limit — it is **light relevance culling**.
Only the N closest / highest-intensity lights to the camera (or camera + player) should
be submitted to the shader each frame. The logical limit can be much higher (e.g., 256
scene lights); the shader limit stays bounded (16–32 is sufficient for a Souls-style
aesthetic).

### Implementation Plan

**Decouple scene lights from shader lights:**

```cpp
// Scene-side (unbounded)
class LightRegistry {
public:
    void registerLight(const Light& light, const glm::vec3& worldPos);
    void unregisterLight(LightHandle handle);
    // Returns up to k_maxShaderLights sorted by relevance to viewPos
    std::vector<Light> gatherRelevant(const glm::vec3& viewPos, int maxCount) const;
private:
    std::vector<std::pair<Light, glm::vec3>> m_lights;
};
```

**Relevance scoring** — a simple heuristic: `score = light.intensity * light.range / max(dist, 0.1f)`.
Sort descending, take the first `maxCount`.

**Raise shader limit** — change `k_maxPointLights` to 16 or 32 and update the UBO
layout and fragment shader loop accordingly.

**Each frame in ForgeGame::onRender:**

```cpp
auto relevant = m_lightRegistry.gatherRelevant(camera.getPosition(), 16);
m_renderer.getLights().clearPointLights();
for (auto& l : relevant) m_renderer.getLights().addPointLight(l);
m_renderer.getLights().upload();
```

Zones expose their lights to the `LightRegistry` when loaded and unregister on eviction.

**Estimated scope:** Small. ~150 LOC. Shader change is trivial (loop bound constant).

---

## System 5 — Navmesh & Pathfinding (Recast/Detour)

### The Problem

Enemy AI navigates via linear waypoint chains (`patrol_waypoint` entities). This works
for a single corridor patrol but breaks completely the moment enemies need to:

- Path around obstacles dynamically
- Chase the player through complex geometry
- Climb stairs or navigate multi-level arenas

Sekiro and DS3 enemies navigate every staircase, ramp, and bridge because they use a
full navmesh. The current system cannot replicate this.

### Verdict

Recast/Detour is the industry standard for game navmesh and is MIT-licensed. Integrating
it is the correct choice — it is what the Souls games themselves use (Source Engine and
many Unreal titles also use it or equivalents). Waypoints become **hints** on top of a
navmesh rather than the sole navigation primitive.

### Implementation Plan

**Integration architecture:**

```cpp
// engine/include/forge/NavMesh.h
class NavMesh {
public:
    bool build(const std::vector<glm::vec3>& verts,
               const std::vector<uint32_t>& indices,
               const rcConfig& cfg);

    bool findPath(const glm::vec3& from, const glm::vec3& to,
                  std::vector<glm::vec3>& outPath) const;

    bool findNearestPoint(const glm::vec3& pos, glm::vec3& out) const;

    void debugDraw(DebugDraw& dd) const;

private:
    // Recast intermediate data
    rcHeightfield*        m_hf    = nullptr;
    rcCompactHeightfield* m_chf   = nullptr;
    rcContourSet*         m_cset  = nullptr;
    rcPolyMesh*           m_pmesh = nullptr;
    rcPolyMeshDetail*     m_dmesh = nullptr;
    // Detour runtime
    dtNavMesh*       m_navMesh  = nullptr;
    dtNavMeshQuery*  m_navQuery = nullptr;
};
```

**Build pipeline:**

1. `LevelLoader` already produces collision geometry (`MapScene::collisionPositions` /
   `collisionIndices`).
2. After physics setup in `setupLevel`, pass that geometry to `NavMesh::build()` with
   tuned `rcConfig` (cell size ~0.2m, agent radius ~0.3m, agent height ~1.8m, max
   climb ~0.5m, max slope ~45°).
3. Store the `NavMesh` on the active `Zone`.
4. Serialize the baked navmesh to a `.nav` cache file alongside the `.map` to avoid
   runtime rebake.

**AI integration:**

`AIComponent` gains a `NavigationRequest` that replaces the waypoint-only path. When
chasing, the AI queries `NavMesh::findPath(enemyPos, playerPos)` to get a waypoint
corridor, then steers toward the next corridor point using the existing movement code.

Waypoint entities become **patrol path hints** fed into Detour's "closest polygon on
navmesh" for the waypoint positions, which gives the AI more organic loitering behavior.

**Estimated scope:** Medium. Recast/Detour are header-only / single-TU drops. Navmesh
build and debug draw are ~300 LOC. AI integration depends on how deeply AIComponent
needs to change, but the steering side is a small change.

---

## System 6 — Terrain for Outdoor Zones

### The Problem

Brush geometry is excellent for hand-crafted interiors, corridors, ruins, and dungeons.
It becomes painful for:

- Large open hillsides (Sekiro's Ashina Outskirts)
- Rolling terrain between landmarks
- Natural cliff faces and mountain slopes

Hundreds of tiny wedge-shaped brushes to approximate a hillside is slow to author,
produces lots of small draw calls, and is hard to iterate on.

### Verdict

Don't replace brushwork — add a terrain layer *on top of* it. The workflow stays in
TrenchBroom for all architectural geometry; terrain is a heightmap-driven mesh dropped
underneath. Souls games blend both approaches: hand-sculpted cliffs and paths (brushwork
analog) over broad terrain meshes for the ground plane.

A heightmap terrain system can coexist with the existing brush scene by being submitted
as an additional `drawMesh` call with a dedicated terrain shader.

### Implementation Plan

**Data format:**

A terrain zone is described by:
- A 16-bit PNG heightmap (`terrain_ashina.hmap.png`) — each texel is one terrain vertex
- A `terrain_ashina.terrain.json` sidecar specifying world scale (meters per texel,
  max height)
- Up to 4 splat texture channels for surface materials (rock, dirt, grass, snow), driven
  by a separate RGBA splat map PNG

**Terrain mesh generation:**

```cpp
class TerrainMesh {
public:
    void loadFromHeightmap(const std::string& hmapPath,
                           const std::string& configPath,
                           AssetManager& assets);

    // Returns a renderable Mesh (uploaded to GPU)
    const Mesh& getMesh() const { return m_mesh; }

    // Returns collision geometry for Bullet
    const std::vector<glm::vec3>& getCollisionVerts() const;
    const std::vector<uint32_t>&  getCollisionIndices() const;

    float sampleHeight(float worldX, float worldZ) const;

private:
    Mesh m_mesh;
    std::vector<glm::vec3> m_collVerts;
    std::vector<uint32_t>  m_collIdx;
    // raw heightmap for sampleHeight()
    std::vector<float> m_heights;
    int m_width, m_depth;
    float m_scaleXZ, m_scaleY;
};
```

**Terrain shader** (`terrain.vert/frag`) — samples the splat map in the fragment stage
to blend up to 4 PBR surface materials. Uses the existing `LightEnvironment` UBO so no
lighting changes are needed.

**Terrain LOD (later):** Geomipmapping or a CDLOD approach can be added later by
replacing the static mesh with a dynamic patch-based tessellation system. Start with
a fixed-resolution mesh and add LOD once the terrain system proves out.

**TrenchBroom integration:** A `terrain_zone` point entity in the FGD specifies which
heightmap and config to load. The terrain is invisible in TrenchBroom but the editor can
show a bounding box so designers know where it sits.

**Estimated scope:** Medium. Heightmap load + mesh gen is ~400 LOC. Splat shader is
~80 LOC of new GLSL. Physics body setup reuses `RigidBodyComponent` mesh constructor
already in place.

---

## System 7 — Occlusion Culling for Dense Environments

### The Problem

Souls games are famously dense — Anor Londo's interior, Blighttown's structure,
Irithyll's long corridors. The player can often see a few dozen meters but the entire
level is loaded. Frustum culling (System 2) eliminates geometry behind the camera;
occlusion culling eliminates geometry behind *other geometry* in front of the camera.
Without it, a player standing outside Anor Londo wastes GPU time drawing every interior
room even though they are completely hidden behind the outer walls.

### Verdict

Software occlusion culling via a **hierarchical depth buffer** (HZB) approach is the
practical choice for an indie engine on OpenGL. Hardware occlusion queries exist but
add GPU-CPU round-trip latency. A CPU-side HZB rasterizer (e.g., the open-source
Masked Software Occlusion Culling library from Intel) is fast, deterministic, and
integrates cleanly as a pre-pass.

This is a polish/optimization system — implement after streaming and frustum culling
are in place, once you have a level dense enough to measure the gain.

### Implementation Plan

**Occluder mesh extraction:**

Not every brush needs to be an occluder. Large, opaque, convex brushes (exterior walls,
pillars, floors) are tagged with an `occluder=1` property in TrenchBroom. During
`setupLevel`, those brushes are simplified into coarse occluder meshes and handed to the
software rasterizer.

**Per-frame CPU HZB pass:**

1. Rasterize all occluder meshes at ~128×72 resolution (quarter-res with 4 depth mips)
   into a CPU-side depth buffer.
2. For each `MapRenderObject` AABB, project and test against the HZB. If fully
   occluded, skip the draw call.
3. This runs on a background thread and produces a visibility bitfield for the render
   thread to consume.

**Library:** Intel's Masked Software Occlusion Culling (MIT) can rasterize ~1000
triangles per millisecond. For typical Souls-scale occluder counts this fits inside 0.5ms
on a modern CPU.

**Estimated scope:** Large, but mostly integration work. ~500 LOC to wire up. The
occluder extraction and mesh simplification are the tricky parts.

---

## System 8 — World Graph & Area Connectivity

### The Problem

There is no concept of the *world* as a structure. Lua calls `loadLevel("level_01")`
and the game knows nothing about what is adjacent, what shortcuts have been opened, or
how areas relate to each other. Building the DS1-style "ah, this ladder connects back to
Firelink Shrine" moment requires the engine to understand that zones are nodes in a
graph with edges that can be opened and closed.

### Verdict

A JSON world manifest (introduced in System 1) serves double duty as the world graph.
Edges in the graph are traversable connections between zones; they can be conditionally
enabled by flag state (e.g., a shortcut elevator is only accessible after pulling the
lever, which sets a flag). This is a design-level system more than a pure engine system,
but it needs a runtime home.

### Implementation Plan

**World graph edge types:**

```jsonc
{
  "connections": [
    {
      "from": "firelink", "to": "undead_burg",
      "type": "one_way",       // always open, one direction
      "entry_trigger": "ladder_to_burg"
    },
    {
      "from": "undead_burg", "to": "firelink",
      "type": "conditional",   // shortcut, opens after flag set
      "required_flag": 10001,  // SHORTCUT_ELEVATOR_PULLED
      "entry_trigger": "elevator_down"
    }
  ]
}
```

**Runtime:** `ZoneGraph::isConnectionOpen(from, to, FlagManager&)` checks the edge's
`required_flag` against the live flag state. `stream_trigger` entities query this before
initiating a load, and `zone_boundary` entities query it before allowing the player to
cross. This means unlocking a shortcut elevator requires only setting a flag — no level
reload, no special code.

**Map screen (future):** The world graph is the data source for an in-game map. Each
zone has a 2D bounding rectangle in the manifest. Once zones are connected, a top-down
map screen can be generated procedurally from the graph without any additional authoring.

**Estimated scope:** Small. ~200 LOC. Mostly data-driven on top of work already done
for System 1.

---

## Priority Roadmap

The systems above are roughly ordered by return-on-investment. Here is a suggested
implementation sequence:

| Phase | System | Why Now |
|---|---|---|
| **1** | Frustum Culling (Sys 2) | Tiny effort, immediate benefit, prerequisite for everything else |
| **2** | Dynamic Light Budget (Sys 4) | Unblocks richer level art right away |
| **3** | Zone Streaming + World Graph (Sys 1 + 8) | Core architecture; do together since the manifest serves both |
| **4** | Navmesh / Pathfinding (Sys 5) | Needed before boss and open-area enemy encounters feel right |
| **5** | Terrain System (Sys 6) | Unlocks outdoor zones; build one outdoor area to test |
| **6** | LOD for Props (Sys 3) | Add once the world has enough props to feel the cost |
| **7** | Occlusion Culling (Sys 7) | Last optimization pass once density is high enough to measure |

---

## On TrenchBroom Specifically

TrenchBroom is not the weak link. The format it produces (Q2 .map) is a superset of
what most AAA Souls-style games use for their "structural" geometry: hand-placed convex
volumes that the level geometry is carved from. The weak link is that the current
engine does not give TrenchBroom output the runtime treatment it deserves.

The one area where TrenchBroom genuinely struggles is **smooth organic terrain** — it
cannot paint heightmaps or sculpt soil. System 6 is the answer there. For everything
else — ruins, dungeons, castles, cliffs with hard faces, rooftops, catacombs — brush
geometry authored in TrenchBroom is the right tool and should remain the primary
authoring workflow.

If and when the world expands to Elden Ring scale (open plains, enormous render
distances), a proper world-space chunk streaming system with terrain LOD would be
warranted. That is a much larger project and a different game scope. For a Souls /
Sekiro-style game with discrete interconnected zones, the roadmap above gets you there.
