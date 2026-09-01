# repoq

A lightweight command-line client for the [Repology](https://repology.org) API,
written in C (C11). Quickly check whether a package is behind upstream or
other distributions.

## Dependencies

- A C11 compiler (`gcc` or `clang`) and `make`
- `libcurl` development files
  - Arch: `pacman -S curl`
  - Debian/Ubuntu: `apt install libcurl4-openssl-dev`
  - Fedora: `dnf install libcurl-devel`

cJSON is vendored under `third_party/cjson/`; no extra install needed.

## Building

```sh
make
make install    # optional, installs to $PREFIX/bin (default /usr/local/bin)
```

## Usage

```
repoq <project-name> [options]
repoq --search <substring> [options]
repoq --outdated [--search <substring>] [options]
```

```sh
repoq firefox                          # query a single project
repoq firefox --repo aosc              # filter by repository
repoq firefox --unique                 # collapse duplicate sibling packages
repoq --search firefox --limit 20      # batch search
repoq --outdated --search firefox      # only outdated/legacy results
repoq firefox --json | jq .            # raw JSON output
```

### Options

```
  -r, --repo <name>      Only show packages from repository <name>.
  -s, --search <substr>  Search projects by substring (batch mode).
  -o, --outdated         Only show outdated/legacy projects (batch mode).
  -l, --limit <N>        Limit results in batch mode (default: 200).
  -j, --json             Print raw JSON instead of a table.
  -n, --no-color         Disable colored output.
  -u, --unique           Collapse rows with the same repo/version/status.
  -h, --help             Show help and exit.
  -v, --version          Show version and exit.
```

## API terms of use

repoq respects [Repology's API terms](https://repology.org/api): every
request carries a custom `User-Agent`, and all requests are throttled to at
most 1 per second.

## License

repoq is licensed under the [Apache License 2.0](LICENSE). The vendored
[cJSON](https://github.com/DaveGamble/cJSON) library is MIT-licensed; see
`third_party/cjson/LICENSE`.
