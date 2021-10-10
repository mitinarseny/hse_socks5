# hse_socks5

## Requirements

* Linux : `>= 5.13`
* [clang](https://clang.llvm.org/): `>=12.0.1`

## Generate

```sh
$ cmake \
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

Add `-DCMAKE_EXPORT_COMPILE_COMMANDS=On` to [generate](#generate).  
And then link to the root:

```sh
$ ln -s $(pwd)/build/compile_commands.json .
```
