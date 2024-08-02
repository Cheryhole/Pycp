# Pycp Proggramming Language Version 0.1
---

Content
- [General Information](#general-information)
- [Using Pycp](#using-pycp)
- [Build Pycp](#build-pycp)


## General Information

Pycp is a new language which can compile the script as a binary executable, or run the script directly.

Its gramma is like Python and C++, so it called Pycp.

## Using Pycp

You can download the binary executable and use it.

for compiling
```
pycp -c/--compile script.pycp
```
for running
```
pycp script.pycp
```

see more with `pycp --help`


## Build Pycp

You can build the binary easily with cmake.
```
mkdir build
cd build
cmake ..
make
```
just wait a moment.
