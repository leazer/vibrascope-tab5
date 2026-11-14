# LVGL 9.2.2 Image Viewer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Windows-friendly, offline, single-HTML viewer that safely parses and previews the LVGL 9.2.2 `.c` and `.bin` image assets used by the TAB5 firmware.

**Architecture:** A dependency-free TypeScript core parses source files into a common `ImageAsset`, validates LVGL metadata, and decodes supported formats into RGBA8888. Parsing and decoding run in a Web Worker while small native-DOM modules manage the asset list, diagnostics, metadata and a Canvas 2D preview. Vite builds the application and `vite-plugin-singlefile` produces one self-contained HTML file.

**Tech Stack:** TypeScript, Vite, Vitest, native DOM, Canvas 2D, Web Worker, `vite-plugin-singlefile`.

**Spec:** `docs/superpowers/specs/2026-10-12-lvgl-image-viewer-design.md`

## Global Constraints

- Match the repository's LVGL 9.2.2 little-endian image layout and `LV_IMAGE_HEADER_MAGIC = 0x19`.
- Treat `RGB565` and `RGB565A8` as release-blocking formats; all current `app/assets/images/*.c` assets must parse.
- Never compile, execute, `eval`, or dynamically import content from a dropped file.
- Work entirely offline and emit no network requests from the production artifact.
- Production output is one `dist/lvgl-image-viewer.html` file that runs from `file://` in Edge and Chrome.
- Compressed, RAW and YUV inputs are recognized as unsupported rather than guessed or reported as corrupt.
- Preserve unrelated existing changes, especially `app/apps/app_startup_anim/app_startup_anim.cpp`.

## Review Focus

- A `.bin` that begins with `0x19` but contains impossible dimensions, stride or length must fail validation instead of allocating a huge buffer; covered in Task 2 boundary tests.
- A `.c` file containing comments, unrelated arrays, macros or malicious expression syntax must never execute code and must report a localized diagnostic; covered in Task 3 parser-security tests.
- `RGB565A8` must locate its alpha plane after the complete color plane, including row stride, rather than after `width * height * 2`; covered in Task 2 golden tests.
- Indexed formats must honor palette size, packed bit order and per-row alignment; covered in Task 4 golden tests.
- Worker failure or one invalid file in a multi-file drop must not destroy already loaded assets or block successful siblings; covered in Task 5 state tests.

---

## File Map

```text
tools/lvgl-image-viewer/
├─ src/
│  ├─ core/{image-asset,color-formats,stride,diagnostics,limits}.ts
│  ├─ parsers/{input-router,bin-asset-parser,c-tokenizer,c-expression,c-asset-parser}.ts
│  ├─ decoders/{decoder-registry,rgb,alpha-luma,indexed}.ts
│  ├─ worker/{protocol,asset-worker}.ts
│  ├─ ui/{asset-store,asset-list,preview-canvas,metadata-panel,diagnostics-panel}.ts
│  ├─ app.ts
│  └─ styles.css
├─ tests/
│  ├─ fixtures/
│  ├─ {stride,bin-parser,pixel-decoders,c-expression,c-parser}.test.ts
│  └─ {repository-assets,worker-protocol,asset-store}.test.ts
├─ index.html
├─ package.json
├─ package-lock.json
├─ tsconfig.json
├─ vite.config.ts
└─ README.md
```

## Task 1: Project Skeleton and Domain Contracts

**Files:**
- Create: `tools/lvgl-image-viewer/package.json`
- Create: `tools/lvgl-image-viewer/package-lock.json`
- Create: `tools/lvgl-image-viewer/tsconfig.json`
- Create: `tools/lvgl-image-viewer/vite.config.ts`
- Create: `tools/lvgl-image-viewer/index.html`
- Create: `tools/lvgl-image-viewer/src/core/image-asset.ts`
- Create: `tools/lvgl-image-viewer/src/core/color-formats.ts`
- Create: `tools/lvgl-image-viewer/src/core/diagnostics.ts`
- Create: `tools/lvgl-image-viewer/src/core/limits.ts`
- Create: `tools/lvgl-image-viewer/src/core/stride.ts`
- Create: `tools/lvgl-image-viewer/tests/stride.test.ts`

