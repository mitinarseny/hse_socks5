# hse_socks5

[SOCKS5](https://datatracker.ietf.org/doc/html/rfc1928) server implementation
using [libuv](https://github.com/libuv/libuv).

## Requirements

* Linux : `>= 5.13`
* [clang](https://clang.llvm.org/): `>=12.0.1`
* [Ninja](https://ninja-build.org/)

## Download

```sh
$ git clone https://github.com/mitinarseny/hse_socks5.git
$ cd hse_socks5
$ git submodule update --init
```

## Generate

```sh
$ cmake -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=Off \
  -DCMAKE_CXX_COMPILER=clang++ \
  -B build
```

## Build

```sh
$ cmake --build build
```

## Run

```sh
$ cd build
$ ./hse_socks5
```

## Development

Add `-DCMAKE_BUILD_TYPE=Debug` and `-DCMAKE_EXPORT_COMPILE_COMMANDS=On` to [generate](#generate).  
And then link to the root:

```sh
$ ln -s $(pwd)/build/compile_commands.json .
```
