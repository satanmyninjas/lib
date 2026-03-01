#!/usr/bin/env bash

###############################################################################
# archive_push.sh
#
# Moves files from ARCHIVE_DIR to CURRENT_DIR in batches of 10,
# committing and pushing each batch.
#
# Supports:
#   --archive=<path>
#   --current=<path>     (supports ".", "..", relative paths)
#   --message="msg"
#   --dry-run | --safety=on
###############################################################################

set -euo pipefail

BATCH_SIZE=10
DRY_RUN=0

ARCHIVE_DIR=""
CURRENT_DIR=""
USER_MESSAGE=""

# ----------------------------
# Argument Parsing
# ----------------------------
for arg in "$@"; do
    case "$arg" in
        --archive=*)
            ARCHIVE_DIR="${arg#*=}"
            ;;
        --current=*)
            CURRENT_DIR="${arg#*=}"
            ;;
        --message=*)
            USER_MESSAGE="${arg#*=}"
            ;;
        --dry-run|--safety=on)
            DRY_RUN=1
            ;;
        *)
            echo "[ERROR] Unknown argument: $arg"
            exit 1
            ;;
    esac
done

# ----------------------------
# Validation
# ----------------------------
if [[ -z "$ARCHIVE_DIR" || -z "$CURRENT_DIR" || -z "$USER_MESSAGE" ]]; then
    echo "[INFO] Usage:"
    echo "  $0 --archive=<path> --current=<path> --message=\"msg\" [--dry-run]"
    exit 1
fi

# Normalize paths (supports ., .., relative paths)
ARCHIVE_DIR="$(realpath "$ARCHIVE_DIR")"
CURRENT_DIR="$(realpath "$CURRENT_DIR")"

if [[ ! -d "$ARCHIVE_DIR" ]]; then
    echo "[ERROR] ARCHIVE_DIR does not exist."
    exit 1
fi

if [[ ! -d "$CURRENT_DIR" ]]; then
    echo "[ERROR] CURRENT_DIR does not exist."
    exit 1
fi

# ----------------------------
# Command Executor
# ----------------------------
run_cmd() {
    if [[ "$DRY_RUN" -eq 1 ]]; then
        printf '[DRY-RUN] %q ' "$@"
        printf '\n'
    else
        "$@"
    fi
}

# ----------------------------
# Main Processing Loop
# ----------------------------
batch_number=1

while true; do
    mapfile -t files < <(
        find "$ARCHIVE_DIR" -maxdepth 1 -type f | sort | head -n "$BATCH_SIZE"
    )

    if [[ "${#files[@]}" -eq 0 ]]; then
        echo "[DONE] No more files to move to current directory."
        break
    fi

    echo "[*] Processing batch $batch_number (${#files[@]} files)"

    for file in "${files[@]}"; do
        base_name="$(basename "$file")"
        run_cmd mv "$file" "$CURRENT_DIR/$base_name"
    done

    cd "$CURRENT_DIR"

    run_cmd git add .
    run_cmd git commit -m "$USER_MESSAGE (batch $batch_number)"
    run_cmd git push

    batch_number=$((batch_number + 1))
done

echo "[DONE] Archival process complete."
