# leaf-analyzer

Command-line driver for the leaf-core v1 library. The library itself never reads image files. JPEG and PNG decoding lives only in this tool, through OpenCV `imgcodecs`.

«EXIF orientation is not applied; inputs must be canonically oriented.»

## Modes

Single image:

```text
leaf-analyzer --input <image> --output <result.json> [--config <config.json>] [--debug <dir>]
```

Batch, in lexicographic path order. A bad file does not stop the rest of the directory. The process exit code is the highest code among the files:

```text
leaf-analyzer --input-dir <dir> --output-dir <dir> [--config <config.json>] [--debug-dir <dir>]
```

Only `.jpg`, `.jpeg` and `.png` are accepted. The extension selects the file; the bytes must still decode. BGR is converted to RGB and BGRA to RGBA before analysis.

`--debug` and `--debug-dir` are rejected while parsing when the tool was built with `LEAF_ENABLE_DEBUG=OFF`. The message is `debug support is unavailable in this build`, and that build does not produce a `leaf-core-debug` target. Debug directories are created only when the flag is present. Overlays draw the contour, center vein, axes, keypoints a1–g2 and M1–M5. They are not fed back into the measurement.

## Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Success, including a structurally complete result that quality marks unacceptable |
| 2 | Mixed, unknown, or missing options, or debug requested from a build without debug support |
| 3 | Unknown or corrupt input |
| 4 | Analysis or config failure. A versioned error JSON was written atomically |
| 5 | The JSON or a debug artifact could not be written |

Outputs are written to a sibling temporary file, flushed, closed, and renamed into place.
