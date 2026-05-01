# Scene Previewer

The Scene Previewer is a web-based tool for visualizing Skewer scenes before rendering. It lets you inspect layers, objects, and materials, and make basic edits.

## Features

- **Open Scenes** - Load folders containing layer-format scenes
- **Layer Parsing** - Parse and display contexts and layers with materials and objects
- **Object Selection** - Click objects in the viewport or sidebar to select
- **Property Editing** - Edit sphere center/radius, quad vertices, obj transforms, material properties
- **Delete Objects** - Press `Delete` or `Backspace` to remove selected objects
- **Recent Scenes** - Quick access to recently opened scenes (stored in IndexedDB)

## Getting Started

```bash
cd apps/scene-previewer
bun install
bun run dev
```

Open http://localhost:5173 to access the previewer.

## Usage

### Opening a Scene

1. Click **Open Existing Scene** on the landing page
2. Select a folder containing scene.json and layer files
3. The scene loads with all layers parsed

The scene tree appears in the left sidebar, showing:
- Contexts (background elements)
- Layers (renderable objects)
- Materials within each layer
- Objects within each layer

### Creating a New Scene

1. Click **New Scene** on the landing page
2. Choose a template (e.g., "Cornell Box" or basic sphere)
3. Start with a basic scene to experiment

### Navigation Controls

- **Orbit** - Left-click and drag to rotate the camera
- **Pan** - Right-click and drag (or middle-click)
- **Zoom** - Scroll wheel

### Selecting Objects

Click on an object in either:
- The **viewport** (3D view) - click directly on the object
- The **sidebar** - click on the object name in the tree

When selected:
- The object highlights with an amber outline in the viewport
- The properties panel appears on the right

### Editing Properties

With an object selected, edit in the right panel:

**For Spheres:**
- Center (position)
- Radius
- Material assignment

**For Quads:**
- Vertex positions (p0, p1, p2, p3)
- Material assignment

**For OBJ meshes:**
- Transform: position, rotation, scale
- Material assignment
- Auto-fit toggle

### Editing Materials

With a material selected (click on material name in sidebar):

**Common properties:**
- Type: `lambertian`, `metal`, or `dielectric`
- Albedo (RGB)
- Emission (RGB)

**Metal-specific:**
- Roughness

**Dielectric-specific:**
- IOR (Index of Refraction)
- Roughness

### Deleting Objects

Select an object and press `Delete` or `Backspace` to remove it from the scene.

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| Delete / Backspace | Delete selected object |
| Escape | Deselect |

## Tech Stack

- **React 19** - UI framework
- **TypeScript** - Type safety
- **Vite** - Build tool
- **Bun** - Package manager and runtime
- **Three.js** - 3D rendering
- **Biome** - Linting and formatting

## See Also

- [Scene Format](scene-format.md) - Complete scene file specification
- [Rendering Tips](rendering-tips.md) - Best practices for quality and performance
- [Animation](animation.md) - Keyframe animation and motion blur
- [Compositing](compositing.md) - Layer compositing with loom
- [CLI Reference](cli.md) - Command-line options