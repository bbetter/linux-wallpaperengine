#!/usr/bin/env bash
# Batch wallpaper diagnostic tool.
# Runs each installed wallpaper briefly, captures output, and classifies errors.
#
# Usage:
#   ./tools/diagnose-wallpapers.sh [--type scene|video|web] [--screen SCREEN] [--timeout N]
#
# Outputs (all under tools/):
#   diagnosis-checked.csv   — wallpapers that ran WITHOUT "Failed to initialize GLEW"
#                             (reliable results; real errors are meaningful here)
#   diagnosis-glew.csv      — wallpapers that showed the GLEW warning but kept running
#                             (errors are still real but GL wasn't fully available)
#   diagnosis-backlog.txt   — IDs not yet tested (resume on next run)
#
# Re-running the script skips IDs already in checked or glew (appends new results).
# To re-test an ID, remove it from its file first.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"

WORKSHOP_DIR="/media/andrii/Entertainment/SteamLibrary/steamapps/workshop/content/431960"
ASSETS_DIR="/media/andrii/Entertainment/SteamLibrary/steamapps/common/wallpaper_engine/assets"
ENGINE="$REPO_DIR/build/output/linux-wallpaperengine"
SCREEN="HDMI-A-1"
FILTER_TYPE=""
SCENE_TIMEOUT=12
VIDEO_TIMEOUT=20
WEB_TIMEOUT=15

CHECKED_CSV="$SCRIPT_DIR/diagnosis-checked.csv"
GLEW_CSV="$SCRIPT_DIR/diagnosis-glew.csv"
BACKLOG_TXT="$SCRIPT_DIR/diagnosis-backlog.txt"

CSV_HEADER="id,title,type,error_category,error_detail,fixed"

# ── Argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --type)    FILTER_TYPE="${2,,}"; shift 2 ;;
        --screen)  SCREEN="$2";          shift 2 ;;
        --timeout) SCENE_TIMEOUT="$2"; VIDEO_TIMEOUT="$2"; WEB_TIMEOUT="$2"; shift 2 ;;
        --engine)  ENGINE="$2";          shift 2 ;;
        *) echo "Unknown argument: $1"; exit 1 ;;
    esac
done

if [[ ! -f "$ENGINE" ]]; then
    echo "Error: engine binary not found at $ENGINE" >&2
    echo "Build first: cmake --build build --target linux-wallpaperengine" >&2
    exit 1
fi

# ── Initialise output files (write header only if file doesn't exist yet) ─────
[[ -f "$CHECKED_CSV" ]] || echo "$CSV_HEADER" > "$CHECKED_CSV"
[[ -f "$GLEW_CSV"    ]] || echo "$CSV_HEADER" > "$GLEW_CSV"

# ── Build set of already-processed IDs (skip on resume) ───────────────────────
declare -A DONE
for _f in "$CHECKED_CSV" "$GLEW_CSV"; do
    [[ -f "$_f" ]] || continue
    while IFS=',' read -r id _rest; do
        id="${id//\"/}"   # strip quotes
        [[ -z "$id" || "$id" == "id" ]] && continue   # skip header / empty
        DONE["$id"]=1
    done < <(tail -n +2 "$_f")
done

# ── Helper: classify log output ───────────────────────────────────────────────
classify() {
    local log="$1"
    local exit_code="$2"

    if [[ "$exit_code" == "139" ]]; then echo "crash:SIGSEGV"; return; fi
    if [[ "$exit_code" == "134" ]]; then echo "crash:SIGABRT"; return; fi

    if grep -qF "GLSL vertex unit parsing Failed:" "$log" 2>/dev/null; then
        local d; d=$(grep "GLSL vertex unit parsing Failed:" "$log" | head -1 | sed 's/.*Failed: //')
        echo "shader_parse:vert: ${d:0:120}"; return
    fi
    if grep -qF "GLSL fragment unit parsing Failed:" "$log" 2>/dev/null; then
        local d; d=$(grep "GLSL fragment unit parsing Failed:" "$log" | head -1 | sed 's/.*Failed: //')
        echo "shader_parse:frag: ${d:0:120}"; return
    fi
    if grep -qF "Program Linking Failed:" "$log" 2>/dev/null; then
        local d; d=$(grep "Program Linking Failed:" "$log" | head -1 | sed 's/.*Failed: //')
        echo "shader_link:${d:0:120}"; return
    fi
    if grep -qE "TypeError|ReferenceError|SyntaxError|Uncaught|JS exception" "$log" 2>/dev/null; then
        local d; d=$(grep -E "TypeError|ReferenceError|SyntaxError|Uncaught|JS exception" "$log" | head -1)
        echo "script_error:${d:0:120}"; return
    fi
    if grep -qF "Cannot setup image" "$log" 2>/dev/null; then
        local d; d=$(grep "Cannot setup image" "$log" | head -1)
        echo "image_setup:${d:0:120}"; return
    fi
    if grep -qE "AssetLoadException|Cannot find file|Cannot find directory" "$log" 2>/dev/null; then
        local d; d=$(grep -E "AssetLoadException|Cannot find file|Cannot find directory" "$log" | grep -v "assets folder" | head -1)
        [[ -n "$d" ]] && { echo "asset_missing:${d:0:120}"; return; }
    fi

    echo "ok:"
}

# ── CSV-safe quoting ──────────────────────────────────────────────────────────
csv_field() { local v="${1//\"/\"\"}"; echo "\"$v\""; }

