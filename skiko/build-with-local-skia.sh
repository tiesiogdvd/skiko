#!/usr/bin/env bash
# Build Skia locally and publish Skiko against it into build/flow-repo (never ~/.m2)
#
# This script handles git operations and delegates configuration to Gradle.
# For more control, use Gradle tasks directly:
#   ./gradlew prepareLocalSkiaBuild -Pskia.repo.dir=/path/to/skia
#   ./gradlew publishToFlowRepo -Pskia.dir=/path/to/skia
#
# Environment variables:
#   SKIA_VERSION   - Skia version to build (default: from gradle.properties)
#   SKIA_TARGET    - Target platform: ios, iosSim, macos, windows, linux, wasm (default: current OS)
#   SKIA_REPO_DIR  - Skia repository directory with tools/skia_release/ (default: ./skia)
#   SKIA_REPO_URL  - Skia repository to clone (default: Flow's fork, tiesiogdvd/skia; its
#                    flow/m<milestone> branch is checked out at the commit the version names)
#   SKIA_DIR       - Skia source directory for publishing (default: $SKIA_REPO_DIR)

set -e  # Exit on error
cd "$(dirname "$0")"
SCRIPT_DIR="$(pwd)"

# Use provided Skia repository directory or default
SKIA_REPO_DIR="${SKIA_REPO_DIR:-${SKIA_PACK_DIR:-$SCRIPT_DIR/skia}}"
SKIA_REPO_URL="${SKIA_REPO_URL:-https://github.com/tiesiogdvd/skia.git}"

# Clone if needed: the version m<milestone>-egl-<commit> is that commit of flow/m<milestone>
if [ ! -d "$SKIA_REPO_DIR" ]; then
    CLONE_VERSION="${SKIA_VERSION:-$(./gradlew -q printSkiaVersion)}"
    echo "Cloning skia repository to $SKIA_REPO_DIR..."
    git clone --branch "flow/${CLONE_VERSION%%-*}" "$SKIA_REPO_URL" "$SKIA_REPO_DIR"
    git -C "$SKIA_REPO_DIR" checkout "${CLONE_VERSION##*-}"
fi

# Convert to absolute path if relative
if [[ "$SKIA_REPO_DIR" != /* ]]; then
    SKIA_REPO_DIR="$(cd "$SKIA_REPO_DIR" && pwd)"
fi

echo "Using skia repository: $SKIA_REPO_DIR"
cd "$SKIA_REPO_DIR"

# Get version from Gradle (respects SKIA_VERSION env var)
SKIA_VERSION=$(cd "$SCRIPT_DIR" && ./gradlew -q printSkiaVersion)
echo "Using Skia version: $SKIA_VERSION"

# Build Skia binaries
cd "$SCRIPT_DIR"
echo "Building Skia binaries with Gradle..."
./gradlew prepareLocalSkiaBuild -Pskia.repo.dir="$SKIA_REPO_DIR"

# Publish Skiko to Maven Local with the built Skia binaries
# If SKIA_DIR is not explicitly set, use the same checkout that contains the
# integrated release scripts and the built outputs.
if [ -z "$SKIA_DIR" ]; then
    SKIA_DIR="$SKIA_REPO_DIR"
fi

echo "Publishing Skiko to build/flow-repo..."
echo "Using Skia source directory: $SKIA_DIR"
./gradlew publishToFlowRepo -Pskia.dir="$SKIA_DIR"

echo "Successfully published Skiko to $SCRIPT_DIR/build/flow-repo"
