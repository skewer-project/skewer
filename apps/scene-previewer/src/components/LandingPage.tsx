import { useEffect, useState } from "react";
import { openSceneFolder } from "../services/fs";
import { createNewScene } from "../services/new-scene";
import {
	getRecentScenes,
	type RecentEntry,
	removeRecentScene,
} from "../services/recent-scenes";
import { loadScene } from "../services/scene-parser";
import type { ResolvedScene } from "../types/scene";

interface Props {
	onSceneLoaded: (scene: ResolvedScene, dir: FileSystemDirectoryHandle) => void;
	onError: (error: string) => void;
}

function timeSince(epoch: number): string {
	const seconds = Math.floor((Date.now() - epoch) / 1000);
	if (seconds < 60) return "just now";
	const minutes = Math.floor(seconds / 60);
	if (minutes < 60) return `${minutes}m ago`;
	const hours = Math.floor(minutes / 60);
	if (hours < 24) return `${hours}h ago`;
	const days = Math.floor(hours / 24);
	if (days < 30) return `${days}d ago`;
	return new Date(epoch).toLocaleDateString();
}

export function LandingPage({ onSceneLoaded, onError }: Props) {
	const [recents, setRecents] = useState<RecentEntry[]>([]);
	const [loading, setLoading] = useState<string | null>(null); // loading key or "open"/"new"

	useEffect(() => {
		getRecentScenes().then(setRecents);
	}, []);

	async function openAndLoad(dir: FileSystemDirectoryHandle) {
		try {
			const scene = await loadScene(dir);
			onSceneLoaded(scene, dir);
		} catch (err) {
			if (err instanceof DOMException && err.name === "AbortError") return;
			onError(err instanceof Error ? err.message : String(err));
		} finally {
			setLoading(null);
		}
	}

	async function handleOpenFolder() {
		setLoading("open");
		try {
			const dir = await openSceneFolder();
			await openAndLoad(dir);
		} catch (err) {
			if (err instanceof DOMException && err.name === "AbortError") {
				setLoading(null);
				return;
			}
			onError(err instanceof Error ? err.message : String(err));
			setLoading(null);
		}
	}

	async function handleNewScene() {
		setLoading("new");
		try {
			const dir = await openSceneFolder();
			const scene = await createNewScene(dir);
			onSceneLoaded(scene, dir);
		} catch (err) {
			if (err instanceof DOMException && err.name === "AbortError") {
				setLoading(null);
				return;
			}
			onError(err instanceof Error ? err.message : String(err));
		} finally {
			setLoading(null);
		}
	}

	async function handleOpenRecent(entry: RecentEntry) {
		const key = `recent:${entry.name}`;
		setLoading(key);
		try {
			// Re-request permission if needed
			const perm = await entry.handle.requestPermission({ mode: "readwrite" });
			if (perm !== "granted") {
				await removeRecentScene(entry.name);
				setRecents((r) => r.filter((e) => e.name !== entry.name));
				setLoading(null);
				return;
			}
			await openAndLoad(entry.handle);
		} catch {
			// Handle is stale — remove it
			await removeRecentScene(entry.name);
			setRecents((r) => r.filter((e) => e.name !== entry.name));
			setLoading(null);
		}
	}

	async function handleRemoveRecent(e: React.MouseEvent, entry: RecentEntry) {
		e.stopPropagation();
		await removeRecentScene(entry.name);
		setRecents((r) => r.filter((re) => re.name !== entry.name));
	}

	return (
		<div className="landing">
			{/* Decorative grid background */}
			<div className="landing-grid" />

			<div className="landing-content">
				{/* Branding */}
				<div className="landing-brand">
					<div className="landing-wordmark">Skewer</div>
					<div className="landing-tagline">Scene Previewer</div>
				</div>

				<div className="landing-columns">
					{/* Start section */}
					<div className="landing-section">
						<div className="landing-section-head">Start</div>

						<button
							type="button"
							className="landing-action"
							onClick={handleNewScene}
							disabled={loading !== null}
						>
							<span className="landing-action-icon">
								<svg
									width="16"
									height="16"
									viewBox="0 0 16 16"
									fill="none"
									aria-hidden="true"
								>
									<path
										d="M8 3v10M3 8h10"
										stroke="currentColor"
										strokeWidth="1.5"
										strokeLinecap="round"
									/>
								</svg>
							</span>
							<span className="landing-action-body">
								<span className="landing-action-title">
									{loading === "new" ? "Creating\u2026" : "New Scene"}
								</span>
								<span className="landing-action-desc">
									Bootstrap a new scene
								</span>
							</span>
						</button>

						<button
							type="button"
							className="landing-action"
							onClick={handleOpenFolder}
							disabled={loading !== null}
						>
							<span className="landing-action-icon">
								<svg
									width="16"
									height="16"
									viewBox="0 0 16 16"
									fill="none"
									aria-hidden="true"
								>
									<path
										d="M2 4.5h4.5l1.5 1.5H14v7H2z"
										stroke="currentColor"
										strokeWidth="1.2"
										strokeLinejoin="round"
									/>
								</svg>
							</span>
							<span className="landing-action-body">
								<span className="landing-action-title">
									{loading === "open" ? "Opening\u2026" : "Open Scene Folder"}
								</span>
								<span className="landing-action-desc">
									Load an existing scene
								</span>
							</span>
						</button>
					</div>

					{/* Recent section */}
					{recents.length > 0 && (
						<div className="landing-section">
							<div className="landing-section-head">Recent</div>
							<div className="landing-recents">
								{recents.map((entry) => {
									const key = `recent:${entry.name}`;
									return (
										<div key={entry.name} className="landing-recent-row">
											<button
												type="button"
												className={`landing-recent${loading === key ? " loading" : ""}`}
												onClick={() => handleOpenRecent(entry)}
												disabled={loading !== null}
											>
												<span className="landing-recent-icon">
													<svg
														width="14"
														height="14"
														viewBox="0 0 16 16"
														fill="none"
														aria-hidden="true"
													>
														<rect
															x="3"
															y="2"
															width="10"
															height="12"
															rx="1.5"
															stroke="currentColor"
															strokeWidth="1.2"
														/>
														<path
															d="M6 5h4M6 7.5h4M6 10h2.5"
															stroke="currentColor"
															strokeWidth="1"
															strokeLinecap="round"
															opacity="0.5"
														/>
													</svg>
												</span>
												<span className="landing-recent-name">
													{entry.name}
												</span>
												<span className="landing-recent-time">
													{timeSince(entry.lastOpened)}
												</span>
											</button>
											<button
												type="button"
												className="landing-recent-remove"
												onClick={(e) => handleRemoveRecent(e, entry)}
												tabIndex={-1}
												title="Remove from recent"
											>
												&times;
											</button>
										</div>
									);
								})}
							</div>
						</div>
					)}
				</div>

				{/* Footer hint */}
				<div className="landing-footer">
					scenes use the{" "}
					<span className="landing-footer-hl">layers format</span>: a folder
					with scene.json + layer file(s)
				</div>
			</div>
		</div>
	);
}