**Interfaces:**
- Produces: `ImageAsset`, `DecodedImage`, `Diagnostic`, `ParseResult`, `LvColorFormat`, `ViewerLimits`.
- Produces: `calculateStride(format: LvColorFormat, width: number): number` and `validateDimensions(width: number, height: number, limits?: ViewerLimits): Diagnostic[]`.

- [ ] **Step 1: Add the minimal Vite/Vitest/TypeScript manifest and generate the lock file**

Use scripts `dev`, `test`, `test:run`, `typecheck`, and `build`. Pin direct development dependencies in `package.json`; `npm install` generates `package-lock.json`.

- [ ] **Step 2: Write failing stride and dimension-limit tests**

Cover zero/negative dimensions, the configured maximum width, height, pixel count and input byte count, plus stride for RGB565, RGB888, ARGB8888, A8 and packed indexed formats.

- [ ] **Step 3: Run the focused test and verify failure**

Run: `npm test -- --run tests/stride.test.ts`

Expected: FAIL because the core contracts and stride functions do not exist.

- [ ] **Step 4: Implement the domain types, explicit LVGL 9.2.2 color values, limits and stride rules**

Keep the core free of DOM types except `Uint8Array` and `ImageData`-compatible buffer data. Represent recoverable diagnostics without throwing.

- [ ] **Step 5: Verify the skeleton**

Run: `npm run typecheck; npm test -- --run tests/stride.test.ts`

Expected: TypeScript exits 0 and all focused tests pass.

- [ ] **Step 6: Commit**

```bash
git add tools/lvgl-image-viewer
git commit -m "feat(viewer): add image asset domain model"
```

## Task 2: BIN Parser and Release-Blocking Pixel Formats

**Files:**
- Create: `tools/lvgl-image-viewer/src/parsers/bin-asset-parser.ts`
- Create: `tools/lvgl-image-viewer/src/decoders/decoder-registry.ts`
- Create: `tools/lvgl-image-viewer/src/decoders/rgb.ts`
- Create: `tools/lvgl-image-viewer/tests/bin-parser.test.ts`
- Create: `tools/lvgl-image-viewer/tests/pixel-decoders.test.ts`
- Create: `tools/lvgl-image-viewer/tests/fixtures/rgb565-2x2.bin`
- Create: `tools/lvgl-image-viewer/tests/fixtures/rgb565a8-padded.bin`

**Interfaces:**
- Consumes: Task 1 domain contracts and stride functions.
- Produces: `parseBinAsset(fileName: string, bytes: Uint8Array, limits?: ViewerLimits): ParseResult`.
- Produces: `decodeAsset(asset: ImageAsset): DecodeResult` and `PixelDecoder` registration keyed by `LvColorFormat`.

- [ ] **Step 1: Write failing BIN header tests**

Assert exact parsing of the 12-byte little-endian header, computed stride when header stride is zero, rejection of wrong magic/truncation/impossible dimensions/undersized data, and `info` diagnostics for compressed, RAW and YUV inputs.

- [ ] **Step 2: Write failing RGB565 and RGB565A8 golden tests**

Use known red, green, blue, white and transparent pixels. Include a padded RGB565A8 fixture proving alpha begins after `stride * height` bytes.

- [ ] **Step 3: Run focused tests and verify failure**

Run: `npm test -- --run tests/bin-parser.test.ts tests/pixel-decoders.test.ts`

Expected: FAIL because parser and decoders do not exist.

- [ ] **Step 4: Implement strict BIN parsing and validation**

Parse fields with `DataView` in little-endian mode. Check all lengths before slicing or allocating and return a single named asset on success.

- [ ] **Step 5: Implement the decoder registry, RGB565 and RGB565A8**

Return a `Uint8ClampedArray` with RGBA ordering. Use stride for row starts and color-plane length; do not read row padding as pixels.

- [ ] **Step 6: Verify focused and cumulative tests**

Run: `npm run typecheck; npm test -- --run`

Expected: all current tests pass.

