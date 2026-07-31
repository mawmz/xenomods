# Changelog

## v1.0.0

- Redesigned the TAS interface with persistent Utility, Telemetry, Trigger, Warp, and frame-counter windows.
- Added tutorial, cutscene, landmark, radial, and collection-point trigger visualization.
- Added player telemetry, collision information, frame counting, and combat-AI inspection tools.
- Added input buffering, camera unlock controls, and camera-aware Warps.
- Added direct save reloading and tutorial-repeat controls.
- Improved trigger rendering, visibility, materials, and performance.

## v0.2.0-debug

Added more TAS and debugging tools.

- Visualize landmark triggers.
- Reload the current save without returning through the title screen.
- Added a persistent player telemetry overlay:
  - Position.
  - Velocity and speed.
  - Facing angle.
  - Grounded or airborne state.
  - Wall contact.
- Added camera unlock controls:
  - Player 2 can move the displayed camera independently.
  - Player 1's movement direction remains unchanged.
- Improved trigger rendering and visibility.
- Removed shadows from rendered trigger zones.
- Fixed movement-state and wall-contact detection.

## v0.1.0

Added some initial TASing tools.

- Visualize tutorial triggers.
- Visualize cutscene triggers.
- Added camera position saving to Warps.
