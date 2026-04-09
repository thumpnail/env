# envc

C99 Windows CLI tool for managing user (local) and machine (global) environment variables.

## Features

- `get`, `set`, `unset`, `list`
- Scope selection: `local`, `global`, `all` (read)
- Safety flag for global writes: `--force-global`
- Simple interactive terminal mode: `tui`
- Name suggestions when a variable is not found

## Build (CMake)

```powershell
cmake -S envc -B envc/build
cmake --build envc/build --config Release
```

Executable output is `envc/build/Release/envc.exe` (or `envc/build/envc.exe` depending on generator).

## Usage

```text
envc get <name> [--scope local|global|all]
envc set <name> <value> [--scope local|global] [--force-global]
envc unset <name> [--scope local|global] [--force-global]
envc list [--scope local|global|all]
envc tui
```

## Notes

- Global writes require Administrator rights.
- Environment updates are broadcast via `WM_SETTINGCHANGE`.
