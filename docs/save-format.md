# Save format

`roboslop.scene.savegame` stores a *run* of an application, as opposed to
`roboslop.scene.document`, which stores an authored scene. A save refers to its
scene by path instead of copying it, so editing the scene and loading an old
save gives the edited room with the saved run's state on top.

Version 1 is:

```json
{
  "version": 1,
  "scene": "scenes/room.json",
  "objects": [
    {
      "id": "crate",
      "transform": {
        "position": [1.0, 0.0, -2.0],
        "scale": [1.0, 1.0, 1.0],
        "rotation": [1.0, 0.0, 0.0, 0.0]
      }
    }
  ],
  "app": {}
}
```

- `scene` is relative to the asset root, under the same rule as model paths
  (`assetPathValid`): no root, no `..`.
- `objects` holds one entry per scene object whose transform is worth
  restoring, matched back by `id` against `roboslop::SceneIdentity`. An id the
  scene no longer has is ignored on load, so a save survives a deleted object.
- `app` is an opaque JSON object the engine never inspects. Applications put
  their own state there and version it themselves; that is what keeps this
  format from growing a key per app. Gorden stores `{version, robot, player,
  sim_time, memory}` — see [agent memory](agent-memory.md).

## API

| Function | Purpose |
|---|---|
| `validateSaveGame` | unique non-empty ids, valid transforms, relative scene path, `app` is an object |
| `saveGameToJson` / `saveGameFromJson` | in-memory conversion; the reader rejects any version but 1 |
| `loadSaveGame` / `saveSaveGame` | file I/O through `roboslop.core.json_file` (atomic write, `FileError::Missing` for an absent file) |

`saveSaveGame` validates before writing, so an invalid save never reaches disk.

## Where saves live

Saves are per-user state, not documents the user authored and not the shared
`/persist` data mount, so they go under `stateDir()`
(`$XDG_STATE_HOME/roboslop`, else `~/.local/state/roboslop`). Nothing else in
the engine dictates a file name below
`stateDir()`, so an application picks a subdirectory and a slot name under it.
Gorden uses `stateDir()/gorden/saves/<slot>.json`, with `default` as the slot.

Saving and loading is explicit: the **Save game** / **Load game** buttons in
Gorden's Settings window, or `save [slot]` / `load [slot]` in its terminal.
There is no autosave — a run is kept only when someone asks for it. Both paths
raise a request that the render pass carries out, because rebuilding the scene
needs the `AssetCache`, which only a pass has.

Loading rebuilds the scene through `SceneRuntime::replace` with the saved
transforms written over the authored document, rather than writing transforms
into the live entities: that is what keeps the physics bodies where the objects
are.
