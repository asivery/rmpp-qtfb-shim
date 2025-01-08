SOURCES="src/qtfb-client/qtfb-client.cpp src/shim.cpp src/fb-shim.cpp src/input-shim.cpp"
aarch64-remarkable-linux-g++ -DFULL_RM2 --sysroot $SDKTARGETSYSROOT -fPIC -shared -o shim.so $SOURCES
aarch64-remarkable-linux-g++ -DFULL_RM2 -D_32BITFIXEDINFO --sysroot $SDKTARGETSYSROOT -fPIC -shared -o shim-32bit-structs.so $SOURCES
