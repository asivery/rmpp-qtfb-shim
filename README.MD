# qtfb-shim

A shim for the rMPP tablet which makes it possible to use rM1 and rM2 applications with it, after recompiling them to aarch64* with the help of [qtfb](https://github.com/asivery/qtfb).

Inspired by [ddvk's awesome remarkable2-framebuffer](https://github.com/ddvk/remarkable2-framebuffer)


\* Some programs incorrectly define the framebuffer structures (they hardcode the 32-bit variant of them, instead of relying on linux headers). In order to use them, please use `shim-32bit-structs.so` as the shim, as opposed to the usual `shim.so`.

