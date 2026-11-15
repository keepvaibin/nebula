//! Where a glTF buffer's or image's `uri` points: the one reading of it, for the cook (nebula-import gltf.rs, which
//! reads the file) and for L304 (which knows a file a model names is that model's).

/// What a `uri` of a model names.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ModelUri<'a> {
    /// Inline data (`data:...`); the rest after `data:`.
    Inline(&'a str),
    /// A file beside the model: its path relative to the model's folder, `/` between parts, percent escapes decoded,
    /// with no `.` or empty part.
    File(String),
    /// Not a file beside the model: a parent step (`..` as a part), an absolute path or a drive, a scheme (`:`), a
    /// backslash (a folder on one system, part of a name on another), an escape that is not UTF-8, or nothing.
    Leaves,
}

/// Reads `uri` as glTF 2.0 writes it (a URI reference, percent-encoded): a relative path of `/`-separated parts.
pub fn model_uri(uri: &str) -> ModelUri<'_> {
    if let Some(rest) = uri.strip_prefix("data:") {
        return ModelUri::Inline(rest);
    }
    if uri.starts_with('/') || uri.contains(':') || uri.contains('\\') {
        return ModelUri::Leaves;
    }
    let Some(decoded) = percent_decoded(uri) else { return ModelUri::Leaves };
    if decoded.contains(['\\', ':', '\0']) {
        return ModelUri::Leaves;
    }
    let mut parts = Vec::new();
    for part in decoded.split('/') {
        match part {
            "" | "." => {}
            ".." => return ModelUri::Leaves,
            _ => parts.push(part),
        }
    }
    if parts.is_empty() {
        return ModelUri::Leaves;
    }
    ModelUri::File(parts.join("/"))
}

/// `%XX` escapes decoded (a `%` not followed by two hex digits stays as written); None when the bytes are not UTF-8.
fn percent_decoded(text: &str) -> Option<String> {
    let bytes = text.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        let hex = |b: u8| (b as char).to_digit(16);
        if bytes[i] == b'%' && i + 2 < bytes.len() {
            if let (Some(h), Some(l)) = (hex(bytes[i + 1]), hex(bytes[i + 2])) {
                out.push((h * 16 + l) as u8);
                i += 3;
                continue;
            }
        }
        out.push(bytes[i]);
        i += 1;
    }
    String::from_utf8(out).ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn each_uri_is_read_one_way() {
        let file = |s: &str| ModelUri::File(s.to_string());
        let table: [(&str, ModelUri); 16] = [
            ("Ball.bin", file("Ball.bin")),
            ("./tex/Ball.png", file("tex/Ball.png")),
            ("tex//Ball.png", file("tex/Ball.png")),
            ("my%20tex.png", file("my tex.png")),
            ("%E3%81%82.png", file("\u{3042}.png")),
            ("100%.png", file("100%.png")),
            ("a..b.png", file("a..b.png")),
            ("tex/../Ball.png", ModelUri::Leaves),
            ("..", ModelUri::Leaves),
            ("/etc/Ball.png", ModelUri::Leaves),
            ("C:/Ball.png", ModelUri::Leaves),
            ("http://a.org/Ball.png", ModelUri::Leaves),
            ("tex\\Ball.png", ModelUri::Leaves),
            ("tex%5CBall.png", ModelUri::Leaves),
            ("%FF.png", ModelUri::Leaves),
            ("data:application/octet-stream;base64,AA==", ModelUri::Inline("application/octet-stream;base64,AA==")),
        ];
        for (uri, want) in table {
            assert_eq!(model_uri(uri), want, "{uri}");
        }
        assert_eq!(model_uri("."), ModelUri::Leaves);
        assert_eq!(model_uri("%2E%2E/x.bin"), ModelUri::Leaves);
    }
}
