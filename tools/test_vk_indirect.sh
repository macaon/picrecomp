#!/usr/bin/env bash
# Uses the built runtime and a real Vulkan device; no game data needed. Linux only.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
out="$root/build/test_vk_indirect"
mkdir -p "$out/runtime"
make -j8 > "$out/build.log" 2>&1
flags=(-O1 -g -fno-strict-aliasing -DPICRECOMP_RUNTIME -Iruntime -Iruntime/nvn -Wno-format)
read -r -a avflags <<< "$(pkg-config --cflags libavcodec libavformat libavutil libswscale)"
clang "${flags[@]}" "${avflags[@]}" -Dmain=picrecomp_app_main -c runtime/main.c -o "$out/main.o"
clang "${flags[@]}" "${avflags[@]}" -c tools/test_vk_indirect.c -o "$out/test.o"
mapfile -t objs < <(rg --files build/rt build/imgui -g '*.o' | sort)
keep=()
for obj in "${objs[@]}"; do
    [[ "$obj" == build/rt/main.o || "$obj" == build/rt/nvn/vulkan/exec.o ]] || keep+=("$obj")
done
clang++ -o "$out/test_vk_indirect" "$out/test.o" "$out/main.o" "${keep[@]}" \
    -fuse-ld=lld -rdynamic -lSDL3 -lavformat -lavcodec -lavutil -lswscale -lvulkan \
    $(pkg-config --libs shaderc) -lpthread -ldl -lm
PICRECOMP_DATA_ROOT="$out/runtime" PICRECOMP_HIDDEN=1 SDL_VIDEO_DRIVER=${SDL_VIDEO_DRIVER:-x11} \
    "$out/test_vk_indirect"
cat "$out/runtime/picrecomp.log"
if rg -q 'VUID-|Validation Error|validation layer unavailable' "$out/runtime/picrecomp.log"; then
    echo "Vulkan validation failed" >&2
    exit 1
fi
