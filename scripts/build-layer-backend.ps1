param([switch]$PrepareToolchain)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repo=Split-Path -Parent $PSScriptRoot
$pin='9b18f57344ff70d6f5aa640a0db7bc55596d5324'
$upstream=Join-Path $repo 'artifacts/upstream-photocraft'
if(-not(Test-Path -LiteralPath (Join-Path $upstream '.git'))){
    & git clone --filter=blob:none --no-checkout https://github.com/storytold/photocraft.git $upstream
    if($LASTEXITCODE){throw 'PhotoCraft源码获取失败'}
    & git -C $upstream sparse-checkout set crates apps xtask .cargo assets/fonts assets/dict
    & git -C $upstream checkout --detach $pin
    if($LASTEXITCODE){throw 'PhotoCraft固定版本检出失败'}
}
if((& git -C $upstream rev-parse HEAD).Trim() -ne $pin){throw 'PhotoCraft源码版本不匹配；不会覆盖已有参考检出'}
$entry=Join-Path $upstream 'crates/io/src/lib.rs'
$base=((& git -C $upstream show HEAD:crates/io/src/lib.rs) -join "`n")+"`n"
$signature='fn import_psd(name: &str, bytes: &[u8], ctl: &photocraft_raster::Interrupt) -> Result<ImportResult, IoError> {'
if(([regex]::Matches($base,[regex]::Escape($signature))).Count -ne 1){throw '固定上游PSD导入入口不唯一'}
$expected=$base.Replace($signature,'pub '+$signature)
$actual=[IO.File]::ReadAllText($entry).Replace("`r`n","`n")
$dirty=@(& git -C $upstream status --porcelain)
if($actual -eq $base -and -not $dirty.Count){
    & git -C $upstream apply --check (Join-Path $PSScriptRoot 'photocraft-psd-entry.patch')
    if($LASTEXITCODE){throw 'PhotoCraft入口补丁检查失败'}
    & git -C $upstream apply (Join-Path $PSScriptRoot 'photocraft-psd-entry.patch')
    if($LASTEXITCODE){throw 'PhotoCraft入口补丁应用失败'}
    $actual=[IO.File]::ReadAllText($entry).Replace("`r`n","`n")
    $dirty=@(& git -C $upstream status --porcelain)
}
if($actual -ne $expected -or $dirty.Count -ne 1 -or $dirty[0].Trim() -ne 'M crates/io/src/lib.rs'){
    throw 'PhotoCraft参考检出含非预期修改；只允许公开PSD原有入口的一行补丁，不会覆盖其他改动'
}
$toolDir=Join-Path $repo 'artifacts/rust-toolchain'
$rustLocal=Join-Path $toolDir 'local/bin/rustc.exe'
$cargoLocal=Join-Path $toolDir 'local/bin/cargo.exe'
$cargo=Get-Command cargo.exe -ErrorAction SilentlyContinue
$rust=Get-Command rustc.exe -ErrorAction SilentlyContinue
if(Test-Path -LiteralPath $cargoLocal){$cargo=$cargoLocal;$rust=$rustLocal}
elseif($cargo -and $rust){$cargo=$cargo.Source;$rust=$rust.Source}
elseif($PrepareToolchain){
    [void](New-Item -ItemType Directory -Path $toolDir -Force)
    $archive=Join-Path $toolDir 'rust.tar.xz'
    $sha='D36A10EBDC894D347F103E6553B65058D2D92DDF1132B64189C458ECEF164231'
    if(-not(Test-Path -LiteralPath $archive)){
        & curl.exe -L --fail --retry 2 --max-time 900 https://static.rust-lang.org/dist/rust-1.95.0-x86_64-pc-windows-msvc.tar.xz -o $archive
        if($LASTEXITCODE){throw 'Rust官方便携工具链下载失败'}
    }
    if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $sha){throw 'Rust工具链SHA-256不匹配'}
    $prefix='rust-1.95.0-x86_64-pc-windows-msvc'
    & tar -xf $archive -C $toolDir "$prefix/rustc" "$prefix/cargo" "$prefix/rust-std-x86_64-pc-windows-msvc"
    if($LASTEXITCODE){throw 'Rust便携工具链解压失败'}
    $local=Join-Path $toolDir 'local';[void](New-Item -ItemType Directory -Path $local -Force)
    Copy-Item -LiteralPath (Join-Path $toolDir "$prefix/rustc/bin") -Destination $local -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $toolDir "$prefix/rustc/lib") -Destination $local -Recurse -Force
    Copy-Item (Join-Path $toolDir "$prefix/cargo/bin/*") (Join-Path $local 'bin') -Force
    Copy-Item (Join-Path $toolDir "$prefix/rust-std-x86_64-pc-windows-msvc/lib/*") (Join-Path $local 'lib') -Recurse -Force
    $cargo=$cargoLocal;$rust=$rustLocal
}else{throw '需要Rust 1.95或更新版；先运行 scripts/build-layer-backend.ps1 -PrepareToolchain，仅安装在本项目artifacts，不修改系统环境'}
$oldRust=$env:RUSTC;$oldHome=$env:CARGO_HOME;$oldTarget=$env:CARGO_TARGET_DIR;$oldFlags=$env:RUSTFLAGS
try{
    $env:RUSTC=$rust;$env:CARGO_HOME=Join-Path $repo 'artifacts/cargo-home';$env:CARGO_TARGET_DIR=Join-Path $repo 'artifacts/layer-backend-target'
    $env:RUSTFLAGS='-C target-feature=+crt-static'
    & $cargo test --locked --release --manifest-path (Join-Path $repo 'tools/layer-backend/Cargo.toml')
    if($LASTEXITCODE){throw 'PhotoCraft C ABI回归失败'}
    & $cargo build --locked --release --manifest-path (Join-Path $repo 'tools/layer-backend/Cargo.toml')
    if($LASTEXITCODE){throw 'PhotoCraft图层后端构建失败'}
    $destination=Join-Path $repo 'x64/Release';[void](New-Item -ItemType Directory -Path $destination -Force)
    Copy-Item -LiteralPath (Join-Path $env:CARGO_TARGET_DIR 'release/YeLayerBackend.dll') -Destination $destination -Force
    & (Join-Path $PSScriptRoot 'write-layer-backend-notices.ps1') -Cargo $cargo -Destination (Join-Path $destination 'LAYER_BACKEND_NOTICES.txt')
    Write-Host "PASS PhotoCraft源码固定于 $pin；后端独立DLL，未修改系统Rust环境。"
}finally{$env:RUSTC=$oldRust;$env:CARGO_HOME=$oldHome;$env:CARGO_TARGET_DIR=$oldTarget;$env:RUSTFLAGS=$oldFlags}
