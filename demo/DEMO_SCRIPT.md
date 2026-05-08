# NSSan Demo Script

This is the evaluator-facing native demo flow.

## One-Time Prep

On the Windows host:

```powershell
git clone --depth=1 --branch llvmorg-18.1.8 https://github.com/llvm/llvm-project.git C:\llvm-project
docker build -f Dockerfile.test -t nssan-test .
```

The first native run builds patched Clang and can take a while. Later runs reuse the cached Docker volumes.

## Live Demo

Run the scripted demo:

```powershell
docker run --rm `
  -v "${PWD}:/workspace" `
  -v "C:\llvm-project:/llvm-project" `
  -v nssan-llvm-src-cache:/llvm-src `
  -v nssan-llvm-build-cache:/llvm-build `
  -w /workspace `
  nssan-test bash ./scripts/demo-native.sh
```

Or show the commands manually inside the container:

```bash
bash ./scripts/install-native-nssan.sh
/llvm-build/bin/clang ./demo/buggy.c -o ./demo/out/buggy
./demo/out/buggy

/llvm-build/bin/clang -fsanitize=numerical ./demo/buggy.c -o ./demo/out/buggy_san
./demo/out/buggy_san

/llvm-build/bin/clang -fsanitize=numerical ./demo/clean.c -o ./demo/out/clean_san
./demo/out/clean_san
```

## Full Suite

```powershell
docker run --rm `
  -v "${PWD}:/workspace" `
  -v "C:\llvm-project:/llvm-project" `
  -v nssan-llvm-src-cache:/llvm-src `
  -v nssan-llvm-build-cache:/llvm-build `
  -w /workspace `
  nssan-test bash ./run_tests.sh
```

## Benchmark Run

```powershell
docker run --rm `
  -v "${PWD}:/workspace" `
  -v "C:\llvm-project:/llvm-project" `
  -v nssan-llvm-src-cache:/llvm-src `
  -v nssan-llvm-build-cache:/llvm-build `
  -w /workspace `
  nssan-test bash ./benchmark.sh
```
