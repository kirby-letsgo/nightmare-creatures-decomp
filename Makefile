PYTHON := .venv/bin/python
CLANG_FORMAT := .venv/bin/clang-format

.PHONY: setup extract sigs configs split recomp state build build-native build-watch build-interp lint format verify shadow clean

setup:
	python3 -m venv .venv
	$(PYTHON) -m pip install -r requirements.txt

extract:
	$(PYTHON) tools/extract.py

sigs:
	$(PYTHON) tools/fetch_sigs.py

configs:
	$(PYTHON) tools/gen_splat.py
	$(PYTHON) tools/name_bios.py
	$(PYTHON) tools/match_psyq.py

split:
	for y in config/splat/*.yaml; do $(PYTHON) -m splat split $$y || exit 1; done

recomp:
	$(PYTHON) tools/recomp/recomp.py
	$(MAKE) state

state:
	$(PYTHON) tools/decomp_state.py

build:
	cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
	cmake --build build/cmake

# Release build: no recompiled code. Handwritten routines (config/splices `entry` rows) run
# natively and everything else is interpreted from the disc.
build-native:
	cmake -S . -B build/cmake-native -DCMAKE_BUILD_TYPE=Release -DNC_INTERPRETER_ONLY=ON
	cmake --build build/cmake-native

# Reference build: everything interpreted from the disc, handwritten routines unused.
build-interp:
	cmake -S . -B build/cmake-interp -DCMAKE_BUILD_TYPE=Release -DNC_INTERPRETER_ONLY=ON \
		-DNC_NATIVE_DECOMP=OFF
	cmake --build build/cmake-interp

# Debug build with memory write watchpoints (NC_WATCH=<address>); slower.
build-watch:
	cmake -S . -B build/cmake-watch -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNC_WATCHPOINTS=ON
	cmake --build build/cmake-watch

lint:
	$(PYTHON) -m ruff check tools
	$(PYTHON) -m ruff format --check tools
	$(CLANG_FORMAT) --dry-run --Werror $$(find src -name '*.c' -o -name '*.h' -o -name '*.cpp')

format:
	$(PYTHON) -m ruff format tools
	$(CLANG_FORMAT) -i $$(find src -name '*.c' -o -name '*.h' -o -name '*.cpp')

# Everything a change must pass before it is pushed. CI has no disc, so the equivalence
# tests and replays only run here: against the recompiled build and the release build.
verify: lint
	@test -f "roms/Nightmare Creatures.chd" || { echo "verify needs roms/Nightmare Creatures.chd"; exit 1; }
	$(PYTHON) tools/recomp/recomp.py
	$(PYTHON) tools/decomp_state.py --check-inventory
	$(PYTHON) tools/decomp_state.py
	$(MAKE) build build-native
	ctest --test-dir build/cmake -j8 --output-on-failure
	ctest --test-dir build/cmake-native -j8 --output-on-failure

# Shadow mode: run every replay on the release build with each call to a handwritten routine
# checked against the original instructions; lists the registry entries no replay reached.
shadow: build-native
	$(PYTHON) tools/test_shadow.py --build build/cmake-native

clean:
	rm -rf build/cmake
