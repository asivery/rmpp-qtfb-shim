SOURCES="src/qtfb-client/qtfb-client.cpp src/shim.cpp src/fb-shim.cpp"
g++ -fPIC -shared -o shim-x86.so $SOURCES
