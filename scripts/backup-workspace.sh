#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh"

BACKUP_SOURCE="${BACKUP_SOURCE:-$ETIL_WORKSPACE_DIR}"
BACKUP_DEST="${BACKUP_DEST:-/tmp}"

TIMESTAMP="$(date +%Y%m%dT%H%M%S)"
ARCHIVE_NAME="${TIMESTAMP}-workspace-backup.tar.gz"
ARCHIVE_PATH="${BACKUP_DEST}/${ARCHIVE_NAME}"

# Validate
if [[ ! -d "$BACKUP_SOURCE" ]]; then
    echo "Error: source directory does not exist: $BACKUP_SOURCE" >&2
    exit 1
fi
if [[ ! -d "$BACKUP_DEST" ]]; then
    echo "Error: destination directory does not exist: $BACKUP_DEST" >&2
    exit 1
fi
if [[ -e "$ARCHIVE_PATH" ]]; then
    echo "Error: archive already exists: $ARCHIVE_PATH" >&2
    exit 1
fi

echo "Backing up: $BACKUP_SOURCE"
echo "Writing to: $ARCHIVE_PATH"

tar -czf "$ARCHIVE_PATH" -C "$BACKUP_SOURCE" \
    --exclude='./build' \
    --exclude='./build-debug' \
    --exclude='./evolutionary-til/cmake-build-*' \
    --exclude='./evolutionary-til/out' \
    --exclude='./evolutionary-til/_deps' \
    --exclude='./evolutionary-til/tools/mcp-client/venv' \
    --exclude='./evolutionary-til/.ssh' \
    --exclude='./.idea' \
    --exclude='./.vscode' \
    --exclude='./.claude' \
    --exclude='./evolutionary-til/.idea' \
    --exclude='./evolutionary-til/.vscode' \
    --exclude='./evolutionary-til/.claude' \
    --exclude='*.o' \
    --exclude='*.so' \
    --exclude='*.a' \
    --exclude='*.exe' \
    --exclude='*.dll' \
    --exclude='*.dylib' \
    --exclude='*.log' \
    --exclude='*.pdf' \
    --exclude='*.swp' \
    --exclude='*.swo' \
    --exclude='*~' \
    --exclude='.DS_Store' \
    --exclude='*.tmp' \
    --exclude='*.bak' \
    --exclude='*.orig' \
    --exclude='*.lst' \
    --exclude='*.pyc' \
    --exclude='*.pyo' \
    --exclude='*/__pycache__' \
    --exclude='CMakeCache.txt' \
    --exclude='cmake_install.cmake' \
    --exclude='compile_commands.json' \
    --exclude='CTestTestfile.cmake' \
    --exclude='*/Testing' \
    .

SIZE="$(du -h "$ARCHIVE_PATH" | cut -f1)"
COUNT="$(tar -tzf "$ARCHIVE_PATH" | wc -l)"

echo ""
echo "Done: $ARCHIVE_PATH"
echo "Size: $SIZE"
echo "Files: $COUNT"
