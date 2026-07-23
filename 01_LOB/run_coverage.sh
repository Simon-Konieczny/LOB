# 1. Clean the old coverage build directory to prevent stale data
rm -rf build-coverage

# 3. Configure CMake strictly for coverage
cmake -B build-coverage -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="--coverage -O0 -fno-inline" \
  -DCMAKE_EXE_LINKER_FLAGS="--coverage"

# 4. Build the engine and tests
cmake --build build-coverage -j 8

# 5. Run the test suite
ctest --test-dir build-coverage --output-on-failure

# 5. Run gcovr to print the summary directly to your terminal (Matches CI exactly)
gcovr -r . build-coverage \
  --filter src/ \
  --exclude ".*main\.cpp" \
  --html-details build-coverage/coverage_report.html

open build-coverage/coverage_report.html