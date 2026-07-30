# AI & Blueprint Rules

## AI Module — First-Class Citizen
- `src/ai/` exists from day 1 with types and context, even as skeletons
- Skeleton types define the interface that will be implemented later
- Breaking changes to AI types are BREAKING CHANGES (requires migration plan)

## AI Context Protocol — HARD RULE
- AI module NEVER imports ECS, Renderer, Blueprint, or other subsystems directly
- Communication is ONE-WAY through a serialized `ContextSnapshot`:
  ```
  Engine → serializes ContextSnapshot → AI reads it → AI produces actions/code
  ```
- The snapshot is a plain data struct, not a reference to live engine state

## Blueprint VM
- VM interface defined BEFORE implementation (header/API-first)
- Blueprint → Zig compiler MUST produce readable, valid Zig code
- No "black box" compilation — user can read and debug the generated code
- Blueprint nodes are type-checked at edit-time, not runtime

## Integration Points
- AI agent context includes: ECS world state, available Blueprint nodes, scene graph
- Blueprint nodes can call AI-generated functions
- AI can generate new Blueprint sub-graphs from natural language
