BUILD:

```
g++ mix.cc -std=c++17 -O2 -o mix
```

With SLD2:
```
g++ mix.cc -std=c++17 -O2 $(pkg-config --cflags --libs sdl2) -o mix
```

RUN:

```
./mix examples/primes.mixal
./mix examples/permutations.mixal < examples/perm.in
```
