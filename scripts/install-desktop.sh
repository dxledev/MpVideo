#!/usr/bin/env bash
set -euo pipefail

project_directory="$(cd -- "${BASH_SOURCE[0]%/*}/.." && pwd)"
build_directory="$project_directory/build"
install_prefix="${HOME:?}/.local"
config_home="${XDG_CONFIG_HOME:-$HOME/.config}"
apps_list="$config_home/apps.list"
config_directory="$config_home/mpvideo"
dry_run=0
desktop_id="io.github.mpvideo"
executable_name="mpvideo"
temporary_files=()

usage() {
    printf '%s\n' \
        'Usage: install-desktop.sh [--dry-run] [--build-dir DIR] [--prefix DIR]' \
        '       [--config-dir DIR] [--apps-list FILE]'
}

fail() {
    printf 'install-desktop: %s\n' "$*" >&2
    exit 1
}

absolute_path() {
    local path="$1"
    if [[ "$path" == '~/'* ]]; then
        path="$HOME/${path#\~/}"
    fi
    [[ "$path" == /* ]] || path="$PWD/$path"
    printf '%s' "${path%/}"
}

parse_options() {
    while (( $# )); do
        case "$1" in
            --dry-run) dry_run=1; shift ;;
            --help|-h) usage; exit 0 ;;
            --build-dir|--prefix|--config-dir|--apps-list)
                (( $# >= 2 )) || fail "Missing value for $1"
                local value
                value="$(absolute_path "$2")" || fail "Expected an absolute path for $1"
                case "$1" in
                    --build-dir) build_directory="$value" ;;
                    --prefix) install_prefix="$value" ;;
                    --config-dir) config_directory="$value" ;;
                    --apps-list) apps_list="$value" ;;
                esac
                shift 2 ;;
            *) fail "Unknown option: $1" ;;
        esac
    done
}

validate() {
    [[ -x "$build_directory/$executable_name" ]] || fail "Build MpVideo first: $build_directory/$executable_name"
    [[ -f "$project_directory/assets/$desktop_id.desktop" ]] || fail "Missing desktop entry asset"
    [[ -f "$project_directory/assets/$desktop_id.svg" ]] || fail "Missing application icon asset"
    [[ -f "$project_directory/assets/OMARCHY-DINO-MIT.txt" ]] || fail "Missing third-party license"
    [[ "$install_prefix" != / && -n "$install_prefix" ]] || fail 'Invalid installation prefix'
    [[ "$install_prefix" != *$'\n'* && "$install_prefix" != *'"'* && "$install_prefix" != *'\\'* && "$install_prefix" != *'%'* ]] ||
        fail 'Installation paths cannot contain newlines, quotes, backslashes, or percent signs'
    [[ "$config_directory" != *$'\n'* && "$config_directory" != *'"'* && "$config_directory" != *'\\'* && "$config_directory" != *'%'* ]] ||
        fail 'Configuration paths cannot contain newlines, quotes, backslashes, or percent signs'
    [[ ! -e "$apps_list" || -f "$apps_list" ]] || fail "App list is not a regular file: $apps_list"
    [[ ! -L "$apps_list" ]] || fail "Refusing to replace a symlinked app list: $apps_list"
    local targets=(
        "$install_prefix/share/licenses/mpvideo/OMARCHY-DINO-MIT.txt"
        "$install_prefix/bin/$executable_name"
        "$install_prefix/share/icons/hicolor/scalable/apps/$desktop_id.svg"
        "$install_prefix/share/applications/$desktop_id.desktop"
        "$install_prefix/bin/applications/$desktop_id.desktop"
    )
    local target
    for target in "${targets[@]}"; do
        [[ ! -L "$target" ]] || fail "Refusing to replace a symlink: $target"
    done
}

track_temp() {
    temporary_files+=("$1")
}

cleanup() {
    local path
    for path in "${temporary_files[@]}"; do
        /usr/bin/rm -f -- "$path"
    done
}
trap cleanup EXIT

install_atomic() {
    local source="$1"
    local destination="$2"
    local mode="$3"
    /usr/bin/mkdir -p -- "${destination%/*}"
    local temporary
    temporary="$(/usr/bin/mktemp "${destination%/*}/.mpvideo-install.XXXXXX")"
    track_temp "$temporary"
    /usr/bin/install -m "$mode" -- "$source" "$temporary"
    /usr/bin/mv -fT -- "$temporary" "$destination"
}

write_desktop_entry() {
    local generated
    generated="$(/usr/bin/mktemp "${TMPDIR:-/tmp}/mpvideo-desktop.XXXXXX")"
    track_temp "$generated"
    /usr/bin/awk -v executable="$install_prefix/bin/$executable_name" -v config="$config_directory" \
        '/^Exec=/ { print "Exec=\"" executable "\" --config-dir \"" config "\" %U"; next } { print }' \
        "$project_directory/assets/$desktop_id.desktop" > "$generated"
    install_atomic "$generated" "$install_prefix/share/applications/$desktop_id.desktop" 644
    install_atomic "$generated" "$install_prefix/bin/applications/$desktop_id.desktop" 644
}

register_app() {
    /usr/bin/mkdir -p -- "${apps_list%/*}"
    local temporary
    temporary="$(/usr/bin/mktemp "${apps_list%/*}/.mpvideo-apps.XXXXXX")"
    track_temp "$temporary"
    if [[ -f "$apps_list" ]]; then
        /usr/bin/awk -F '|' -v id="$desktop_id.desktop" '$1 != id { print }' "$apps_list" > "$temporary"
        /usr/bin/chmod --reference="$apps_list" "$temporary"
    else
        : > "$temporary"
        /usr/bin/chmod 644 "$temporary"
    fi
    printf '%s|%s|MpVideo\n' \
        "$desktop_id.desktop" \
        "$install_prefix/share/icons/hicolor/scalable/apps/$desktop_id.svg" >> "$temporary"
    /usr/bin/mv -fT -- "$temporary" "$apps_list"
}

main() {
    parse_options "$@"
    validate
    if (( dry_run )); then
        printf 'Executable: %s/bin/%s\nDesktop entry: %s/share/applications/%s.desktop\nLegacy entry: %s/bin/applications/%s.desktop\nIcon: %s/share/icons/hicolor/scalable/apps/%s.svg\nApp list: %s\n' \
            "$install_prefix" "$executable_name" "$install_prefix" "$desktop_id" \
            "$install_prefix" "$desktop_id" "$install_prefix" "$desktop_id" "$apps_list"
        printf 'License: %s/share/licenses/mpvideo/OMARCHY-DINO-MIT.txt\n' "$install_prefix"
        return
    fi
    install_atomic "$build_directory/$executable_name" "$install_prefix/bin/$executable_name" 755
    install_atomic "$project_directory/assets/$desktop_id.svg" \
        "$install_prefix/share/icons/hicolor/scalable/apps/$desktop_id.svg" 644
    install_atomic "$project_directory/assets/OMARCHY-DINO-MIT.txt" \
        "$install_prefix/share/licenses/mpvideo/OMARCHY-DINO-MIT.txt" 644
    write_desktop_entry
    register_app
    if [[ -x /usr/bin/update-desktop-database ]]; then
        /usr/bin/update-desktop-database "$install_prefix/share/applications"
    fi
    printf 'Installed MpVideo.\n'
}

main "$@"
