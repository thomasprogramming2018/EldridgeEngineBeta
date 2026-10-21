# Scene editing

- Ctrl+Z: undo the last scene edit.
- Ctrl+Y or Ctrl+Shift+Z: redo.
- Ctrl+S: save the active scene.
- Ctrl+Shift+S: save the active scene to a different file.
- Ctrl+O: open an .elscene file in a scene tab.
- Ctrl+N: create an empty scene tab.

The File and Edit menus provide the same actions. A star next to a scene's name
means it has unsaved changes. Closing the editor asks what to do with modified
scenes. Opening a file that already has a tab reloads the saved file in that tab.
If the tab has unsaved changes, confirm reloading or cancel to keep them.

Each scene keeps its own history of up to 100 snapshots. Creating, deleting,
renaming, parenting, transform controls, material values, and shader assignments
are recorded. A new edit after undo removes the redo branch. Loading starts a
fresh history. Undoing replaces scene objects, so the inspector selection clears.
Text fields may handle their own text undo while they have keyboard focus.

Scene files are JSON. They contain object names, parent relationships, local
position/rotation/scale, mesh geometry, material values, and shader paths.
Shader and texture files remain external assets: keep them at their referenced
paths. Scene saving does not copy these assets or save camera navigation.
Editing shader source changes the .elsha file; scene undo does not undo edits
to external files.

SceneSerialization.cpp contains the file and history code. toJson creates the
same CPU snapshot used by both saving and undo. fromJson builds and validates a
new set of objects before replacing the current objects. QSaveFile writes a
replacement safely instead of overwriting the original file a piece at a time.

To make a future editor action undoable, update the scene and then call
scene.recordEdit(). Do not rebuild the inspector while a control is emitting
its value-changed signal. Scene::objectsAboutToReset clears the inspector before
undo or loading destroys its old component references.

Project and prefab packaging, object import, and rendering options are documented
in ProjectsAndGraphics.md. Project files copy dependencies; scene files do not.
