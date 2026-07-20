BUILD:

```
g++ mix.cc -std=c++17 -O2 -o mix
```

With SLD2:
```
g++ mix.cc -std=c++17 -O2 $(pkg-config --cflags --libs sdl2) -o mix
```

With SLD2 & profiling:
```
g++ mix.cc -std=c++17 -O2 -pg $(pkg-config --cflags --libs sdl2) -o mix_p
./mix_p custom/graph.mixal
gprof ./mix_p gmon.out > analysis.txt


g++ mix.cc -std=c++17 -O1 -g $(pkg-config --cflags --libs sdl2) -o mix_g
/usr/lib/linux-tools/5.4.0-216-generic/perf record -g ./mix_g custom/graph.mixal
/usr/lib/linux-tools/5.4.0-216-generic/perf report

g++ mix.cc -std=c++17 -O3 -fprofile-generate -o mix_pgo  $(pkg-config --cflags --libs sdl2)


```

RUN:

```
./mix examples/primes.mixal
./mix examples/permutations.mixal < examples/perm.in
```
