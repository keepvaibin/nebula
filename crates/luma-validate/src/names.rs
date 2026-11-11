//! What the game has, by name: the self-description (plan 1.8) as far as it exists. Until 1.8's file is signed, the
//! function rows come from the kernel's `.rows` files (one line per function: name, signature, `hook` or `tell`,
//! source line; `tell` rows are constructors and destructors, which take `before` and `after` only).

use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};

/// Why a folder of `.rows` files does not read: what a tool says in its own language.
#[derive(Debug)]
pub enum RowsError {
    /// A folder or file that cannot be read.
    Read { path: PathBuf, error: std::io::Error },
    /// A line that is not four tab-separated fields (name, signature, `hook` or `tell`, source line); 1-based.
    Line { path: PathBuf, line: usize },
}

impl std::fmt::Display for RowsError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            RowsError::Read { path, error } => write!(f, "{}: {error}", path.display()),
            RowsError::Line { path, line } => write!(f, "{} line {line}: not name, signature, hook or tell, source line", path.display()),
        }
    }
}

impl std::error::Error for RowsError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            RowsError::Read { error, .. } => Some(error),
            RowsError::Line { .. } => None,
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct Names {
    /// Each function's signatures, with whether the row is a constructor or destructor (`tell`).
    pub functions: HashMap<String, Vec<(String, bool)>>,
    /// `Class.field` to its type; None until the self-description lists fields (1.2, 1.8).
    pub fields: Option<HashMap<String, String>>,
    /// Class names; None until the self-description lists classes (1.2, 1.8).
    pub classes: Option<HashSet<String>>,
    /// The services' permission names; None until the service registry (2.1) lists them.
    pub permissions: Option<Vec<String>>,
    /// The LumaCore version mods are checked against (`LUMA_API`).
    pub api: u32,
}

impl Names {
    /// Reads every `*.rows` file under `folder`. A line that is not four tab-separated fields is an error naming it.
    pub fn from_rows_folder(folder: &Path, api: u32) -> Result<Names, RowsError> {
        let mut names = Names { api, ..Names::default() };
        let mut stack = vec![folder.to_path_buf()];
        while let Some(dir) = stack.pop() {
            let read = |error| RowsError::Read { path: dir.clone(), error };
            for entry in std::fs::read_dir(&dir).map_err(read)? {
                let path = entry.map_err(read)?.path();
                if path.is_dir() {
                    stack.push(path);
                } else if path.extension().is_some_and(|e| e == "rows") {
                    let text = std::fs::read_to_string(&path).map_err(|error| RowsError::Read { path: path.clone(), error })?;
                    names.add_rows(&text).map_err(|line| RowsError::Line { path: path.clone(), line })?;
                }
            }
        }
        Ok(names)
    }

    /// Adds the rows of one `.rows` text; Err gives the line number of a malformed line.
    pub fn add_rows(&mut self, text: &str) -> Result<(), usize> {
        for (n, line) in text.lines().enumerate() {
            if line.is_empty() {
                continue;
            }
            let fields: Vec<&str> = line.split('\t').collect();
            if fields.len() != 4 || !matches!(fields[2], "hook" | "tell") {
                return Err(n + 1);
            }
            let list = self.functions.entry(fields[0].to_string()).or_default();
            if !list.iter().any(|(s, _)| s == fields[1]) {
                list.push((fields[1].to_string(), fields[2] == "tell"));
            }
        }
        Ok(())
    }
}
