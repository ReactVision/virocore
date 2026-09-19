# ViroRenderer on WebAssembly

## Building the shipped renderer

`build_web.sh` is the one that matters: it produces the three files
`@reactvision/viro-web-renderer` publishes, and through that package everything
Viro renders on the web.

```sh
source ~/emsdk/emsdk_env.sh
./build_web.sh
# → products/build/viro-web.{js,wasm,data}
```

Then, from the package beside this checkout:

```sh
cd ../../viro-web-renderer && npm run copy-wasm && npm test
```

Its test suite is what catches the two ways this goes wrong: a binary older than
the TypeScript declared over it, and a preloaded font the package may not
redistribute.

Build from a clean tree. The script stamps the commit into the binary and
appends `-dirty` when the working copy has changes, which is the difference
between an artifact someone else can reproduce and one only your machine ever
had.

### What gets baked in

`preload/` is embedded into `viro-web.data` with emscripten's `--preload-file`,
so **everything in that directory is redistributed with the package**. It holds
the GLSL the renderer loads at runtime and one font, `Roboto.ttf` — Apache 2.0,
with its licence beside it, and the same face Viro renders text in on Android
and Quest. A font that cannot be redistributed cannot go in there; that mistake
held the package at unpublished for weeks.

## Command line builds

1. Download Emscripten
2. Run Emscripten's environment script to set the environment (change path below to your Emscripten SDK)
```
source ~/Source/emsdk/emsdk_env.sh --build=Release
```
3. Build with the build.sh script, passing in the test target you'd like to build
```
./build.sh viro_test_fbx
```
4. Start a local webserver to run the target using the run script
```
./run.sh
```
5. Navigate to http://localhost:8080/build/viro_fbx_test.html

## CLion builds

1. Download Emscripten
2. Download CLion
3. Open CLion and choose 'Create project from existing sources'
4. Choose the `/ViroRenderer/wasm` folder
5. Wait for it to sync for a bit
6. If the IDE tells you that project sources are located outside the project would, and asks you if you would like to chnage the project root, hit 'YES' (Change the project root). This *should* happen.
7. Open up preferences (Command-Period)
8. Navigate to `Build, Execution, Deployment` -> `CMake`
9. Create a new Profile called 'Emscripten'
    * Name: Emscripten
    * Build type: Release
    
    * For the following options, get _your_ paths by sourcing the EMSDK environment (step 2 above). It prints out the value of these variables
    
     _Example_ 
    * CMake options: -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=/Users/radvani/Source/emsdk/emscripten/1.37.34/cmake/Modules/Platform/Emscripten.cmake
    * Environment Variables
        * EMSDK: /Users/radvani/Source/emsdk
        * EM_CONFIG: /Users/radvani/.emscripten
        * BINARYEN_ROOT: /Users/radvani/Source/emsdk/clang/e1.37.34_64bit/binaryen
        * EMSCRIPTEN: /Users/radvani/Source/emsdk/emscripten/1.37.34
    * Generation path: products

10. DELETE the other profiles (e.g. Debug): this forces CLion to also use the Emscripten profile when compiling in the IDE
11. Wait for the IDE to sync
12. At the top right, change to Emscripten, and the target you want to build (e.g. viro_test_fbx)
13. Build!
     Note: if you can an error that the working directory `Products` does not exist, create it under ViroRenderer/wasm

14. To run with CLion, go to  `Run` -> `Edit Configurations` 
15. Choose the desired target: e.g. `viro_fbx_test`
16. Set Executable to `run.sh` (in the ViroRenderer/wasm directory)
17. Set Program Arguments to `--no_browser --port 8080 ./products/build/`
18. Set Environment Variable: `PATH=$PATH:/Users/radvani/Source/emsdk/emscripten/1.37.34:/usr/bin`
      (Note: change to _your_ Emscripten path. The usr/bin is for Python)
19. Run!
20. Navigate to http://localhost:8080/build/viro_fbx_test.html



