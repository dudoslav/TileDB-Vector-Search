# Building and Running Tests

TileDB Vector Search can be built from source for either C++ or Python.

## C++

To build for C++, run:

```bash
cmake -S ./src -B ./src/build -DCMAKE_BUILD_TYPE=Debug
cmake --build ./src/build -j3
```

Then you can run the tests:

```
cmake --build ./src/build --target check
```

Alternatively, you can setup CLion, which is the suggested way to develop C++ in this project. To set up CLion:

- Open up CLion to the root directory of this repo.
- Go to `File` -> `Settings` -> `Build, Execution, Deployment` -> `CMake`.
  - Set `CMake options` to `G "Unix Makefiles" -DCMAKE_IDE=ON -DTileDB_DIR:PATH=/Users/<name>/repo/tileDB/build/dist -DTILEDB_VS_ENABLE_BLAS=on -DTILEDB_VS_PYTHON=off`.
    - Note that `DTileDB_DIR` will be specific to your TileDB installation. If you have it installed in a standard location, you can omit this option.
  - Set `Build directory` to `cmake-build-debug/libtiledbvectorsearch`.
- Next right click on `src/CMakeLists.txt` and select `Load CMake Project`.
- After that you should see configurations for unit tests and build targets automatically generated by CLion.

## Python

Before building you may want to set up a virtual environment:

```bash
conda create --name TileDB-Vector-Search python=3.9
conda activate TileDB-Vector-Search
```

To build for Python, run:

```bash
pip install .
```

You can run unit tests with `pytest`. You'll also need to install the test dependencies:

```bash
pip install ".[test]"
```

Then you can run the tests:

```bash
cd apis/python
# To run all tests.
pytest
# To run a single test and display standard output and standard error.
pytest test/test_ingestion.py -s
```

To test Demo notebooks:

```bash
cd apis/python
pip install -r test/ipynb/requirements.txt
pytest --nbmake test/ipynb
```

Credentials:

- Some tests run on TileDB Cloud using your current environment variable `TILEDB_REST_TOKEN` - you will need a valid API token for the tests to pass. See [Create API Tokens](https://docs.tiledb.com/cloud/how-to/account/create-api-tokens) for for instructions on getting one.
- For continuous integration, the token is configured for the `unittest` user and all tests should pass.

# Dependencies

## Linux

There are several dependencies needed, for Ubuntu you can install via:

```
apt-get openblas-dev build-essentials cmake3
```

To build the python API after you have the dependencies, use pip:

```bash
pip install .
```

## Docker

A docker image is also provided for simplicity:

```
docker build -t tiledb/tiledb-vector-search .
```

You run the example docker image which provides the python package with:

```
docker run --rm tiledb/tiledb-vector-search
```

# Formatting

There are two ways you can format your code.

### 1. Using `clang-format`

If you just want to format C++ code and don't want to `pip install` anything, you can install [clang-format](https://clang.llvm.org/docs/ClangFormat.html) version 17 and use that directly. Install it yourself, or by running this installation script:

```bash
./scripts/install_clang_format.sh
```

Then run it:

```bash
# Check if any files require formatting changes:
./scripts/run_clang_format.sh . clang-format 0
# Run on all files and make formatting changes:
./scripts/run_clang_format.sh . clang-format 1
```

### 2. Using `pre-commit`

Alternatively, you can format all code in the repo (i.e. C++, Python, YAML, and Markdown files) with [pre-commit](https://pre-commit.com/), though it requires installing with `pip`. Install it with:

```bash
pip install ".[formatting]"
```

Then run it:

```bash
# Run on all files and make formatting changes:
pre-commit run --all-files
# Run on single file and make formatting changes:
pre-commit run --files path/to/file.py
```

# Debugging C++ crashes in Python

If you are seeing C++ crashes only when running Python, you can debug by running `lldb -f python -- apis/python/test/crash.py` and then `(lldb) run`. By default tiledbvspy does not have debug symbols, so you'll see a stack trace like:

```bash
(lldb) bt
* thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS (code=1, address=0x30001356a8f20)
  * frame #0: 0x0000000124a8d7c0 _tiledbvspy.cpython-39-darwin.so`___lldb_unnamed_symbol832 + 88
    frame #1: 0x0000000124a8d954 _tiledbvspy.cpython-39-darwin.so`___lldb_unnamed_symbol834 + 32
    frame #2: 0x0000000124ac0c64 _tiledbvspy.cpython-39-darwin.so`___lldb_unnamed_symbol1139 + 104
    frame #3: 0x0000000124b6ff00 _tiledbvspy.cpython-39-darwin.so`___lldb_unnamed_symbol2869 + 32
    frame #4: 0x0000000124b20150 _tiledbvspy.cpython-39-darwin.so`___lldb_unnamed_symbol1879 + 52
    frame #5: 0x0000000124c47354 _tiledbvspy.cpython-39-darwin.so`___lldb_unnamed_symbol3648 + 100
    frame #6: 0x000000012073b4d0 cc.cpython-39-darwin.so`pybind11::detail::clear_instance(_object*) + 392
    frame #7: 0x000000012073b1c8 cc.cpython-39-darwin.so`pybind11_object_dealloc + 44
    frame #8: 0x000000010007feb0 python`frame_dealloc + 840
    frame #9: 0x000000010005d260 python`_PyFunction_Vectorcall + 476
    frame #10: 0x000000010016ba6c python`_PyEval_EvalFrameDefault + 27688
    frame #11: 0x0000000100162dfc python`_PyEval_EvalCode + 700
    frame #12: 0x00000001001d7e04 python`run_mod + 188
    frame #13: 0x00000001001d6674 python`pyrun_file + 176
    frame #14: 0x00000001001d60e4 python`pyrun_simple_file + 352
    frame #15: 0x00000001001d5f44 python`PyRun_SimpleFileExFlags + 64
    frame #16: 0x00000001001f883c python`pymain_run_file + 264
    frame #17: 0x00000001001f7e84 python`pymain_run_python + 360
    frame #18: 0x00000001001f7cc4 python`Py_RunMain + 40
    frame #19: 0x0000000100006a20 python`main + 56
    frame #20: 0x00000001832510e0 dyld`start + 2360
```

To enable symbols, you can update `src/CMakeLists.txt` and add `set(CMAKE_BUILD_TYPE "Debug")` after it adds `set(CMAKE_BUILD_TYPE ...`.
