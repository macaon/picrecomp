# PicRecomp build
# Windows: MSYS2 UCRT64 shell (see docs/BUILD-windows.md), clang + Vulkan renderer.
# Linux: clang + Vulkan renderer, x86-64 or aarch64 (see docs/linux.md).
# Windows from Linux: WINCROSS=1 (tools/windows/build.sh): zig cc for x86_64-windows-gnu, deps from tools/windows/fetch_deps.sh.
UNAME := $(shell uname -s)
comma := ,
ifeq ($(WINCROSS),1)
PLATFORM := windows
else ifneq ($(filter MINGW% MSYS% CYGWIN% Windows_NT,$(UNAME) $(OS)),)
PLATFORM := windows
else ifeq ($(UNAME),Linux)
PLATFORM := linux
else
$(error PicRecomp builds on Linux and Windows)
endif

# Linux: ARCH (x86_64 / aarch64, default this machine's) picks the target, separately from PLATFORM. The generated code
# differs per arch (AArch64 hosts run the guest's FP/SIMD as inline asm, others use the portable C of simd.h), so a
# cross build has its own generated-<arch>/ and build/<arch>/; SYSROOT is the target's root filesystem (headers, libs).
GEN      ?= generated
ifeq ($(PLATFORM),linux)
HOSTARCH := $(patsubst arm64,aarch64,$(shell uname -m))
ARCH     ?= $(HOSTARCH)
ifneq ($(ARCH),$(HOSTARCH))
CROSS    := --target=$(ARCH)-linux-gnu $(if $(SYSROOT),--sysroot=$(SYSROOT))
GEN      := generated-$(ARCH)
B        ?= build/$(ARCH)
endif
GENSIMD  := --simd $(if $(filter aarch64,$(ARCH)),asm,c)
endif

# B: build dir. DEPS: prefix with SDL3 / FFmpeg / shaderc instead of the system's.
B         ?= build
DEPS      ?=
CC       := clang $(CROSS)
CXX      := clang++ $(CROSS)
ifeq ($(WINCROSS),1)
ZIG      ?= build/zig-full/zig
CC       := $(ZIG) cc -target x86_64-windows-gnu
CXX      := $(ZIG) c++ -target x86_64-windows-gnu
GENARCH  ?= -mcpu=x86_64_v3  # (zig's spelling of x86-64-v3)
endif
IMGUI    := third_party/imgui
GENFLAGS := -O1 -fno-strict-aliasing -Wno-return-type -Wno-unused-label -fvisibility=hidden -Iruntime
# FNPROF=1: game code with the function-entry profiler's hook (PICRECOMP_FNPROF; runtime/cpu.h FN_ENTER)
GENFLAGS += $(if $(FNPROF),-DPICRECOMP_FNPROF)

