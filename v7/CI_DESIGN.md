# V7 CI Design — One Runner

The repository has one effective firmware runner. CI must never fan out V7 builds.

## Pre-code/prep rule
This architecture branch contains no runnable V7 workflow. That is intentional: do not consume the runner until V7 source exists.

## Coding CI
Create one V7 workflow after source import. Requirements:
- manual workflow_dispatch for expensive hardware firmware builds during early development;
- one job only;
- one PlatformIO environment: t-deck;
- concurrency group: v7-firmware;
- cancel-in-progress: true for development validation, so obsolete queued builds do not waste capacity;
- recursive submodule checkout;
- Python + PlatformIO + esptool;
- PlatformIO cache keyed from the V7 platformio/dependency lock inputs;
- build once;
- create app + merged image in same job;
- validate image metadata in same job;
- calculate SHA-256/size in same job;
- upload one artifact bundle.

Fast source-only/unit tests that do not need the firmware toolchain may be kept in the same job initially to avoid concurrency surprises. Optimize only after stability.

## Release behavior
Do not auto-release every push. Release creation must require an explicit tag/release gate after hardware QA.
A release job must never mutate or replace V1-V6 artifacts.

## Branch strategy
- v7-saitama-hybrid-prep: architecture/handoff (this branch)
- v7-saitama-hybrid-dev: coding integration branch, created when source import begins
- v7-rc/*: optional release-candidate branch only if needed
- main is not the V7 scratchpad

## Website
Website preview/deploy belongs in kevinkok1999/Meshoffgridnl and is separate from firmware CI. Do not make firmware build success automatically publish production installer content.
