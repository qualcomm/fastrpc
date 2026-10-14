# Calculator FastRPC Example

Minimal FastRPC example offloading `sum` and `max` to the Hexagon DSP.
Supports unsigned PDs only.

## Layout

```
calculator/
├── inc/                        calculator.h (QAIC-generated), calculator_test.h
├── apps/                       Linux host side
│   └── src/
│       ├── calculator_stub.c   QAIC-generated stub
│       └── calculator_test.c   Test logic
├── hexagon/                    Hexagon DSP side
│   └── src/
│       ├── calculator_skel.c   QAIC-generated skeleton
│       └── calculator_imp.c    DSP implementation
└── calculator.idl              Interface definition
```

## Prerequisites

- `clang` and `ld.lld`
- `libcdsprpc.so.1.0.0` — built from the fastrpc project (required at link time for the host library)
- `QAIC` — to regenerate `calculator.h`, `calculator_stub.c`, and `calculator_skel.c` from `calculator.idl`
  - Source: https://github.com/qualcomm/QAIC.git
  - `AEEStdDef.idl` and `remote.idl` from the Hexagon SDK are required dependencies, place them
    in an `idl/` directory alongside `calculator.idl`, then run:
    ```bash
    ./qaic -I./idl calculator.idl -st
    ```

## Build

> **Note:** `Makefile.am` for autotools integration will be added once the QAIC compiler
> is available and the IDL dependencies (`AEEStdDef.idl`, `remote.idl`) are in place.


Set `ROOT` to the fastrpc repo root before building.

**DSP skeleton:**
```bash
cd hexagon/
mkdir -p obj
for f in src/*.c; do
    clang --target=hexagon -fPIC -Wall -Werror \
        -I"../inc" -I"$ROOT/inc" \
        -c "$f" -o "obj/$(basename ${f%.c}.o)"
done
ld.lld -shared -soname=libcalculator_skel.so \
    -z separate-loadable-segments \
    -o libcalculator_skel.so obj/*.o
```

**Host library:**
```bash
cd apps/
mkdir -p obj
for f in src/*.c; do
    clang --target=aarch64-linux-gnu -fPIC -Wall -Werror \
        -I"../inc" -I"$ROOT/inc" \
        -c "$f" -o "obj/$(basename ${f%.c}.o)"
done
ld.lld -shared -soname=libcalculator.so \
    -o libcalculator.so obj/*.o \
    "$ROOT/src/.libs/libcdsprpc.so.1.0.0"
```

## Deploy & Run

Copy the libraries to the target device:

```bash
# Host stub
cp apps/libcalculator.so /usr/lib/fastrpc_test/

# DSP skeleton
cp hexagon/libcalculator_skel.so /usr/share/fastrpc_test/v68/
```

Run using the `fastrpc_test` binary (CDSP, unsigned PD only):

```bash
fastrpc_test -d 3 -U 1
```