- [ ] **Step 7: Commit**

```bash
git add tools/lvgl-image-viewer
git commit -m "feat(viewer): decode LVGL binary RGB assets"
```

## Task 3: Safe C Asset Parser and Repository Regression

**Files:**
- Create: `tools/lvgl-image-viewer/src/parsers/c-tokenizer.ts`
- Create: `tools/lvgl-image-viewer/src/parsers/c-expression.ts`
- Create: `tools/lvgl-image-viewer/src/parsers/c-asset-parser.ts`
- Create: `tools/lvgl-image-viewer/tests/c-expression.test.ts`
- Create: `tools/lvgl-image-viewer/tests/c-parser.test.ts`
- Create: `tools/lvgl-image-viewer/tests/repository-assets.test.ts`
- Create: `tools/lvgl-image-viewer/tests/fixtures/multiple-assets.c`
- Create: `tools/lvgl-image-viewer/tests/fixtures/malformed-assets.c`

**Interfaces:**
- Consumes: Task 1 types and Task 2 decoder support.
- Produces: `tokenizeCSource(source: string, limits?: ViewerLimits): TokenizeResult`.
- Produces: `evaluateIntegerExpression(tokens: readonly CToken[]): ExpressionResult`.
- Produces: `parseCAssets(fileName: string, source: string, limits?: ViewerLimits): ParseResult`.

- [ ] **Step 1: Write failing expression and security tests**

Assert support for decimal/hex integers, parentheses and `+ - * / << >> | &`; reject identifiers, member access, calls, strings, divide-by-zero, unsafe integers and trailing tokens without executing them.

- [ ] **Step 2: Write failing C parser tests**

Cover comments, preprocessor noise, attributes, trailing commas, explicit/omitted stride, multiple arrays and descriptors, missing references, duplicate names, partial success and line-numbered diagnostics.

- [ ] **Step 3: Add the live repository asset regression test**

Resolve `../../../app/assets/images`, parse every `.c`, require at least one asset and no errors, assert `mouse_cursor` is RGB565A8 and all other current business descriptors are RGB565, then decode every asset.

- [ ] **Step 4: Run focused tests and verify failure**

Run: `npm test -- --run tests/c-expression.test.ts tests/c-parser.test.ts tests/repository-assets.test.ts`

Expected: FAIL because C parsing is not implemented.

- [ ] **Step 5: Implement tokenizer and restricted expression evaluator**

Track byte offset and line/column. Enforce input, token and array-byte limits while scanning; unsupported syntax produces diagnostics, never fallback evaluation.

- [ ] **Step 6: Implement descriptor extraction and array linking**

Parse only relevant `uint8_t` arrays and `lv_image_dsc_t` designated initializers. Resolve `.data`, validate metadata/data consistency and preserve valid siblings if one descriptor fails.

- [ ] **Step 7: Verify repository compatibility**

Run: `npm run typecheck; npm test -- --run`

Expected: every current TAB5 image asset parses and decodes; all tests pass.

- [ ] **Step 8: Commit**

```bash
git add tools/lvgl-image-viewer
git commit -m "feat(viewer): parse LVGL C image assets safely"
```

## Task 4: Remaining Uncompressed Color Formats

**Files:**
- Create: `tools/lvgl-image-viewer/src/decoders/alpha-luma.ts`
- Create: `tools/lvgl-image-viewer/src/decoders/indexed.ts`
- Modify: `tools/lvgl-image-viewer/src/decoders/rgb.ts`
- Modify: `tools/lvgl-image-viewer/src/decoders/decoder-registry.ts`
- Modify: `tools/lvgl-image-viewer/tests/pixel-decoders.test.ts`
- Create: `tools/lvgl-image-viewer/tests/fixtures/indexed-golden.ts`

**Interfaces:**
- Consumes: `PixelDecoder`, `ImageAsset`, stride helpers from earlier tasks.
- Produces: decoders for RGB888, ARGB8888, XRGB8888, L8, A8, AL88, I1, I2, I4 and I8.

- [ ] **Step 1: Add failing golden tests for direct formats**