# The game code (made by `make gen` from your own game files) is its own library next to the app (runtime/gamelib.h);
# the app itself holds none of it
# the game's modules and their guest bases (runtime/modules.h): name:base pairs
GAME_MODULES := $(shell sed -n 's/^ *X(\([a-z0-9]*\), *\(0x[0-9A-Fa-f]*\)ull).*/\1:\2/p' runtime/modules.h)
GEN_SRC  := $(foreach m,$(GAME_MODULES),$(wildcard $(GEN)/$(firstword $(subst :, ,$(m)))/*.c))
GEN_OBJ  := $(patsubst %.c,$(B)/obj/%.o,$(patsubst $(GEN)/%,%,$(GEN_SRC))) $(B)/obj/tools/gamelib_entry.o
RT_SRC   := $(wildcard runtime/*.c) $(wildcard runtime/hle/*.c) $(wildcard runtime/services/*.c) \
            $(wildcard runtime/nvn/*.c) $(wildcard runtime/nvn/maxwell/*.c) runtime/platform/$(PLATFORM).c
RT_HDRS  := $(wildcard runtime/*.h) $(wildcard runtime/platform/*.h) $(wildcard runtime/nvn/*.h) \
            $(wildcard runtime/nvn/maxwell/*.h) $(wildcard runtime/nvn/vulkan/*.h) $(wildcard runtime/ui/*.h)
UI_SRC   := $(wildcard runtime/ui/*.cpp)
UI_OBJ   := $(patsubst runtime/%.cpp,$(B)/rt/%.o,$(UI_SRC))
IMGUI_OBJ := $(B)/imgui/imgui.o $(B)/imgui/imgui_draw.o $(B)/imgui/imgui_tables.o $(B)/imgui/imgui_widgets.o \
             $(B)/imgui/imgui_impl_sdl3.o

ifeq ($(PLATFORM),linux)
EXE      := $(B)/picrecomp
# (pkg-config: Fedora keeps FFmpeg's headers in /usr/include/ffmpeg; Debian / Ubuntu in /usr/include, where it adds nothing)
DEPINC   := $(if $(DEPS),-I$(DEPS)/include) $(shell pkg-config --cflags libavcodec libavformat libavutil libswscale 2>/dev/null)
ifeq ($(ARCH),x86_64)
GENARCH  ?= -march=x86-64-v3  # as on Windows: AVX2 + FMA for the lifted FP/SIMD code (checked at startup)
endif
GENFLAGS += $(GENARCH) -fPIC
OSFLAGS  := -Wno-format  # (%llx for uint64_t, which is unsigned long here: the same size)
SHADERC  ?= $(or $(shell pkg-config --libs shaderc 2>/dev/null),-lshaderc)  # (Fedora: -lshaderc_shared; override with SHADERC=...)
# -rdynamic: crash backtraces name host functions (runtime/platform/linux.c)
LDFLAGS  := -fuse-ld=lld -rdynamic $(if $(DEPS),-L$(DEPS)/lib) -lSDL3 -lavformat -lavcodec -lavutil -lswscale \
            -lvulkan $(SHADERC) -lpthread -ldl -lm
BACKEND_SRC := $(wildcard runtime/nvn/vulkan/*.c) $(wildcard runtime/nvn/vulkan/*.cpp)
BACKEND_OBJ := $(patsubst runtime/%.cpp,$(B)/rt/%.o,$(patsubst runtime/%.c,$(B)/rt/%.o,$(BACKEND_SRC)))
IMGUI_OBJ += $(B)/imgui/imgui_impl_vulkan.o
GAMELIB  := $(B)/picgame.so
GAMELIB_LDFLAGS := -shared -fuse-ld=lld  # (the runtime functions it calls: the app's own, -rdynamic)
GAMELIB_SHARED := -shared
else
EXE      := $(B)/picrecomp.exe
DEPINC   := $(if $(DEPS),-I$(DEPS)/include)
OSFLAGS  := -D_FILE_OFFSET_BITS=64 -D_POSIX_THREAD_SAFE_FUNCTIONS -D_CRT_RAND_S -DWIN32_LEAN_AND_MEAN -DNOMINMAX
GENARCH  ?= -march=x86-64-v3  # AVX2 + FMA for the lifted FP/SIMD code (simd.h); override for older CPUs
GENFLAGS += -D_FILE_OFFSET_BITS=64 $(GENARCH)
LDFLAGS  := -fuse-ld=lld -Wl,--stack,16777216 $(if $(DEPS),-L$(DEPS)/lib) -lSDL3 -lavformat -lavcodec -lavutil -lswscale \
            -lvulkan-1 -lwinmm -lole32 -lshell32 -lcomdlg32 -luuid -lshaderc_shared \
            $(if $(RELEASE),-Wl$(comma)--subsystem$(comma)windows)  # release: no console window (log -> picrecomp.log)
BACKEND_SRC := $(wildcard runtime/nvn/vulkan/*.c) $(wildcard runtime/nvn/vulkan/*.cpp)
BACKEND_OBJ := $(patsubst runtime/%.cpp,$(B)/rt/%.o,$(patsubst runtime/%.c,$(B)/rt/%.o,$(BACKEND_SRC)))
BACKEND_OBJ += $(if $(wildcard docs/icon/icon.ico),$(B)/rt/platform/windows_rc.o)  # the .exe's icon (none yet)
WINDRES  ?= windres
IMGUI_OBJ += $(B)/imgui/imgui_impl_vulkan.o
GAMELIB  := $(B)/picgame.dll
# the runtime functions it calls (RT_API in runtime/cpu.h): exported by the .exe, imported through an import library
# made from tools/windows/picrecomp.def
IMPLIB   := $(B)/picrecomp.lib
GAMELIB_LDFLAGS := -shared -fuse-ld=lld $(IMPLIB)
GAMELIB_SHARED := -shared  # (+ the import library: runtime/gamebuild.c)
endif

# GAMELIB_ID: what the game code library is built from (the generator, the headers it is compiled against, the
# flags). A library with another id is rebuilt by the start screen, never loaded.
GAMELIB_SRCS := tools/gen.py tools/lifter.py tools/analyze.py tools/nso.py tools/passes.py tools/patches.py \
                tools/simd_c.py tools/gamelib_entry.c runtime/hle/hle_symbols.txt \
                runtime/cpu.h runtime/simd.h runtime/simd_fp.h runtime/gamelib.h runtime/modules.h
GAMELIB_ID := $(shell (cat $(GAMELIB_SRCS); echo '$(GENFLAGS)') | (shasum -a 256 2>/dev/null || sha256sum) | cut -c1-16)

RT_OBJ   := $(patsubst runtime/%.c,$(B)/rt/%.o,$(RT_SRC))
RTFLAGS  := -O2 -g -fno-strict-aliasing -Wall -Wno-unused-function -DPICRECOMP_RUNTIME -Iruntime -Iruntime/nvn $(DEPINC) $(OSFLAGS)
CXXFLAGS := -O2 -g -std=c++17 -DPICRECOMP_RUNTIME -I$(IMGUI) -I$(IMGUI)/backends -Iruntime -Iruntime/nvn $(DEPINC)

all: $(EXE) $(if $(GEN_SRC),$(GAMELIB))

ifeq ($(PLATFORM),linux)
HOSTCC ?= gcc
HACTOOL_SRC := $(shell find third_party/hactool -type f)
all: $(B)/tools/hactool

.PHONY: hactool
hactool: $(B)/tools/hactool

$(B)/tools/hactool: $(HACTOOL_SRC)
	@mkdir -p "$(@D)"
	@set -e; stage=$$(mktemp -d "$(@D)/hactool.XXXXXX"); \
		trap 'rm -rf "$$stage"' EXIT; \
		cp -R third_party/hactool/. "$$stage"; \
		cp "$$stage/config.mk.template" "$$stage/config.mk"; \
		$(MAKE) -C "$$stage" MAKEOVERRIDES= CC="$(HOSTCC)"; \
		mv "$$stage/hactool" "$@"
endif


$(B)/obj/%.o: $(GEN)/%.c runtime/cpu.h
	@mkdir -p $(dir $@)
	@$(CC) $(GENFLAGS) -c $< -o $@

$(B)/obj/tools/gamelib_entry.o: tools/gamelib_entry.c $(GAMELIB_SRCS)
	@mkdir -p $(dir $@)
	$(CC) $(GENFLAGS) -DGAMELIB_ID='"$(GAMELIB_ID)"' -c $< -o $@
$(B)/rt/gamelib.o: $(GAMELIB_SRCS)
$(B)/rt/gamelib.o: RTFLAGS += -DGAMELIB_ID='"$(GAMELIB_ID)"'
# the start screen's build of it (runtime/gamebuild.c) uses the same flags
$(B)/rt/gamebuild.o: $(GAMELIB_SRCS) Makefile
$(B)/rt/gamebuild.o: RTFLAGS += -DGAMELIB_ID='"$(GAMELIB_ID)"' -DGAMELIB_CFLAGS='"$(strip $(filter-out -Iruntime,$(GENFLAGS)))"' \
                                -DGAMELIB_SHARED='"$(GAMELIB_SHARED)"'

$(B)/rt/%.o: runtime/%.c $(RT_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(RTFLAGS) -c $< -o $@

# SMAA (third_party/smaa, MIT): the reference shader as a C string for runtime/nvn/smaa_data.c (both backends build
# their passes from it); the non-ASCII bytes in its comments become '?', and its CRLF line ends plain LF (a CR left in
# the string literals ends them early)
$(B)/inc/smaa_hlsl.inc: third_party/smaa/SMAA.hlsl
	@mkdir -p $(dir $@)
	LC_ALL=C tr -d '\r' < $< | LC_ALL=C tr '\200-\377' '?' | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/"/' -e 's/$$/\\n"/' > $@
$(B)/rt/nvn/smaa_data.o: $(B)/inc/smaa_hlsl.inc
$(B)/rt/nvn/smaa_data.o: RTFLAGS += -I$(B)/inc -Ithird_party/smaa/Textures

$(B)/rt/%.o: runtime/%.cpp $(RT_HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(OSFLAGS) -c $< -o $@

# linked to a temp file, then renamed over: a copy of the game still running keeps its (old) file - relinking in
# place gets running copies killed by macOS (code signature of the changed pages). The temp file keeps the real name
# (in a folder of its own): on Windows the import library records the name the game code library loads it by.
$(EXE): $(RT_OBJ) $(BACKEND_OBJ) $(UI_OBJ) $(IMGUI_OBJ)
	@mkdir -p $(dir $@)link
	$(CXX) -o $(dir $@)link/$(notdir $@) $^ $(LDFLAGS)
	mv -f $(dir $@)link/$(notdir $@) $@

ifeq ($(PLATFORM),windows)
$(IMPLIB): tools/windows/picrecomp.def
	@mkdir -p $(dir $@)
	$(or $(ZIG),zig) dlltool -m i386:x86-64 -D picrecomp.exe -d $< -l $@
$(GAMELIB): $(IMPLIB)
endif

# its id file (<library>.id) says what it was built from without loading it (runtime/gamelib.c)
$(GAMELIB): $(GEN_OBJ) | $(EXE)
	$(CC) $(GAMELIB_LDFLAGS) -o $@.tmp $(GEN_OBJ)
	mv -f $@.tmp $@
	echo $(GAMELIB_ID) > $@.id

$(B)/imgui/%.o: $(IMGUI)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(B)/imgui/imgui_impl_%.o: $(IMGUI)/backends/imgui_impl_%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Python: tools/venv (Linux: bin/python, Windows: Scripts/python.exe)
PY := $(firstword $(wildcard tools/venv/bin/python) $(wildcard tools/venv/Scripts/python.exe))

gen:
	for mb in $(GAME_MODULES); do m=$${mb%%:*}; b=$${mb#*:}; \
	    (cd tools && ../$(PY) gen.py $$m ../extracted/exefs/$$m $$b ../$(GEN)/$$m --hle ../runtime/hle/hle_symbols.txt $(GENSIMD)) || exit 1; \
	done

gen-objs: $(GEN_OBJ)

# Runtime objects only (no generated code, no link): quick compile check
runtime-objs: $(RT_OBJ) $(BACKEND_OBJ) $(UI_OBJ)

$(B)/rt/platform/windows_rc.o: runtime/platform/windows.rc docs/icon/icon.ico
	@mkdir -p $(dir $@)
	$(WINDRES) -I docs/icon -O coff $< -o $@

# The game code generator as a standalone helper (PyInstaller, tools/picgen.spec): build/helper/picgen/, for the start
# screen's setup on the player's machine (runtime/gamebuild.c). GEN_VENV: its venv.
GEN_PY  ?= $(if $(filter windows,$(PLATFORM)),py -3.12,python3)
GEN_VENV ?= build/gen-venv
GEN_VPY := $(if $(filter windows,$(PLATFORM)),$(GEN_VENV)/Scripts/python.exe,$(GEN_VENV)/bin/python)

gen-helper: $(GEN_VENV)/.ok
	$(if $(filter windows,$(PLATFORM)),USERPROFILE="$$(cygpath -w ~ 2>/dev/null || echo $$HOME)") \
	$(GEN_VPY) -m PyInstaller --noconfirm --clean --log-level WARN --distpath build/helper --workpath build/pyinstaller \
	    tools/picgen.spec
	$(if $(filter windows,$(PLATFORM)),$(GEN_VPY) tools/pe_no_cfg.py build/helper/picgen/picgen.exe)
	@du -sh build/helper/picgen

$(GEN_VENV)/.ok:
	$(GEN_PY) -m venv $(GEN_VENV)
	$(GEN_VPY) -m pip install -q capstone==5.0.9 unicorn==2.1.4 lz4==4.4.5 pyinstaller==6.22.3
	touch $@

.PHONY: all gen gen-helper gen-objs runtime-objs
