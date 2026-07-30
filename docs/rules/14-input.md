# Input Abstraction Rules

## Architecture
```
InputState (platform-agnostic)
  └─ InputAction — logical action (ACTION_JUMP, ACTION_MOVE)
       └─ InputBinding — physical trigger (keyboard key, mouse button, gamepad axis)
            └─ InputSource — platform backend (X11, Win32, evdev)
```

## Action Mapping Layer
- Game code NEVER checks raw key codes — it queries actions
- Actions are defined as a comptime enum, not strings
- Bindings are configurable (key remapping), not hardcoded

```zig
// ✅ Game code
if (input.isPressed(.action_jump)) { ... }
if (input.axis(.action_move_x)) |x| { ... }

// ❌ Raw key check in game code
if (x11_state.keys[XKB_KEY_space]) { ... }
```

## Action Types
| Type | Behavior | Example |
|---|---|---|
| Pressed | True the frame key goes down | Jump, Shoot |
| Released | True the frame key goes up | Release trigger |
| Held | True every frame while down | Run, Crouch |
| Axis | Continuous value [-1..1] | Move, Look, Throttle |
| Delta | Accumulated change since last frame | Mouse look, Scroll |

## Binding System
- Default bindings defined in `config/default_bindings.zig`
- User bindings stored as engine config, not in a separate file
- Multiple bindings per action (e.g., Space + A button both map to `ACTION_JUMP`)

## Platform Backend
- `platform/input.zig` — public interface (InputState struct, action queries)
- `platform/input_x11.zig` — X11 keystroke → InputState translation
- Input backends are initialized by the platform context, not manually

## Prohibited
- ❌ Platform-specific input codes outside `platform/` directory
- ❌ String-based action lookup (costly and error-prone)
- ❌ Hardcoded key bindings in game code
- ❌ Polling raw input state from systems (go through action queries)
