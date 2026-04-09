set shell := ["pwsh", "-NoLogo", "-Command"]

project := "env/env.csproj"

# Build the env CLI project.
build:
    dotnet build {{project}}

# Run the env CLI (pass args with: just run -- <args>)
run *args:
    dotnet run --project {{project}} -- {{args}}

# Run tests in the solution (if/when tests exist).
test:
    dotnet test env-the-enviroment-tool.sln

# Clean build artifacts for the project.
clean:
    dotnet clean {{project}}

# Restore NuGet packages.
restore:
    dotnet restore {{project}}

# Configure CMake build files for the C99 envc project.
envc-configure:
    cmake -S envc -B envc/build

# Build the C99 envc executable.
envc-build: envc-configure
    cmake --build envc/build --config Release

# Run envc from the CMake build directory (pass args with: just envc-run -- <args>)
envc-run *args: envc-build
    ./envc/build/envc.exe {{args}}
