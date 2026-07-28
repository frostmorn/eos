# Skill: Project Knowledge Maintenance

Use this skill when performing deep-dive analysis, debugging, or feature development in this repository to ensure the architectural documentation remains an accurate "source of truth."

## Instructions

1.  **Continuous Discovery**: Whenever a new technical mechanism (e.g., a specific `ioctl` command), a new structural dependency, or a previously unknown hardware mapping is uncovered during code reading, trigger this skill.
2.  **Incremental Updates**: 
    *   Do **not** rewrite the entire `Agents.md`. Instead, identify the specific section (e.g., "Core Kernel Mechanisms", "Driver Architecture") that requires updating.
    *   Add technical details discovered: include function names, structure members, or logic flows (e.g., "How `devfs_readdir` iterates through devices").
3.  **Refinement of Existing Knowledge**: If new evidence contradicts a previously recorded assumption in `Agents.md`, explicitly update the entry to reflect the corrected architectural reality.
4.  **Precision and Depth**: Ensure all additions are technical and detailed enough for an automated agent to understand the underlying implementation without needing to re-read the entire source tree.
5.  **Consistency**: Maintain the existing Markdown structure and level of detail present in `Agents.md`.
