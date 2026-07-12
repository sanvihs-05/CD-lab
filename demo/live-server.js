#!/usr/bin/env node
// NSSan live playground server.
// Serves the playground page and runs submitted C through the real Docker
// NSSan toolchain (patched clang -fsanitize=numerical), returning the report.
//
// Launch via demo/live.ps1, which sets WORKSPACE / LLVM_PATH and opens a browser.

'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');
const { execFile } = require('child_process');

const PORT = process.env.NSSAN_PORT ? parseInt(process.env.NSSAN_PORT, 10) : 7890;
const WORKSPACE = process.env.WORKSPACE || path.resolve(__dirname, '..');
const LLVM_PATH = process.env.LLVM_PATH || 'C:\\llvm-project';
const LIVE_DIR = path.join(WORKSPACE, 'demo', 'live');
const SNIPPET_C = path.join(LIVE_DIR, 'snippet.c');
const PAGE = path.join(__dirname, 'live-playground.html');

fs.mkdirSync(LIVE_DIR, { recursive: true });

const DOCKER_BASE = [
  'run', '--rm',
  '-v', `${WORKSPACE}:/workspace`,
  '-v', `${LLVM_PATH}:/llvm-project`,
  '-v', 'nssan-llvm-src-cache:/llvm-src',
  '-v', 'nssan-llvm-build-cache:/llvm-build',
  '-w', '/workspace',
  'nssan-test', 'bash', '-lc',
];

let ready = false;
let warming = false;

function dockerRun(script, timeoutMs) {
  return new Promise((resolve) => {
    execFile('docker', DOCKER_BASE.concat([script]),
      { timeout: timeoutMs || 90000, maxBuffer: 4 * 1024 * 1024 },
      (err, stdout, stderr) => {
        resolve({ err, stdout: stdout || '', stderr: stderr || '' });
      });
  });
}

// Install the NSSan pass + runtime into the cached clang once, up front.
async function warmup() {
  if (ready || warming) return;
  warming = true;
  const r = await dockerRun('bash ./scripts/install-native-nssan.sh >/dev/null 2>&1 && echo READY', 600000);
  ready = /READY/.test(r.stdout) && !r.err;
  warming = false;
  return ready;
}

const CLANG = '/llvm-build/bin/clang';

// only allow a safe threshold token like 1e-5, 1e-8, 0.001
function safeThreshold(t) {
  if (typeof t !== 'string') return '';
  return /^[0-9]+(\.[0-9]+)?([eE][-+]?[0-9]+)?$/.test(t.trim()) ? t.trim() : '';
}

async function compileAndRun(code, sanitize, threshold) {
  fs.writeFileSync(SNIPPET_C, code, 'utf8');
  const src = '/workspace/demo/live/snippet.c';
  const bin = '/workspace/demo/live/snippet_bin';
  const flags = sanitize ? '-g -fsanitize=numerical' : '-g';
  const thr = safeThreshold(threshold);
  const env = (sanitize && thr) ? `NSSAN_THRESHOLD=${thr} ` : '';
  // Compile (merge stderr) then run (merge stderr). Report each phase.
  const script =
    `set +e; ` +
    `COMPILE_OUT=$(${CLANG} ${flags} ${src} -lm -o ${bin} 2>&1); COMPILE_RC=$?; ` +
    `if [ $COMPILE_RC -ne 0 ]; then echo "@@PHASE:compile-error"; echo "$COMPILE_OUT"; exit 0; fi; ` +
    `echo "@@PHASE:run"; ${env}${bin} 2>&1; echo "@@EXIT:$?"`;
  const r = await dockerRun(script, 90000);
  let out = r.stdout;
  if (r.err && r.err.killed) out += '\n@@TIMEOUT (killed after 90s)';
  else if (r.err && !out) out += (r.stderr || String(r.err));
  return out;
}

function send(res, code, type, body) {
  res.writeHead(code, { 'Content-Type': type, 'Cache-Control': 'no-store' });
  res.end(body);
}

const server = http.createServer((req, res) => {
  if (req.method === 'GET' && (req.url === '/' || req.url === '/index.html')) {
    fs.readFile(PAGE, (e, buf) => {
      if (e) return send(res, 500, 'text/plain', 'playground page missing');
      send(res, 200, 'text/html; charset=utf-8', buf);
    });
    return;
  }

  if (req.method === 'GET' && req.url === '/status') {
    send(res, 200, 'application/json', JSON.stringify({ ready, warming }));
    return;
  }

  if (req.method === 'POST' && req.url === '/run') {
    let body = '';
    req.on('data', (c) => { body += c; if (body.length > 200000) req.destroy(); });
    req.on('end', async () => {
      let payload;
      try { payload = JSON.parse(body); } catch (_) { return send(res, 400, 'application/json', '{"error":"bad json"}'); }
      const code = String(payload.code || '');
      const sanitize = payload.sanitize !== false;
      const threshold = payload.threshold;
      try {
        if (!ready) { await warmup(); }
        if (!ready) return send(res, 200, 'application/json', JSON.stringify({ output: 'Toolchain not ready. Is Docker running? Try again in a moment.' }));
        const output = await compileAndRun(code, sanitize, threshold);
        send(res, 200, 'application/json', JSON.stringify({ output, ready }));
      } catch (e) {
        send(res, 200, 'application/json', JSON.stringify({ output: 'Server error: ' + e.message }));
      }
    });
    return;
  }

  if (req.method === 'POST' && req.url === '/bench') {
    (async () => {
      try {
        if (!ready) { await warmup(); }
        if (!ready) return send(res, 200, 'application/json', JSON.stringify({ output: 'Toolchain not ready. Is Docker running?' }));
        const r = await dockerRun('bash ./benchmark.sh 2>&1', 360000);
        let out = r.stdout || '';
        if (r.err && r.err.killed) out += '\n@@TIMEOUT (benchmark exceeded 6 min)';
        else if (r.err && !out) out += String(r.err);
        send(res, 200, 'application/json', JSON.stringify({ output: out }));
      } catch (e) {
        send(res, 200, 'application/json', JSON.stringify({ output: 'Server error: ' + e.message }));
      }
    })();
    return;
  }

  send(res, 404, 'text/plain', 'not found');
});

server.listen(PORT, () => {
  console.log(`NSSan live playground on http://localhost:${PORT}`);
  console.log('Warming up the toolchain (installing NSSan into cached clang)...');
  warmup().then((ok) => {
    console.log(ok ? 'Toolchain ready. Open the page and hit Run.' : 'Warmup failed — check that Docker is running and the image is built.');
  });
});
