BUILD:

```
g++ mix.cc -std=c++17 -O2 -o mix
```

With SLD2:
```
Portable:
g++ mix.cc -std=c++17 -O3 -flto -DNDEBUG -o mix $(pkg-config --cflags --libs sdl2)

Native:
g++ mix.cc -std=c++17 -O3 -march=native -flto -DNDEBUG -o mix $(pkg-config --cflags --libs sdl2)
```

RUN:

```
./mix examples/primes.mixal
./mix examples/permutations.mixal < examples/perm.in
```
