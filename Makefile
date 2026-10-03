PYTHON := .venv/bin/python

.PHONY: setup extract sigs configs split recomp build build-watch build-interp lint format clean

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

build:
	cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
	cmake --build build/cmake

# Release-style build: no recompiled code, everything interpreted from the disc.
build-interp:
	cmake -S . -B build/cmake-interp -DCMAKE_BUILD_TYPE=Release -DNC_INTERPRETER_ONLY=ON
	cmake --build build/cmake-interp

# Debug build with memory write watchpoints (NC_WATCH=<address>); slower.
build-watch:
	cmake -S . -B build/cmake-watch -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNC_WATCHPOINTS=ON
	cmake --build build/cmake-watch

lint:
	$(PYTHON) -m ruff check tools
	$(PYTHON) -m ruff format --check tools
	clang-format --dry-run --Werror $$(find src -name '*.c' -o -name '*.h' -o -name '*.cpp')

format:
	$(PYTHON) -m ruff format tools
	clang-format -i $$(find src -name '*.c' -o -name '*.h' -o -name '*.cpp')

clean:
	rm -rf build/cmake
