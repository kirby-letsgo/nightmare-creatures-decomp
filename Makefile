PYTHON := .venv/bin/python

.PHONY: setup extract build lint format clean

setup:
	python3 -m venv .venv
	$(PYTHON) -m pip install -r requirements.txt

extract:
	$(PYTHON) tools/extract.py

build:
	cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Debug
	cmake --build build/cmake

lint:
	$(PYTHON) -m ruff check tools
	$(PYTHON) -m ruff format --check tools
	clang-format --dry-run --Werror $$(find src -name '*.c' -o -name '*.h')

format:
	$(PYTHON) -m ruff format tools
	clang-format -i $$(find src -name '*.c' -o -name '*.h')

clean:
	rm -rf build/cmake
