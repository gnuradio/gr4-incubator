# gr4-incubator

The gr4-incubator repo is designed to be a staging area for GR4 block development prior to 
upstreaming blocks to the main gnuradio4 repo.  It is intended to collect and triage the "kitchen sink"
of useful blocks, schedulers, utilities, but have a lower bar for accepting contributions
than the core.

## Getting Started

Pre-requisites for compiling this codebase are outlined in `docker/Dockerfile`.  For convenience,
example `devcontainer.json` configurations are given in the `.devcontainer` directory for VS Code integration

## Organization

The tree is organized around creating multiple modules under the `blocks` directory that live 

```
├── blocks
│   ├── analog
│   │   ├── apps
│   │   ├── benchmarks
│   │   ├── docs
│   │   ├── examples
│   │   ├── include
│   │   ├── plugin
│   │   ├── src
│   │   └── test
│   ├── audio
│   ├── basic
│   ├── pfb
│   ├── soapysdr
│   └── zeromq
├── cmake
│   └── Modules
├── docker
├── examples
│   └── gr3_flowgraphs
└── subprojects
```



## Examples

Examples that exercise multiple modules