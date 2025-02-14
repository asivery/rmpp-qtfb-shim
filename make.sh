SOURCES="src/qtfb-client/qtfb-client.cpp src/shim.cpp src/fb-shim.cpp src/input-shim.cpp"
aarch64-remarkable-linux-g++ --sysroot $SDKTARGETSYSROOT -fPIC -shared -o shim.so $SOURCES
aarch64-remarkable-linux-g++ -D_32BITFIXEDINFO --sysroot $SDKTARGETSYSROOT -fPIC -shared -o shim-32bit-structs.so $SOURCES
