#!/usr/bin/env bash
set -euo pipefail
android_root="$(cd "$(dirname "$0")/.." && pwd)"
test_classes="$(mktemp -d)"
trap 'rm -rf "$test_classes"' EXIT
java_sources=(
    "$android_root/app/src/main/java/com/bblueskyy/lasrecomp/GameFiles.java"
    "$android_root/tests/GameFilesTest.java"
    "$android_root/app/src/main/java/com/bblueskyy/lasrecomp/EmbeddedAssets.java"
    "$android_root/tests/EmbeddedAssetsTest.java"
)
if command -v javac >/dev/null 2>&1; then
    javac -d "$test_classes" "${java_sources[@]}"
else
    java com.sun.tools.javac.Main -d "$test_classes" "${java_sources[@]}"
fi
java -cp "$test_classes" com.bblueskyy.lasrecomp.GameFilesTest

java -cp "$test_classes" com.bblueskyy.lasrecomp.EmbeddedAssetsTest