Assert channel order and alpha behavior for RGB888, ARGB8888, XRGB8888, L8, A8 and AL88, including padded rows.

- [ ] **Step 2: Add failing indexed-format golden tests**

For I1/I2/I4/I8 assert palette size/location, packed bit order, multiple rows with alignment and truncated palette/data rejection.

- [ ] **Step 3: Run focused tests and verify failure**

Run: `npm test -- --run tests/pixel-decoders.test.ts`

Expected: new cases fail as unsupported.

- [ ] **Step 4: Implement direct and indexed decoders**

Keep format-specific byte ordering in one module per family and share bounds-check helpers. Do not add compressed or YUV decoding.

- [ ] **Step 5: Verify all core tests**

Run: `npm run typecheck; npm test -- --run`

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add tools/lvgl-image-viewer
git commit -m "feat(viewer): support LVGL uncompressed color formats"
```

## Task 5: Worker Pipeline and Multi-Asset Application State

**Files:**
- Create: `tools/lvgl-image-viewer/src/parsers/input-router.ts`
- Create: `tools/lvgl-image-viewer/src/worker/protocol.ts`
- Create: `tools/lvgl-image-viewer/src/worker/asset-worker.ts`
- Create: `tools/lvgl-image-viewer/src/ui/asset-store.ts`
- Create: `tools/lvgl-image-viewer/tests/worker-protocol.test.ts`
- Create: `tools/lvgl-image-viewer/tests/asset-store.test.ts`

**Interfaces:**
- Consumes: `parseBinAsset`, `parseCAssets`, `decodeAsset`.
- Produces: `parseInput(input: ParseInput): ParseResult`.
- Produces: discriminated `WorkerRequest`/`WorkerResponse` messages with request IDs and transferable decoded buffers.
- Produces: `AssetStore` operations `beginFile`, `acceptResult`, `failFile`, `selectAsset`, `removeFile`, and `clear`.

- [ ] **Step 1: Write failing router and worker-protocol tests**

Cover case-insensitive extensions, content/magic validation, unknown extension diagnostics, stable request IDs and serializable errors.

- [ ] **Step 2: Write failing asset-store tests**

Cover multiple simultaneous files, one invalid sibling, partial C success, same-name replacement, stable selection and cleanup after worker failure.

- [ ] **Step 3: Run focused tests and verify failure**

Run: `npm test -- --run tests/worker-protocol.test.ts tests/asset-store.test.ts`

Expected: FAIL because pipeline/state modules do not exist.

- [ ] **Step 4: Implement routing and the worker protocol**

Transfer `ArrayBuffer` results instead of cloning full RGBA buffers. Convert thrown worker faults to file-scoped diagnostics with no stack HTML.

- [ ] **Step 5: Implement deterministic asset state**

Use file name plus descriptor name for display identity, replace entries from a repeated file name, and keep successful siblings visible when another parse fails.

- [ ] **Step 6: Verify pipeline tests**

Run: `npm run typecheck; npm test -- --run`

Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git add tools/lvgl-image-viewer
git commit -m "feat(viewer): add worker parsing pipeline"
```

## Task 6: Viewer UI, Canvas Interaction and PNG Export

**Files:**
- Create: `tools/lvgl-image-viewer/src/ui/asset-list.ts`
- Create: `tools/lvgl-image-viewer/src/ui/preview-canvas.ts`
- Create: `tools/lvgl-image-viewer/src/ui/metadata-panel.ts`
- Create: `tools/lvgl-image-viewer/src/ui/diagnostics-panel.ts`
- Create: `tools/lvgl-image-viewer/src/app.ts`
- Create: `tools/lvgl-image-viewer/src/styles.css`
- Modify: `tools/lvgl-image-viewer/index.html`
- Modify: `tools/lvgl-image-viewer/tests/asset-store.test.ts`
- Create: `tools/lvgl-image-viewer/tests/preview-math.test.ts`

