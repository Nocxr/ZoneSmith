# Shared C/C++ build commands

GNU Make is the command entry point. CMake uses CXX as the language name for C++.

| Command | Behavior |
| --- | --- |
| `make` / `make build` | Stop this checkout's application, configure, build Release |
| `make run ARGS="..."` | Build and launch with arguments |
| `make stop` / `make kill` | Stop this checkout's executable |
| `make clean` | Stop and remove a verified build directory inside the checkout |
| `make rebuild` | Clean and build without launching |
| `make debug` / `make release` | Build the selected configuration |
| `make test` | Run existing tests; fail clearly if none exist |
| `make install` | Build and register a command for this checkout |
| `make uninstall` | Remove managed command launchers |
| `make help` | Show commands and project extensions |

Overrides: CONFIG (Release), BUILD_DIR (build), GENERATOR (Ninja), ARGS, CMAKE, CTEST, CMAKE_ARGS, BUILD_ARGS. Existing Mac build-mac defaults are preserved for Nex. For Visual Studio/multi-config generators, set EXE to the actual configuration subdirectory executable; Mac app-bundle projects use their existing bundle names. If a cache uses another generator, select that generator or use a new BUILD_DIR; do not delete an unrelated build directory.

Windows installation creates a managed command in %LOCALAPPDATA%/CxxTools/bin and adds that one directory to user PATH. It points to the executable in this checkout so runtime DLLs, modules and assets stay together. Open a new terminal after installing. Reinstall after moving the checkout. Unix installation creates a link under ~/.local/bin; that directory must be on PATH. Uninstall preserves shared PATH entries used by other tools.

Use the root Makefile and scripts as the standard for new C/C++ projects. Repository AGENTS.md preserves this command format for future work. The connector cannot change global Codex instructions on a local computer.

Validation: Release builds of the standalone app and its core library succeeded with GCC 15.2 on Windows. Dashboard's two plugin adapters also compiled and linked against the extracted cores and existing module ABI. Existing ZoneSmith aggregate-initializer warnings remain. No applications were launched or PATH entries changed.
