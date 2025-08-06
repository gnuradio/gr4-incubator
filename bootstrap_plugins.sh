#!/bin/bash
set -e

cd "$(dirname "$0")"

for module_dir in blocks/*; do
    [ -d "$module_dir" ] || continue
    modulename=$(basename "$module_dir")
    incdir="$module_dir/include/gnuradio-4.0/$modulename"
    plugindir="$module_dir/plugin"

    if [[ ! -d "$incdir" || ! -d "$plugindir" ]]; then
        continue
    fi

    # Find all .hpp files for this module
    mapfile -t HEADERS < <(find "$incdir" -type f -name '*.hpp' | sort)
    if [ ${#HEADERS[@]} -eq 0 ]; then continue; fi

    python3 generate_plugin_meson.py \
      --headers "${HEADERS[@]}" \
      --plugin-dir "$plugindir" \
      --lib-name "Gr4${modulename^}Blocks" \
      --split \
      --parser-exe gnuradio_4_0_parse_registrations
done

echo "All codegen complete."
