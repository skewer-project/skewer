# Scene Previewer

The Scene Previewer is a web-based tool for visualizing Skewer scenes before rendering.

!!! note "Coming Soon"
    This feature is introduced in PRs #136-138 and will be available after merging.

## Features

- **Open Scenes** - Load folders containing layer-format scenes
- **Layer Parsing** - Parse and display layers with materials and objects
- **Object Selection** - Select and inspect individual objects
- **Property Editing** - Edit basic object properties
- **Landing Page** - Create new scenes based on Cornell Box or open existing ones
- **Recent Scenes** - Quick access to recently opened scenes

## Getting Started

After the previewer is merged into `main`:

```bash
cd apps/scene-previewer
bun install
bun run dev
```

Open http://localhost:5173 to access the previewer.

## Usage

### Opening a Scene

1. Click "Open Existing Scene" on the landing page
2. Select a folder containing layer-format JSON files
3. The scene loads with all layers parsed

### Creating a New Scene

1. Click "New Scene" on the landing page
2. Choose "Cornell Box" as a template
3. Start with a basic scene to experiment with

### Navigation Controls

- **Orbit** - Click and drag to rotate the camera
- **Pan** - Right-click and drag (or middle-click)
- **Zoom** - Scroll wheel

### Object Editing

1. Click on an object to select it
2. Use the properties panel to modify:
   - Transform (position, rotation, scale)
   - Material properties

## Tech Stack

- **React** - UI framework
- **TypeScript** - Type safety
- **Vite** - Build tool
- **Bun** - Package manager and runtime
- **Biome** - Linting and formatting
