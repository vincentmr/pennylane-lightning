PYTHON3 := $(shell which python3 2>/dev/null)

PYTHON := python3
COVERAGE := --cov=pennylane_lightning --cov-report term-missing --cov-report=html:coverage_html_report
TESTRUNNER := -m pytest tests --tb=short

ifdef verbose
    VERBOSE := --verbose
else
    VERBOSE :=
endif

ifdef check
    CHECK := --check
else
    CHECK :=
endif

.PHONY: help
help:
	@echo "Please use \`make <target>' where <target> is one of"
	@echo "  docs                     to generate documents"
	@echo "  clean                    to delete all temporary, cache, and build files"
	@echo "  clean-docs               to delete all built documentation"
	@echo "  test                     to run the test suite"
	@echo "  test-cpp [backend=?]     to run the C++ test suite (requires CMake)"
	@echo "                           Default: lightning_qubit"
	@echo "  test-cpp [verbose=1]     to run the C++ test suite (requires CMake)"
	@echo "                           use with 'verbose=1' for building with verbose flag"
	@echo "  test-cpp [target=?]      to run a specific C++ test target (requires CMake)."
	@echo "  coverage-cpp [backend=?] to generate a coverage report for python interface"
	@echo "                           Default: lightning_qubit"
	@echo "  test-python [device=?]   to run the Python test suite"
	@echo "                           Default: lightning.qubit"
	@echo "  coverage [device=?]      to generate a coverage report for python interface"
	@echo "                           Default: lightning.qubit"
	@echo "  format [check=1]         to apply C++ and Python formatter;"
	@echo "                           use with 'check=1' to check instead of modify (requires black and clang-format)"
	@echo "  format [version=?]       to apply C++ and Python formatter;"
	@echo "                           use with 'version={version}' to check or modify with clang-format-{version} instead of clang-format"
	@echo "  check-tidy [backend=?]   to build PennyLane-Lightning with ENABLE_CLANG_TIDY=ON (requires clang-tidy & CMake)"
	@echo "                           Default: lightning_qubit"
	@echo "  check-tidy [verbose=1]   to build PennyLane-Lightning with ENABLE_CLANG_TIDY=ON (requires clang-tidy & CMake)"
	@echo "                           use with 'verbose=1' for building with verbose flag"
	@echo "  check-tidy [target=?]    to build a specific PennyLane-Lightning target with ENABLE_CLANG_TIDY=ON (requires clang-tidy & CMake)"

.PHONY : clean
clean:
	find . -type d -name '__pycache__' -exec rm -r {} \+
	rm -rf build Build BuildTests BuildTidy BuildGBench
	rm -rf .coverage coverage_html_report/
	rm -rf pennylane_lightning/*_ops*

.PHONY : test-builtin test-suite test-python coverage coverage-cpp test-cpp test-cpp-no-omp test-cpp-blas test-cpp-kokkos
test-builtin:
	PL_DEVICE=$(if $(device:-=),$(device),lightning.qubit) $(PYTHON) -I $(TESTRUNNER)

test-suite:
	pl-device-test --device $(if $(device:-=),$(device),lightning.qubit) --skip-ops --shots=20000
	pl-device-test --device $(if $(device:-=),$(device),lightning.qubit) --shots=None --skip-ops

test-python: test-builtin test-suite

coverage:
	@echo "Generating coverage report for $(if $(device:-=),$(device),lightning.qubit) device:"
	$(PYTHON) $(TESTRUNNER) $(COVERAGE)
	pl-device-test --device $(if $(device:-=),$(device),lightning.qubit) --skip-ops --shots=20000 $(COVERAGE) --cov-append
	pl-device-test --device $(if $(device:-=),$(device),lightning.qubit) --shots=None --skip-ops $(COVERAGE) --cov-append

coverage-cpp:
	@echo "Generating cpp coverage report in BuildCov/out for $(if $(backend:-=),$(backend),lightning_qubit) backend"
	rm -rf ./BuildCov
	cmake -BBuildCov  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DENABLE_COVERAGE=ON -DPL_BACKEND=$(if $(backend:-=),$(backend),lightning_qubit)
	cmake --build ./BuildCov
	cd ./BuildCov; for file in *runner ; do ./$file; done; \
	lcov --directory . -b ../pennylane_lightning/core/src --capture --output-file coverage.info; \
	genhtml coverage.info --output-directory out

build:
	rm -rf ./Build
	cmake -BBuild -DENABLE_BLAS=ON -DENABLE_KOKKOS=ON -DENABLE_WARNINGS=ON -DPL_BACKEND=$(if $(backend:-=),$(backend),lightning_qubit)
	cmake --build ./Build $(VERBOSE)

test-cpp:
	rm -rf ./BuildTests
	cmake -BBuildTests -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DENABLE_KOKKOS=ON -DENABLE_OPENMP=ON -DENABLE_WARNINGS=ON -DPL_BACKEND=$(if $(backend:-=),$(backend),lightning_qubit)
ifdef target
	cmake --build ./BuildTests $(VERBOSE) --target $(target)
	OMP_PROC_BIND=false ./BuildTests/$(target)
else
	cmake --build ./BuildTests $(VERBOSE)
	OMP_PROC_BIND=false cmake --build ./BuildTests $(VERBOSE) --target test
endif

test-cpp-blas:
	rm -rf ./BuildTests
	cmake -BBuildTests -DBUILD_TESTS=ON -DENABLE_BLAS=ON -DENABLE_WARNINGS=ON -DPL_BACKEND=$(if $(backend:-=),$(backend),lightning_qubit)
	cmake --build ./BuildTests $(VERBOSE)
	cmake --build ./BuildTests $(VERBOSE) --target test

.PHONY: format format-cpp
format: format-cpp format-python

format-cpp:
	./bin/format $(CHECK) --cfversion $(if $(version:-=),$(version),0) ./pennylane_lightning

format-python:
	black -l 100 ./pennylane_lightning/ ./tests $(CHECK)

.PHONY: check-tidy
check-tidy:
	rm -rf ./BuildTidy
	cmake -BBuildTidy -DENABLE_CLANG_TIDY=ON -DBUILD_TESTS=ON -DENABLE_WARNINGS=ON -DPL_BACKEND=$(if $(backend:-=),$(backend),lightning_qubit)
ifdef target
	cmake --build ./BuildTidy $(VERBOSE) --target $(target)
else
	cmake --build ./BuildTidy $(VERBOSE)
endif

.PHONY: gbenchmark
gbenchmark:
	rm -rf ./BuildGBench
	cmake -BBuildGBench -DBUILD_BENCHMARKS=ON -DENABLE_OPENMP=ON -DENABLE_BLAS=ON -DCMAKE_BUILD_TYPE=Release
	cmake --build ./BuildGBench