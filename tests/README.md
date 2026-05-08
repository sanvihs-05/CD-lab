# NSSan Smoke Tests

These tests mirror the instability scenarios called out in the synopsis. They are designed to be compiled with debug info and low optimization so the pass can preserve source locations and shadow stack-backed `float` values reliably.

Run the whole suite with:

```powershell
pwsh ./tests/run-all.ps1 -BuildDir ./build
```

The runner expects:

- a built `NSSanPass` plugin in `build/`
- a built `NSSanRuntime` library in `build/`
- a working `clang` on `PATH`, or `-Clang <path>` supplied explicitly

Expected no-report cases:

- `kahan_sum_1e7`
- `no_cancel_baseline`