**Interfaces:**
- Consumes: `AssetStore`, worker messages and `DecodedImage`.
- Produces: `PreviewViewport` with `fit`, `actualSize`, `zoomAt`, `panBy`, `screenToPixel` and `reset`.
- Produces: `renderDecodedImage(canvas: HTMLCanvasElement, image: DecodedImage, viewport: PreviewViewport): void`.
- Produces: `exportPng(image: DecodedImage, fileName: string): Promise<void>`.

- [ ] **Step 1: Write failing viewport and state-transition tests**

Assert fit scale, 1:1, integer nearest-neighbor zoom, cursor-anchored wheel zoom, panning bounds, screen-to-pixel mapping, selection after removal and preference restoration.

- [ ] **Step 2: Run focused tests and verify failure**

Run: `npm test -- --run tests/preview-math.test.ts tests/asset-store.test.ts`

Expected: FAIL because viewport/UI logic does not exist.

- [ ] **Step 3: Build the accessible three-column shell**

Add full-window drop handling, multi-file picker, keyboard shortcuts, empty/loading/success/partial/failure states and `aria-live` diagnostics. Insert all file-controlled text via `textContent`.

- [ ] **Step 4: Implement canvas rendering and inspection**

Draw checker/solid backgrounds separately from image pixels, disable smoothing, support wheel zoom/drag/double-click, optional pixel grid and hover metadata.

- [ ] **Step 5: Implement PNG export and preferences**

Export only the original decoded pixels through an offscreen canvas. Store background, grid and display preferences in `sessionStorage`; never store image bytes.

- [ ] **Step 6: Verify tests and development build**

Run: `npm run typecheck; npm test -- --run; npm run build`

Expected: all commands exit 0 and the application bundle builds.

- [ ] **Step 7: Commit**

```bash
git add tools/lvgl-image-viewer
git commit -m "feat(viewer): add drag-and-drop image preview UI"
```

## Task 7: Single-HTML Packaging, Real-Asset Acceptance and Documentation

**Files:**
- Modify: `tools/lvgl-image-viewer/vite.config.ts`
- Modify: `tools/lvgl-image-viewer/package.json`
- Create: `tools/lvgl-image-viewer/README.md`
- Modify: `tools/lvgl-image-viewer/tests/repository-assets.test.ts`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: the complete viewer from Tasks 1-6.
- Produces: reproducible `npm run build` output at `tools/lvgl-image-viewer/dist/lvgl-image-viewer.html`.

- [ ] **Step 1: Configure the single-file production build**

Inline JavaScript, CSS and worker code; disable external preload/module references. Ignore `tools/lvgl-image-viewer/dist/` while keeping source, tests and lock file tracked.

- [ ] **Step 2: Add production artifact assertions**

Add a post-build test or script that requires exactly one HTML output, rejects `http://`, `https://`, external script/style URLs and unexpected sibling assets, and checks the application marker text.

- [ ] **Step 3: Write usage and support documentation**

Document double-click use, drag/drop and shortcuts, supported LVGL/version/formats, privacy/offline behavior, build/test commands and unsupported compression/RAW/YUV cases.

- [ ] **Step 4: Run the full automated acceptance suite**

Run: `npm clean-install; npm run typecheck; npm test -- --run; npm run build; npm run verify:dist`

Expected: every command exits 0; repository assets all parse; exactly one self-contained HTML file is produced.

- [ ] **Step 5: Perform browser smoke testing**

Open `dist/lvgl-image-viewer.html` by `file://` in the installed Chromium browser. Drop `app/assets/images/logo_tab.c`, `mouse_cursor.c` and `launcher_bg.c`; verify dimensions, formats, transparency, responsive UI, zoom/background/pixel inspection and PNG export. Drop one malformed C and one truncated BIN and verify localized diagnostics without losing valid images. Confirm DevTools/network evidence shows no network requests.

- [ ] **Step 6: Check repository cleanliness and diff scope**

Run: `git status --short; git diff --check HEAD~6..HEAD`

Expected: only viewer files, its docs and the intentional `.gitignore` line are part of this feature; pre-existing unrelated modifications remain uncommitted and untouched.

- [ ] **Step 7: Commit**

```bash
git add tools/lvgl-image-viewer .gitignore
git commit -m "docs(viewer): package and verify offline release"
```
