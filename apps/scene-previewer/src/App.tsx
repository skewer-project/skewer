import { useState } from "react";
import { OpenFolderButton } from "./components/OpenFolderButton";
import { SceneInspector } from "./components/SceneInspector";
import type { ResolvedScene } from "./types/scene";

function App() {
  const [scene, setScene] = useState<ResolvedScene | null>(null);
  const [error, setError] = useState<string>("");

  return (
    <div style={{ fontFamily: "monospace", padding: "1rem", maxWidth: "900px" }}>
      <h1 style={{ fontSize: "1.25rem", marginBottom: "0.5rem" }}>Skewer Scene Previewer</h1>
      <OpenFolderButton onSceneLoaded={setScene} onError={setError} />
      {error && <p style={{ color: "red", marginTop: "0.5rem" }}>Error: {error}</p>}
      {scene && <SceneInspector scene={scene} />}
    </div>
  );
}

export default App;
