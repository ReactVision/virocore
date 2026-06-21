# PICO support — mikevocalz/virocore fork

This fork adds PICO OS 5.9+ / OS 6 / Swan support to virocore's OpenXR backend.
It tracks upstream `ReactVision/virocore` and is meant to be **upstreamable** —
every change is vendor-neutral OpenXR with no PICO `#ifdef`s, so the long-term
goal is to merge it back and retire the fork.

## Layout

- Base: upstream tag `v2.56.0` (commit `5c3d101`).
- Work branch: `pico-support`.
- Two native commits:
  1. input — PICO interaction profiles.
  2. renderer — runtime detection, soft-required extensions, foveation, JNI.
- `scripts/build-pico-aar.sh` — builds `viro_renderer-release.aar`.
- `.github/workflows/build-pico-aar.yml` — CI builds the AAR per push/tag.

## How it's consumed

The JS package `@reactvision/react-viro` ships a **prebuilt**
`android/viro_renderer/viro_renderer-release.aar` and re-exports it via gradle.
So consumption is just: build the AAR here → drop it into the JS-package fork
(`mikevocalz/viro`) → republish as `@mikevocalz/react-viro`. `expo-pico` depends
on that renamed package instead of patching `node_modules`.

```
mikevocalz/virocore (native)  --build-pico-aar.sh-->  viro_renderer-release.aar
        │                                                      │
        └── CI artifact ──────────────────────────────────────┘
                                                               ▼
mikevocalz/viro (JS fork)  --bundles AAR, renames pkg-->  @mikevocalz/react-viro
                                                               ▼
expo-pico  --depends on @mikevocalz/react-viro-->  app builds with PICO support
```

## Staying in sync with upstream

Keep the fork a thin, rebasable delta:

```bash
# one-time
git remote add upstream https://github.com/ReactVision/virocore.git

# when upstream tags a new release (e.g. v2.57.0)
git fetch upstream --tags
git checkout -b pico-support-2.57.0 v2.57.0
git cherry-pick <input-commit> <renderer-commit>   # the two pico commits
# resolve any conflicts (most likely in the extension arrays or swapchain block)
./scripts/build-pico-aar.sh                          # rebuild + sanity-grep
```

Because the two commits are small and touch a contained region (OpenXR backend
only), rebasing onto a new upstream tag is usually a clean cherry-pick. If a
conflict appears it'll be in `VROSceneRendererOpenXR.cpp`'s extension list or
swapchain creation — both well-localized.

## Upstreaming

File the issue in `ISSUE-pico-openxr-support.md` first. If ReactVision wants it,
these same two commits become the PR (rendering+input+foveation), with anchors /
scene mesh / body+eye tracking / MRC as separate follow-up PRs per
`PICO-FEATURE-ROADMAP.md`. Once merged upstream, retire this fork and point
`expo-pico` back at `@reactvision/react-viro`.

## What this fork does NOT contain

The feature-work items (spatial anchors, room/scene mesh, body/eye/motion
tracking, human occlusion, MRC) are NOT here — they need new scene-graph
subsystems + JS component APIs, not OpenXR flags. See `PICO-FEATURE-ROADMAP.md`.
This fork is "renders + full controller/hand input + foveation on PICO," which
is the bar for most apps.

## Build boundary (honest note)

The AAR build (`./gradlew :sharedCode:assembleRelease`) requires the Android
NDK toolchain and was NOT executed in the environment that authored these
commits — the source changes are complete and self-consistent, but the compiled
`.so` / `.aar` and on-device validation are your step. The CI workflow exists to
make that build reproducible rather than machine-dependent. Before tagging a
release, also verify on your PICO 4 Ultra (B3110):
- the FB foveation enum spellings against your NDK's openxr headers,
- the PICO OS version system-property key,
- controllers respond (the input fix), and
- `getRuntimeInfo()` returns `vendor=PICO`.
