import { useState } from "react";
import { OpenFolderButton } from "./components/OpenFolderButton";
import { SceneInspector } from "./components/SceneInspector";
import { Viewport } from "./components/Viewport";
import type { ResolvedScene } from "./types/scene";

function App() {
	const [scene, setScene] = useState<ResolvedScene | null>(null);
	const [error, setError] = useState<string>("");

	return (
		<div
			style={{
				display: "flex",
				flexDirection: "column",
				height: "100dvh",
				fontFamily: "monospace",
			}}
		>
			{/* Toolbar */}
			<div
				style={{
					padding: "0.5rem 1rem",
					borderBottom: "1px solid #333",
					display: "flex",
					alignItems: "center",
					gap: "1rem",
				}}
			>
				<strong>Skewer</strong>
				<OpenFolderButton onSceneLoaded={setScene} onError={setError} />
				{error && <span style={{ color: "red" }}>{error}</span>}
				{scene && (
					<span style={{ color: "#888" }}>
						{scene.contexts.length}c / {scene.layers.length}L /{" "}
						{[...scene.contexts, ...scene.layers].reduce(
							(s, l) => s + l.data.objects.length,
							0,
						)}{" "}
						objects
					</span>
				)}
			</div>

			{/* Main area */}
			<div style={{ flex: 1, display: "flex", overflow: "hidden" }}>
				{/* Left panel: scene inspector */}
				{scene && (
					<div
						style={{
							width: "280px",
							overflowY: "auto",
							borderRight: "1px solid #333",
							padding: "0.5rem",
						}}
					>
						<SceneInspector scene={scene} />
					</div>
				)}

				{/* Viewport */}
				<div style={{ flex: 1, position: "relative" }}>
					<Viewport />
				</div>
			</div>
		</div>
	);
}

export default App;
