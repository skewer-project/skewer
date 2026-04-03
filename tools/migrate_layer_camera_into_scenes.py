#!/usr/bin/env python3
"""
Inline `layer_camera.json` into each `scene*.json` in a layers-format animation folder,
matching `scenes/layers_format/deep_test/scene.json` (camera on the scene object, not as a layer).

Removes `layer_camera.json` from each scene's `layers` list. Optionally deletes the
standalone `layer_camera.json` file after migration.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

LAYER_CAMERA_NAME = "layer_camera.json"


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def load_camera(layer_camera_path: Path) -> dict:
    data = json.loads(layer_camera_path.read_text(encoding="utf-8"))
    if "camera" not in data:
        print(f"error: {layer_camera_path} has no 'camera' key", file=sys.stderr)
        sys.exit(1)
    return data["camera"]


def migrate_scene(path: Path, camera: dict, dry_run: bool) -> bool:
    raw = path.read_text(encoding="utf-8")
    data = json.loads(raw)
    layers = data.get("layers")
    if not isinstance(layers, list):
        print(f"skip (no layers list): {path}", file=sys.stderr)
        return False

    if LAYER_CAMERA_NAME not in layers:
        # Already migrated or different layout; idempotent no-op.
        return False

    new_layers = [x for x in layers if x != LAYER_CAMERA_NAME]

    data["camera"] = camera
    data["layers"] = new_layers

    if dry_run:
        print(f"would update: {path}")
        return True

    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print(f"updated: {path}")
    return True


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "scene_dir",
        nargs="?",
        type=Path,
        default=_repo_root() / "scenes/layers_format/animation/altar_animation",
        help="Directory containing layer_camera.json and scene*.json (default: altar_animation)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print actions without writing files",
    )
    parser.add_argument(
        "--delete-layer-file",
        action="store_true",
        help=f"Remove {LAYER_CAMERA_NAME} from the scene directory after a successful run",
    )
    args = parser.parse_args()
    scene_dir = args.scene_dir.resolve()
    layer_camera = scene_dir / LAYER_CAMERA_NAME

    scenes = sorted(scene_dir.glob("scene*.json"))
    if not scenes:
        print(f"error: no scene*.json under {scene_dir}", file=sys.stderr)
        sys.exit(1)

    still_referenced = [
        p
        for p in scenes
        if LAYER_CAMERA_NAME in (json.loads(p.read_text(encoding="utf-8")).get("layers") or [])
    ]

    if not layer_camera.is_file():
        if not still_referenced:
            print("nothing to do (no layer_camera.json and no scene lists it)")
            sys.exit(0)
        print(
            f"error: {len(still_referenced)} scene(s) still list {LAYER_CAMERA_NAME} "
            f"but {layer_camera} is missing; restore that file or fix scenes by hand",
            file=sys.stderr,
        )
        sys.exit(1)

    camera = load_camera(layer_camera)

    changed = 0
    for p in scenes:
        if migrate_scene(p, camera, args.dry_run):
            changed += 1

    if args.delete_layer_file and not args.dry_run:
        layer_camera.unlink()
        print(f"deleted: {layer_camera}")
    elif args.delete_layer_file and args.dry_run:
        print(f"would delete: {layer_camera}")

    print(f"done: {changed} scene file(s) {'(dry run)' if args.dry_run else ''}")


if __name__ == "__main__":
    main()
