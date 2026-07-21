graph.mixal (1920x1080) - second frame
1. g++ mix.cc -std=c++17 -O3 -o mix $(pkg-config --cflags --libs sdl2)
   0.89 fps
2. g++ mix.cc -std=c++17 -O3 -march=native -o mix $(pkg-config --cflags --libs sdl2)
   0.93 fps
3. g++ mix.cc -std=c++17 -O3 -march=native -flto -o mix $(pkg-config --cflags --libs sdl2)
   1.38 fps !!!
4. g++ mix.cc -std=c++17 -O3 -march=native -flto -DNDEBUG -o mix $(pkg-config --cflags --libs sdl2)
   1.46 fps
