# Math Conventions

## Coordinate System
| Property | Convention |
|---|---|
| Up axis | +Y |
| Handedness | Right-handed |
| Forward | -Z |
| Unit | Meters (1 unit = 1 meter) |

## Matrices
- **Column-major** layout (matching GLM, DirectXMath layout)
- Column index first in API: `mat4.data[col][row]`
- Transform order: **Scale → Rotate → Translate** (SRT)
- Matrix multiplication: left-to-right reading of operations
  - `result = translate * rotate * scale` → first scale, then rotate, then translate

```zig
// ✅ Column-major indexing
pub const Mat4 = struct {
    data: [4][4]f32,  // data[col][row]
    // ...
};
```

## Transforms
- `Transform` type stores: position (vec3), rotation (quat), scale (vec3)
- Rotation order for Euler conversions: **Yaw then Pitch then Roll** (YPR)
- Euler angles are DEGREES in API, RADIANS internally
- Use quaternions for runtime rotation — Euler only for editor/import

## Angles
| Context | Unit | Example |
|---|---|---|
| API / user input | Degrees | `rotateX(45.0)` |
| Internal runtime | Radians | stored as radians in quat/matrix |
| Conversion | `core/math.zig` | `degreesToRadians`, `radiansToDegrees` |

## Quaternions
- Stored as `[x, y, z, w]`
- w is the real component (last position)
- Identity: `[0, 0, 0, 1]`

## Prohibited
- ❌ Mixing conventions in the same subsystem
- ❌ Degrees vs Radians confusion — convert at the API boundary only
- ❌ Hardcoded projection matrices without FOV/aspect parameters
