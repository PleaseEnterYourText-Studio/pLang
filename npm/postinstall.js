// pLang compiler 安装脚本：检测 LLVM，配置 plc 的 LLVM 动态库路径
// - macOS: 查找 brew LLVM（Intel /opt/... 与 ARM /opt/homebrew/...），用 install_name_tool 改写
// - Linux: 查找 libLLVM.so，设置 rpath
// - Windows: 查找 LLVM.dll，设置 PATH 提示
'use strict';

const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const binDir = path.join(__dirname, 'bin');
const libDir = path.join(__dirname, 'lib');

function log(msg) { console.log('[plang] ' + msg); }
function warn(msg) { console.warn('[plang] WARN: ' + msg); }

function findLLVMDir() {
  const candidates = [
    process.env.LLVM_HOME,
    '/opt/homebrew/opt/llvm',          // Apple Silicon brew
    '/usr/local/opt/llvm',             // Intel brew
    '/usr/lib/llvm-18', '/usr/lib/llvm-17', '/usr/lib/llvm-16',  // Linux
    '/usr/local/llvm',
  ].filter(Boolean);
  for (const dir of candidates) {
    if (fs.existsSync(dir)) return dir;
  }
  return null;
}

function findLib(dir) {
  const names = ['libLLVM.dylib', 'libLLVM.so', 'LLVM.dll'];
  for (const n of names) {
    const p = path.join(dir, 'lib', n);
    if (fs.existsSync(p)) return p;
  }
  // libLLVM 可能在 lib/ 下不同子目录（如 lib/llvm-18）
  try {
    for (const sub of fs.readdirSync(path.join(dir, 'lib'))) {
      const subDir = path.join(dir, 'lib', sub);
      if (fs.statSync(subDir).isDirectory()) {
        for (const n of names) {
          const p = path.join(subDir, n);
          if (fs.existsSync(p)) return p;
        }
      }
    }
  } catch (e) { /* ignore */ }
  return null;
}

function main() {
  const platform = process.platform;

  // 1. 标准库：确保随包一起
  const stdDir = path.join(__dirname, 'std');
  if (!fs.existsSync(stdDir)) {
    warn('std/ not found in package');
  }

  // 2. LLVM：查找
  const llvmDir = findLLVMDir();
  if (!llvmDir) {
    warn('LLVM not found. pLang requires LLVM.');
    warn('  macOS:  brew install llvm');
    warn('  Linux:  sudo apt install llvm  (or llvm-18-dev)');
    warn('  Windows: install LLVM from https://llvm.org and set LLVM_HOME');
    log('You can set LLVM_HOME to point at your LLVM installation.');
    return; // 不硬失败：用户可能已手动配置
  }
  const llvmLib = findLib(llvmDir);
  if (!llvmLib) {
    warn('LLVM found at ' + llvmDir + ' but libLLVM not located. Set LLVM_HOME.');
    return;
  }
  log('Found LLVM: ' + llvmLib);

  // 3. 改写 plc 的链接路径
  const plcPath = path.join(binDir, platform === 'win32' ? 'plc.exe' : 'plc');
  if (!fs.existsSync(plcPath)) {
    warn('plc binary not found at ' + plcPath);
    return;
  }
  try {
    if (platform === 'darwin') {
      // 把硬编码的 LLVM 路径改为用户机器上的实际路径
      execSync(`install_name_tool -change /usr/local/opt/llvm/lib/libLLVM.dylib "${llvmLib}" "${plcPath}"`);
      execSync(`install_name_tool -change /opt/homebrew/opt/llvm/lib/libLLVM.dylib "${llvmLib}" "${plcPath}"`);
      log('plc -> ' + llvmLib);
    } else if (platform === 'linux') {
      execSync(`patchelf --set-rpath "${path.dirname(llvmLib)}" "${plcPath}"`);
      log('plc rpath -> ' + path.dirname(llvmLib));
    } else if (platform === 'win32') {
      // Windows: 复制 LLVM.dll 到 bin 旁边（或提示）
      const dll = findLib(llvmDir);
      if (dll) {
        fs.copyFileSync(dll, path.join(binDir, 'LLVM.dll'));
        log('copied LLVM.dll to bin/');
      }
    }
  } catch (e) {
    warn('Failed to configure LLVM path: ' + e.message);
    warn('Set LLVM_HOME or configure manually.');
  }
}

main();
