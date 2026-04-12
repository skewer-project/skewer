import { useCallback, useEffect, useRef, useState } from "react";
import { LandingPage } from "./components/LandingPage";
import {
  MaterialPropertiesPanel,
  PropertiesPanel,
} from "./components/PropertiesPanel";
import { SceneInspector } from "./components/SceneInspector";
import type { ViewportHandle } from "./components/Viewport";
import { Viewport } from "./components/Viewport";
import { addRecentScene } from "./services/recent-scenes";
import { saveScene } from "./services/scene-serializer";
import type { Material, ResolvedScene, SceneObject } from "./types/scene";

function App() {
  const [scene, setScene] = useState<ResolvedScene | null>(null);
  const [dirHandle, setDirHandle] = useState<FileSystemDirectoryHandle | null>(
    null,
  );
  const [error, setError] = useState<string>("");
  const [selectedObjectKey, setSelectedObjectKey] = useState<string | null>(
    null,
  );
  const [selectedMaterialKey, setSelectedMaterialKey] = useState<string | null>(
    null,
  );

  function handleSelectObject(key: string | null) {
    setSelectedObjectKey(key);
    setSelectedMaterialKey(null);
  }

  function handleSelectMaterial(key: string | null) {
    setSelectedMaterialKey(key);
    setSelectedObjectKey(null);
  }
  const [sceneVersion, setSceneVersion] = useState(0);
  const [saving, setSaving] = useState(false);
  const [hasUnsavedChanges, setHasUnsavedChanges] = useState(false);
  const viewportRef = useRef<ViewportHandle>(null);

  function isEditableTarget(target: EventTarget | null) {
    if (!(target instanceof HTMLElement)) return false;
    if (target.isContentEditable) return true;
    return (
      target.tagName === "INPUT" ||
      target.tagName === "TEXTAREA" ||
      target.tagName === "SELECT"
    );
  }

  function handleSceneLoaded(s: ResolvedScene, dir: FileSystemDirectoryHandle) {
    setScene(s);
    setDirHandle(dir);
    setError("");
    setSelectedObjectKey(null);
    setSelectedMaterialKey(null);
    setSceneVersion((v) => v + 1);
    setHasUnsavedChanges(false);
    addRecentScene(dir.name, dir);
  }

  /** Update scene data without triggering a full Three.js rebuild. */
  const handleSceneEdit = useCallback(
    (updater: (s: ResolvedScene) => ResolvedScene) => {
      setScene((prev) => {
        if (!prev) return prev;
        setHasUnsavedChanges(true);
        return updater(prev);
      });
    },
    [setScene, setHasUnsavedChanges],
  );

  async function handleSave() {
    if (!scene || !dirHandle) return;
    setSaving(true);
    setError("");
    try {
      await saveScene(dirHandle, scene);
      setHasUnsavedChanges(false);
    } catch (err) {
      if (err instanceof DOMException && err.name === "AbortError") return;
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setSaving(false);
    }
  }

  const handleDeleteObject = useCallback(
    (objectKey: string) => {
      const [tag, liStr, oiStr] = objectKey.split(":");
      const li = Number(liStr);
      const oi = Number(oiStr);
      handleSceneEdit((s) => {
        const listKey = tag === "ctx" ? "contexts" : "layers";
        const newList = [...s[listKey]];
        const newLayer = {
          ...newList[li],
          data: {
            ...newList[li].data,
            objects: newList[li].data.objects.filter((_, i) => i !== oi),
          },
        };
        newList[li] = newLayer;
        return { ...s, [listKey]: newList };
      });
      setSceneVersion((v) => v + 1);
      setSelectedObjectKey(null);
    },
    [handleSceneEdit, setSceneVersion, setSelectedObjectKey],
  );

  useEffect(() => {
    if (!scene) return;
    const handleKeyDown = (event: KeyboardEvent) => {
      if (event.key !== "Delete" && event.key !== "Backspace") return;
      if (!selectedObjectKey) return;
      if (isEditableTarget(event.target)) return;
      event.preventDefault();
      handleDeleteObject(selectedObjectKey);
    };
    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [scene, selectedObjectKey, handleDeleteObject]);

  function handleAddObject(
    tag: "ctx" | "lyr",
    layerIdx: number,
    obj: SceneObject,
  ) {
    let newObjIdx = -1;
    handleSceneEdit((s) => {
      const listKey = tag === "ctx" ? "contexts" : "layers";
      newObjIdx = s[listKey][layerIdx].data.objects.length;
      const newList = [...s[listKey]];
      const newLayer = {
        ...newList[layerIdx],
        data: {
          ...newList[layerIdx].data,
          objects: [...newList[layerIdx].data.objects, obj],
        },
      };
      newList[layerIdx] = newLayer;
      return { ...s, [listKey]: newList };
    });
    setSceneVersion((v) => v + 1);
    if (newObjIdx >= 0) {
      setSelectedObjectKey(`${tag}:${layerIdx}:${newObjIdx}`);
      setSelectedMaterialKey(null);
    }
  }

  function handleAddMaterial(
    tag: "ctx" | "lyr",
    layerIdx: number,
    name: string,
    mat: Material,
  ) {
    handleSceneEdit((s) => {
      const listKey = tag === "ctx" ? "contexts" : "layers";
      const newList = [...s[listKey]];
      const newLayer = {
        ...newList[layerIdx],
        data: {
          ...newList[layerIdx].data,
          materials: { ...newList[layerIdx].data.materials, [name]: mat },
        },
      };
      newList[layerIdx] = newLayer;
      return { ...s, [listKey]: newList };
    });
    setSelectedMaterialKey(`${tag}:${layerIdx}:mat:${name}`);
    setSelectedObjectKey(null);
  }

  function handleNavigateHome() {
    if (
      hasUnsavedChanges &&
      !window.confirm(
        "You have unsaved changes. Go back to the landing page and discard them?",
      )
    ) {
      return;
    }
    setScene(null);
    setDirHandle(null);
    setSelectedObjectKey(null);
    setSelectedMaterialKey(null);
    setHasUnsavedChanges(false);
    setError("");
  }

  const totalObjects = scene
    ? [...scene.contexts, ...scene.layers].reduce(
        (s, l) => s + l.data.objects.length,
        0,
      )
    : 0;

  return (
    <div className="app-root">
      {/* Full-screen viewport */}
      <div className="viewport-fill">
        <Viewport
          ref={viewportRef}
          scene={scene}
          dirHandle={dirHandle}
          sceneVersion={sceneVersion}
          selectedObjectKey={selectedObjectKey}
          onSelectObject={handleSelectObject}
        />
      </div>

      {/* HUD overlay */}
      <div className="hud">
        {/* Top-left: header panel */}
        <div className="panel hud-header">
          <button
            type="button"
            className={`wordmark${scene ? " wordmark-link" : ""}`}
            onClick={handleNavigateHome}
            disabled={!scene}
          >
            Skewer
          </button>
          {scene && hasUnsavedChanges && (
            <button
              type="button"
              className={`open-btn${saving ? " loading" : ""}`}
              disabled={saving}
              onClick={handleSave}
            >
              {saving ? "Saving…" : "Save"}
            </button>
          )}
          {error && <span className="error-msg">{error}</span>}
        </div>

        {/* Left sidebar: scene inspector */}
        {scene && dirHandle && (
          <div className="panel hud-sidebar">
            <SceneInspector
              scene={scene}
              selectedObjectKey={selectedObjectKey}
              selectedMaterialKey={selectedMaterialKey}
              onSelectObject={handleSelectObject}
              onSelectMaterial={handleSelectMaterial}
              onAddObject={handleAddObject}
              onAddMaterial={handleAddMaterial}
              dirHandle={dirHandle}
            />
          </div>
        )}

        {/* Right sidebar: properties panel */}
        {scene && (selectedObjectKey || selectedMaterialKey) && (
          <div className="panel hud-properties">
            {selectedObjectKey && (
              <PropertiesPanel
                scene={scene}
                objectKey={selectedObjectKey}
                onSceneEdit={handleSceneEdit}
                onDeleteObject={() => handleDeleteObject(selectedObjectKey)}
                viewportRef={viewportRef}
              />
            )}
            {selectedMaterialKey && (
              <MaterialPropertiesPanel
                scene={scene}
                matKey={selectedMaterialKey}
                onSceneEdit={handleSceneEdit}
                viewportRef={viewportRef}
              />
            )}
          </div>
        )}

        {/* Bottom-right: stats */}
        {scene && (
          <div className="panel hud-stats">
            <span className="stat-tag stat-ctx">{scene.contexts.length}c</span>
            <span className="stat-sep">/</span>
            <span className="stat-tag stat-lyr">{scene.layers.length}L</span>
            <span className="stat-sep">/</span>
            <span className="stat-num">{totalObjects} obj</span>
            {scene.output_dir && (
              <>
                <span className="stat-sep">&rarr;</span>
                <span className="stat-dir">{scene.output_dir}</span>
              </>
            )}
          </div>
        )}

        {/* Landing page */}
        {!scene && (
          <LandingPage onSceneLoaded={handleSceneLoaded} onError={setError} />
        )}
      </div>
    </div>
  );
}

export default App;
