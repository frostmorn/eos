# Skill: C-Code Project Analysis

Use this skill when entering a new C project or repository to build a comprehensive architectural mental model and file map.

## Instructions

1.  **Initial Mapping**: Start by using `ls -F` on the root directory to understand the high-level directory structure (e.g., `build/`, `main/`, `tools/`, `drivers/`).
2.  **File Discovery**: Use `find . -name "*.c" -o -name "*.h"` to generate a complete list of all source and header files in the project.
3.  **Entry Point Identification**: Search for functions like `main()`, `app_main()`, or hardware-specific entry hooks in the discovered file list.
4.  **Dependency Tracing**: 
    *   Identify "Global Include" headers (e.s., `includes.h` or `common.h`).
    *   Read these files to understand the global include hierarchy and dependency graph.
5.  **Architectural Probing**:
    *   Search for pattern-specific macros (e.g., `_REG_`, `_INIT_`, `_ATTR`) which often indicate driver registration or object instantiation patterns.
    *   Look for "Board" or "Config" directories to understand how the software maps to physical hardware.
6.  **Verification**: Once a hypothesis is formed (e.g., "This is a VFS-based driver model"), verify it by reading implementation files (`.c`) of core components.
