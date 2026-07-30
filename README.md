# Simple Guitar

NAM amp sim / FX host for Windows and macOS. Loads .nam captures and cab IRs, chains built-in effects, hosts VST3/AU plugins. Runs standalone or as VST3/AU/CLAP in a DAW.

Very early. Most of this doesn't exist yet.

## Building

Needs CMake 3.24+, a C++20 compiler, and Node.

```
cmake -B build
cmake --build build --config Release
```

On Windows, if configure can't find WebView2, download the Microsoft.Web.WebView2 nuget package and point at it with `-DJUCE_WEBVIEW2_PACKAGE_LOCATION`.

## License

GPL-3.0. Built on JUCE 8 and NeuralAmpModelerCore.
