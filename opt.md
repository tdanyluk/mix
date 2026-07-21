# Optimization steps applied
graph.mixal (1920x1080) - second frame
1. g++ mix.cc -std=c++17 -O3 -o mix $(pkg-config --cflags --libs sdl2)
   0.89 fps
2. g++ mix.cc -std=c++17 -O3 -march=native -o mix $(pkg-config --cflags --libs sdl2)
   0.93 fps
3. g++ mix.cc -std=c++17 -O3 -march=native -flto -o mix $(pkg-config --cflags --libs sdl2)
   1.38 fps !!!
4. g++ mix.cc -std=c++17 -O3 -march=native -flto -DNDEBUG -o mix $(pkg-config --cflags --libs sdl2)
   1.46 fps
5. throw MixException -> ThrowMixException
   1.66 fps
6. op table -> switch
   1.79 fps
7. added unnamed namespace
   seems equivalent with -flto
   1.81 fps
8. part() happy path
   2.28 fps !!!
9. set_part() happy path
   2.51 fps !!!
10. throw MixException -> ThrowMixException for the remaining
    2.59 fps
11. NOT ADDED: using unchecked byte didn't help
12. Optimize address()
    2.75 fps 
13. Optimize load_mem_operand_part()
    2.87 fps
14. Optimize get_m()
    2.82 fps
15. Force inline some important functions
    3.19 fps


# Profiling
```
g++ mix.cc -std=c++17 -O2 -march=native -DNDEBUG -g -o mix_g $(pkg-config --cflags --libs sdl2)
/usr/lib/linux-tools/5.4.0-216-generic/perf record -g ./mix_g custom/graph.mixal
/usr/lib/linux-tools/5.4.0-216-generic/perf report
```
