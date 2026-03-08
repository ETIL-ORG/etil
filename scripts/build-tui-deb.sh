#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# Build a self-contained .deb package for the ETIL TUI client.
#
# The package installs to /opt/etil-tui/ with a pre-built virtualenv
# containing all Python dependencies. A wrapper script is placed at
# /usr/bin/etil-tui.
#
# Usage: scripts/build-tui-deb.sh [--output DIR]
#
# Requires: python3, python3-venv, dpkg-deb

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TUI_DIR="$PROJECT_DIR/tools/mcp-client"
VERSION=$(python3 -c "exec(open('$TUI_DIR/etil_mcp_client/version.py').read()); print(__version__)")
ARCH=$(dpkg --print-architecture)
PKG_NAME="etil-tui"
OUTPUT_DIR="$HOME/workspace/packages"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --output) OUTPUT_DIR="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

mkdir -p "$OUTPUT_DIR"
TIMESTAMP=$(date -u +%Y%m%d%H%M%S)
DEB_FILE="$OUTPUT_DIR/${PKG_NAME}_${VERSION}+${TIMESTAMP}_${ARCH}.deb"
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

echo "Building $PKG_NAME $VERSION for $ARCH ..."

# --- Create directory structure ---
OPT="$STAGE/opt/etil-tui"
mkdir -p "$OPT" "$STAGE/usr/bin" "$STAGE/DEBIAN"

# --- Copy Python source ---
cp -r "$TUI_DIR/etil_mcp_client" "$OPT/"
cp "$TUI_DIR/requirements.txt" "$OPT/"

# Remove __pycache__ directories
find "$OPT" -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true

# --- Build virtualenv with dependencies ---
echo "Creating virtualenv and installing dependencies ..."
python3 -m venv "$OPT/venv"
"$OPT/venv/bin/pip" install --quiet --upgrade pip
"$OPT/venv/bin/pip" install --quiet -r "$OPT/requirements.txt"

# Remove pip/setuptools caches to reduce size
rm -rf "$OPT/venv/lib/python3.*/site-packages/pip"
rm -rf "$OPT/venv/lib/python3.*/site-packages/setuptools"
rm -rf "$OPT/venv/lib/python3.*/site-packages/pkg_resources"
find "$OPT/venv" -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true
find "$OPT/venv" -name "*.pyc" -delete 2>/dev/null || true
find "$OPT/venv" -name "*.dist-info" -type d -exec sh -c '
    for d; do
        # Keep METADATA and RECORD (needed for imports), remove the rest
        find "$d" -type f ! -name METADATA ! -name RECORD ! -name top_level.txt -delete 2>/dev/null || true
    done
' _ {} + 2>/dev/null || true

# --- Fix venv paths to be relocatable ---
# The venv hardcodes the build path; fix shebangs and pyvenv.cfg
PYTHON_VER=$(python3 -c "import sys; print(f'python{sys.version_info.major}.{sys.version_info.minor}')")
sed -i "s|$OPT/venv|/opt/etil-tui/venv|g" "$OPT/venv/bin/activate"
sed -i "s|$OPT/venv|/opt/etil-tui/venv|g" "$OPT/venv/bin/activate.csh" 2>/dev/null || true
sed -i "s|$OPT/venv|/opt/etil-tui/venv|g" "$OPT/venv/bin/activate.fish" 2>/dev/null || true
sed -i "s|$STAGE||g" "$OPT/venv/pyvenv.cfg"

# Fix shebangs in all scripts
find "$OPT/venv/bin" -type f -exec grep -l "$OPT/venv" {} + 2>/dev/null | while read -r f; do
    sed -i "s|$OPT/venv|/opt/etil-tui/venv|g" "$f"
done

# --- Ensure all installed files are world-readable ---
# dpkg installs as root; without this, users can't read the Python source
find "$OPT" -type f -exec chmod a+r {} +
find "$OPT" -type d -exec chmod a+rx {} +
find "$OPT/venv/bin" -type f -exec chmod a+rx {} +

# --- Create wrapper script ---
cat > "$STAGE/usr/bin/etil-tui" <<'WRAPPER'
#!/usr/bin/env bash
export PYTHONPATH="/opt/etil-tui${PYTHONPATH:+:$PYTHONPATH}"
exec /opt/etil-tui/venv/bin/python -m etil_mcp_client "$@"
WRAPPER
chmod 755 "$STAGE/usr/bin/etil-tui"

# --- Create DEBIAN/control ---
INSTALLED_SIZE=$(du -sk "$STAGE" | awk '{print $1}')
cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $VERSION
Section: devel
Priority: optional
Architecture: $ARCH
Depends: python3 (>= 3.12)
Installed-Size: $INSTALLED_SIZE
Maintainer: Mark Deazley <evolutionary-til-support@googlegroups.com>
Description: ETIL MCP Client TUI
 Interactive terminal client for the Evolutionary Threaded Interpretive
 Language (ETIL) MCP server. Provides a rich TUI with JSON-RPC logging,
 OAuth authentication, file management, and a built-in help browser.
 .
 Connects to an ETIL MCP server via HTTP. Self-contained Python
 virtualenv with all dependencies included.
Homepage: https://github.com/krystalmonolith/evolutionary-til
EOF

# --- Build the .deb ---
echo "Packaging ..."
dpkg-deb --build --root-owner-group "$STAGE" "$DEB_FILE"

SIZE=$(du -h "$DEB_FILE" | awk '{print $1}')
echo ""
echo "Built: $DEB_FILE ($SIZE)"
echo ""
echo "Install:   sudo dpkg -i $DEB_FILE"
echo "Run:       etil-tui --connect https://your-server.example.com/mcp"
echo "Uninstall: sudo dpkg -r $PKG_NAME"
