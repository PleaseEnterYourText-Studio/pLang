// 构建 npm 包：从 cmake-build-debug 收集产物到 npm/ 目录
// 用法：node scripts/build-package.js [buildDir]
'use strict';

const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
const buildDir = process.argv[2] || path.join(root, '..', 'cmake-build-debug');

function cp(src, dst) {
  if (!fs.existsSync(src)) {
    console.error('[build] missing: ' + src);
    process.exit(1);
  }
  fs.mkdirSync(path.dirname(dst), { recursive: true });
  fs.copyFileSync(src, dst);
  console.log('[build] ' + path.basename(src) + ' -> ' + path.relative(root, dst));
}

// plc / lsp-server 可执行文件
const isWin = process.platform === 'win32';
cp(path.join(buildDir, isWin ? 'plc.exe' : 'plc'), path.join(root, 'bin', isWin ? 'plc.exe' : 'plc'));
cp(path.join(buildDir, isWin ? 'lsp-server.exe' : 'lsp-server'), path.join(root, 'bin', isWin ? 'lsp-server.exe' : 'lsp-server'));

// 标准库源码
const stdSrc = path.join(root, '..', 'std');
if (fs.existsSync(stdSrc)) {
  fs.cpSync(stdSrc, path.join(root, 'std'), { recursive: true });
  console.log('[build] std/ -> npm/std/');
}

console.log('[build] done.');
