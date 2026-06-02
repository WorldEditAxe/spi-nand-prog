# SPI-NAND Programmer

## About

A SPI-NAND flash programmer software botched together using SPI-MEM and SPI-NAND framework taken from Linux v5.8.

## Features

* Reading/Writing SPI NAND
* Operations with on-die ECC enabled/disabled
* Operations with OOB data included or not
* Skip bad blocks during writing
* Data verification for writing when on-die ECC is enabled

## Supported devices

[WCH CH347](https://www.wch.cn/products/CH347.html)

The default driver. No extra arguments needed. 

[dword1511/stm32-vserprog](https://github.com/dword1511/stm32-vserprog)

add the following arguments to select this driver:

```
-d serprog -a /dev/ttyACM0
```

Linux spidev devices

Uses the native Linux `/dev/spidevX.Y` userspace SPI interface:

```
-d spidev -a /dev/spidev0.0
```

Optional comma-separated settings can be appended to the driver argument:

```
-d spidev -a /dev/spidev0.0,speed=12000000,mode=0,io=single,max=4096
```

`io` defaults to `single`. Use `io=rx-dual` or `io=rx-quad` for wider read-data
transfers only. Use `io=dual` or `io=quad` only if the Linux SPI controller and
wiring also support multi-I/O address/write transfers through spidev.

## Usage
```
spi-nand-prog <operation> [file name] [arguments]

Operations: read/write/erase/scan
Arguments:
 -d <driver>: hardware driver to be used.
 -a <arg>: additional argument provided to current driver.
 -o <offset>: Flash offset. Should be aligned to page boundary when reading and block boundary when writing. default: 0
 -l <length>: read length. default: flash_size
 --no-ecc: disable on-die ECC. This also disables data verification when writing.
 --with-oob: include OOB data during operation.
```
