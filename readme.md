
<img src="https://karimali.ca/resources/images/projects/swan.png" width="150">

# SWAN (a.k.a Swift-WALA)
A static program analysis framework for analyzing Swift applications using WALA as the analysis core. 


## Introduction

This static program analysis framework is being developed for detecting security vulnerabilities in Swift applications using taint analysis. A custom translator converts Swift Intermediate Language ([SIL](https://github.com/apple/swift/blob/master/docs/SIL.rst)) to [WALA](https://github.com/wala/WALA) IR (called CAst). The SIL is retrieved by hooking into the Swift compiler and grabbing the SIL modules during compilation. The resulting CAst nodes are analyzed using custom analysis written on top of WALA.

The current translator only supports the most common SIL instructions, and we recently added general support for Swift v5, so better SIL instruction support is likely to come soon.

## Current work
The translator and basic toolchain/dataflow has been implemented. We are currently working on implementing the architecture for the analysis to be built on top of WALA. Then we will implement points-to analysis and taint analysis with basic sources and sinks identified.

## Future plans
- Lifecycle awareness for iOS and macOS applications (custom call graph building)
- Sources and sinks for iOS and macOS libraries
- Xcode plugin
- Better (maybe full) SIL instruction support for latest Swift version

## Getting Started

First, you should consider that the final build may be as large as 100GB.

**Disclaimer:** SWAN doesn't target a specific Swift or WALA source code version. The source code pulled from the Apple and IBM are not versioned, nor do they have stable branches. Therefore, the build is pretty volatile as changes made by Apple and IBM to their source code can break the build for SWAN. Changes in the Swift compiler are often problematic for SWAN. We try our best to make sure the build works with the most up to date dependency source code. Please open up an issue if it is breaking for you.

### Download Projects

We use the latest Swift compiler and WALA.
```
mkdir swift-source
cd swift-source
git clone https://github.com/apple/swift
git clone https://github.com/wala/WALA
git clone https://github.com/themaplelab/swan
```
`master` branch may not always be the up-to-date branch. In this case, use the `-b` flag when cloning `swan` to select the appropriate branch.

### Build Dependencies

#### WALA

```
cd ./WALA
./gradlew assemble
cd ..
```

#### Swift

```
cd ./swift
./utils/update-checkout --clone
./utils/build-script
cd ..
```
Optionally, the `-d` flag can be added to the `build-script` so Swift can compile in debug mode.

#### Edit Swift-WALA Configurations

```
cd swift-wala/com.ibm.wala.cast.swift
cp gradle.properties.example gradle.properties
```

Edit `gradle.properties` and provide proper paths. Some example paths are already provided to give you an idea of what they might look like for you. For macOS, change the `linux` to `macosx` in the paths. (e.g `swift-linux-x86_64` to `swift-macosx-x86_64`)


#### Build Swift-WALA

```
./gradlew assemble
```

### Running Swift-WALA

- First you need to setup environment variables. You can also add this to your `~/.bashrc` or `~/.bash_profile`. Make sure to `source` after.

```
export WALA_PATH_TO_SWIFT_BUILD={path/to/your/swift/build/dir}
export WALA_DIR={path/to/your/wala/dir}
export SWIFT_WALA_DIR={path/to/your/swift-wala/dir}
```

- To run the Java code:

`./gradlew run --args THE_ARGS_YOU_WANT`


- Otherwise, you can run the standalone c++ code in `{swift-wala/dir/}/com.ibm.wala.cast.swift/swift-wala-translator/build/external-build/swiftWala/linux_x86-64/bin`.

```
./swift-wala-translator-standalone example.swift
```
