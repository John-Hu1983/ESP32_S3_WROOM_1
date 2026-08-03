from pathlib import Path
import importlib
import re
from typing import List, Tuple

try:
    _scons_script = importlib.import_module("SCons.Script")
except ModuleNotFoundError:
    _scons_script = None

if _scons_script is None:
    Import = None

    def Exit(code=0):
        raise SystemExit(code)
else:
    Exit = _scons_script.Exit
    Import = _scons_script.Import

file_name = globals().get("__file__")
if isinstance(file_name, str):
    fallback_project_dir = Path(file_name).resolve().parents[1]
else:
    fallback_project_dir = Path.cwd()

env = {"PROJECT_DIR": str(fallback_project_dir)}
if (Import is not None) and ("COMMAND_LINE_TARGETS" in globals()):
    Import("env")

# Enforce project style: data type definitions belong in headers, not C source files.
TYPEDEF_PATTERN = re.compile(r"^\s*typedef\s+(struct|enum|union)\b")

project_dir_value = env.get("PROJECT_DIR") if hasattr(env, "get") else None
if not project_dir_value:
    project_dir_value = str(fallback_project_dir)

PROJECT_DIR = Path(project_dir_value)
SRC_DIR = PROJECT_DIR / "src"

IGNORE_SUBPATHS = {
    Path("desktop") / "lvgl_launcher",
}


def should_skip(path: Path) -> bool:
    try:
        rel = path.relative_to(SRC_DIR)
    except ValueError:
        return True

    for ignored in IGNORE_SUBPATHS:
        if rel == ignored or ignored in rel.parents:
            return True

    return False


def find_violations() -> List[Tuple[Path, int, str]]:
    violations_local: List[Tuple[Path, int, str]] = []

    if not SRC_DIR.exists():
        return violations_local

    for c_file in SRC_DIR.rglob("*.c"):
        if should_skip(c_file):
            continue

        try:
            lines = c_file.read_text(encoding="utf-8", errors="ignore").splitlines()
        except OSError:
            continue

        for line_idx, line_text in enumerate(lines, start=1):
            if TYPEDEF_PATTERN.search(line_text):
                rel_path = c_file.relative_to(PROJECT_DIR)
                violations_local.append((rel_path, line_idx, line_text.strip()))

    return violations_local


found_violations = find_violations()
if found_violations:
    print("\n[typedef-check] ERROR: Found forbidden typedef in .c file(s).")
    print("[typedef-check] Please move these type definitions to the corresponding .h file.\n")
    for v_rel_path, v_line_no, v_line in found_violations:
        print(f"  - {v_rel_path}:{v_line_no}: {v_line}")
    print("")
    Exit(1)

print("[typedef-check] PASS: no typedef struct/enum/union found in project .c files.")