# ── Main loop ─────────────────────────────────────────────────────────────────
echo "Diagnosing wallpapers in: $WORKSHOP_DIR"
echo "Engine : $ENGINE  |  Screen: $SCREEN"
echo "Results: $CHECKED_CSV  (reliable)"
echo "         $GLEW_CSV  (GLEW-partial)"
echo "Backlog: $BACKLOG_TXT"
echo ""

TMPLOG=$(mktemp /tmp/wpe-diag-XXXXXX.log)
trap 'rm -f "$TMPLOG"' EXIT

# Counters
N_CHECKED=0; N_GLEW=0; N_SKIPPED=0; N_RESUMED=0
declare -A CAT_CHECKED=() CAT_GLEW=()

# Collect all candidate IDs for backlog update at the end
declare -a ALL_IDS=()

for folder in "$WORKSHOP_DIR"/*/; do
    [[ -d "$folder" ]] || continue
    id=$(basename "$folder")
    project_json="$folder/project.json"

    [[ -f "$project_json" ]] || { echo "[$id] SKIP: no project.json"; (( N_SKIPPED++ )) || true; continue; }

    title=$(python3 -c "import json; d=json.load(open('$project_json')); print(d.get('title','?'))" 2>/dev/null || echo "?")
    wp_type=$(python3 -c "import json; d=json.load(open('$project_json')); print(d.get('type','?').lower())" 2>/dev/null || echo "?")

    # Type filter
    [[ -n "$FILTER_TYPE" && "$wp_type" != "$FILTER_TYPE" ]] && { ALL_IDS+=("$id"); continue; }

    ALL_IDS+=("$id")

    # Resume: skip already-done IDs
    if [[ -v DONE[$id] ]]; then
        (( N_RESUMED++ )) || true
        printf "[%s] %-40s — already checked, skipping\n" "$id" "${title:0:40}"
        continue
    fi

    case "$wp_type" in video) tmo=$VIDEO_TIMEOUT ;; web) tmo=$WEB_TIMEOUT ;; *) tmo=$SCENE_TIMEOUT ;; esac

    printf "[%s] %-40s (%s) ... " "$id" "${title:0:40}" "$wp_type"

    exit_code=0
    timeout -k 3s "$tmo" "$ENGINE" \
        --assets-dir "$ASSETS_DIR" \
        --screen-root "$SCREEN" \
        --noautomute \
        "$folder" \
        > "$TMPLOG" 2>&1 || exit_code=$?

    [[ "$exit_code" == "124" || "$exit_code" == "137" ]] && exit_code=0   # timeout kill = normal

    # Detect GLEW failure
    glew_fail=0
    grep -qF "Failed to initialize GLEW" "$TMPLOG" 2>/dev/null && glew_fail=1

    result=$(classify "$TMPLOG" "$exit_code")
    category="${result%%:*}"
    detail="${result#*:}"

    row="$(csv_field "$id"),$(csv_field "$title"),$(csv_field "$wp_type"),$(csv_field "$category"),$(csv_field "$detail"),$(csv_field "")"

    if [[ "$glew_fail" == "1" ]]; then
        echo "$row" >> "$GLEW_CSV"
        CAT_GLEW[$category]=$(( ${CAT_GLEW[$category]:-0} + 1 ))
        (( N_GLEW++ )) || true
        printf "GLEW-partial | %s%s\n" "$category" "${detail:+ — ${detail:0:55}}"
    else
        echo "$row" >> "$CHECKED_CSV"
        CAT_CHECKED[$category]=$(( ${CAT_CHECKED[$category]:-0} + 1 ))
        (( N_CHECKED++ )) || true
        printf "checked      | %s%s\n" "$category" "${detail:+ — ${detail:0:55}}"
    fi
done

# ── Update backlog: IDs in ALL_IDS that are not yet in DONE and not just processed ──
{
    for id in "${ALL_IDS[@]}"; do
        if [[ ! -v DONE[$id] ]]; then
            # Check if we just processed it this run
            if ! grep -qF "\"$id\"" "$CHECKED_CSV" "$GLEW_CSV" 2>/dev/null; then
                echo "$id"
            fi
        fi
    done
} > "$BACKLOG_TXT"

# ── Summary ───────────────────────────────────────────────────────────────────
backlog_count=$(wc -l < "$BACKLOG_TXT" 2>/dev/null || echo 0)

echo ""
echo "════════════════════════════════════════════════════════"
printf "  Checked (no GLEW)  : %d\n" "$N_CHECKED"
printf "  GLEW-partial        : %d\n" "$N_GLEW"
printf "  Resumed (skipped)   : %d\n" "$N_RESUMED"
printf "  Backlog remaining   : %d\n" "$backlog_count"
echo "────────────────────────────────────────────────────────"

if [[ "$N_CHECKED" -gt 0 ]]; then
    echo "  Checked breakdown:"
    for cat in ok shader_parse shader_link asset_missing image_setup script_error crash; do
        cnt="${CAT_CHECKED[$cat]:-0}"
        [[ "$cnt" -gt 0 ]] && printf "    %-20s %d\n" "$cat" "$cnt"
    done
fi

if [[ "$N_GLEW" -gt 0 ]]; then
    echo "  GLEW-partial breakdown:"
    for cat in ok shader_parse shader_link asset_missing image_setup script_error crash; do
        cnt="${CAT_GLEW[$cat]:-0}"
        [[ "$cnt" -gt 0 ]] && printf "    %-20s %d\n" "$cat" "$cnt"
    done
fi
echo "════════════════════════════════════════════════════════"
echo ""
echo "Files written:"
echo "  $CHECKED_CSV"
echo "  $GLEW_CSV"
echo "  $BACKLOG_TXT"
