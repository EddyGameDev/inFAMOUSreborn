//! `inFamous.exe`: starts the game the way `installer/launch.txt` says, without a console window.
#![cfg_attr(windows, windows_subsystem = "windows")]

use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

fn message(text: &str) {
    #[cfg(windows)]
    {
        #[link(name = "user32")]
        extern "system" {
            fn MessageBoxW(
                hwnd: *mut core::ffi::c_void,
                text: *const u16,
                caption: *const u16,
                kind: u32,
            ) -> i32;
        }
        let w = |s: &str| s.encode_utf16().chain(Some(0)).collect::<Vec<u16>>();
        let (t, c) = (w(text), w("inFamous"));
        // SAFETY: both strings are NUL-terminated UTF-16; MB_OK | MB_ICONERROR.
        unsafe { MessageBoxW(core::ptr::null_mut(), t.as_ptr(), c.as_ptr(), 0x10) };
    }
    #[cfg(not(windows))]
    eprintln!("{text}");
}

fn resolve(root: &Path, p: &str) -> PathBuf {
    let pb = PathBuf::from(p);
    if pb.is_absolute() {
        pb
    } else {
        p.split('/')
            .filter(|s| !s.is_empty())
            .fold(root.to_path_buf(), |a, s| a.join(s))
    }
}

fn run() -> Result<(), String> {
    let me = std::env::current_exe().map_err(|e| e.to_string())?;
    let root = me.parent().ok_or("no folder")?.to_path_buf();
    let cfg = root.join("installer").join("launch.txt");
    let txt = std::fs::read_to_string(&cfg)
        .map_err(|_| "The game has not been built yet.\n\nRun inFamous Setup again to finish the installation.".to_string())?;
    let (mut exe, mut cwd, mut out, mut err) = (None, None, None, None);
    let mut args = Vec::new();
    let mut env = Vec::new();
    for line in txt.lines() {
        let line = line.trim_end();
        if line.starts_with('#') || line.is_empty() {
            continue;
        }
        let Some((k, v)) = line.split_once('=') else {
            continue;
        };
        match k {
            "exe" => exe = Some(resolve(&root, v)),
            "cwd" => cwd = Some(resolve(&root, v)),
            "arg" => args.push(resolve(&root, v)),
            "stdout" => out = Some(v.to_string()),
            "stderr" => err = Some(v.to_string()),
            "env" => {
                if let Some((a, b)) = v.split_once('=') {
                    env.push((a.to_string(), b.to_string()));
                }
            }
            _ => {}
        }
    }
    let exe = exe.ok_or("launch.txt names no game executable")?;
    if !exe.is_file() {
        return Err(format!(
            "{} is missing.\n\nRun inFamous Setup again to rebuild the game.",
            exe.display()
        ));
    }
    let cwd = cwd.unwrap_or_else(|| root.clone());
    let mut c = Command::new(&exe);
    c.args(&args)
        .current_dir(&cwd)
        .envs(env)
        .stdin(Stdio::null());
    let open = |name: &Option<String>| -> Stdio {
        match name
            .as_deref()
            .map(|n| resolve(&cwd, n))
            .and_then(|p| std::fs::File::create(p).ok())
        {
            Some(f) => Stdio::from(f),
            None => Stdio::null(),
        }
    };
    c.stdout(open(&out)).stderr(open(&err));
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        c.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
    }
    c.spawn()
        .map_err(|e| format!("Could not start {}: {e}", exe.display()))?;
    Ok(())
}

fn main() {
    if let Err(e) = run() {
        message(&e);
        std::process::exit(1);
    }
}
