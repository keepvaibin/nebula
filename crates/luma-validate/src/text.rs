//! The messages, compiled from `localization/NebulaText.tsv` by build.rs.

include!(concat!(env!("OUT_DIR"), "/validator_text.rs"));

/// The text of `id` (a code `L203` or a phrase `Json.Comment`) in the language at `lang` (an index of LANGUAGES).
/// An id with no row gives the id in braces; the test below makes sure none is missing.
pub(crate) fn get(id: &str, lang: usize) -> String {
    match TEXT.binary_search_by(|(other, _)| (*other).cmp(id)) {
        Ok(i) => TEXT[i].1[lang.min(13)].to_string(),
        Err(_) => format!("{{{id}}}"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The phrase ids written in the sources: string literals of the form `Word.Word` with capitals.
    fn phrase_ids() -> Vec<String> {
        let sources = [
            include_str!("json.rs"),
            include_str!("lib.rs"),
            include_str!("zip.rs"),
            include_str!("binary.rs"),
            include_str!("source.rs"),
            include_str!("rules/mod.rs"),
            include_str!("rules/container.rs"),
            include_str!("rules/manifest.rs"),
            include_str!("rules/layers.rs"),
            include_str!("rules/patches.rs"),
            include_str!("rules/code.rs"),
            include_str!("rules/kit.rs"),
            include_str!("rules/mods.rs"),
        ];
        let mut ids = Vec::new();
        for source in sources {
            // Escaped quotes and the quote character literal would shift which pieces are inside strings.
            let source = source.replace("\\\"", "").replace("'\"'", "");
            for piece in source.split('"').skip(1).step_by(2) {
                let mut parts = piece.split('.');
                let first = parts.next().unwrap_or("");
                let looks = piece.contains('.')
                    && first.chars().next().is_some_and(|c| c.is_ascii_uppercase())
                    && piece.split('.').all(|p| !p.is_empty() && p.chars().all(|c| c.is_ascii_alphanumeric()))
                    && piece.split('.').all(|p| p.chars().next().is_some_and(|c| c.is_ascii_uppercase()));
                if looks {
                    ids.push(piece.to_string());
                }
            }
        }
        ids.sort();
        ids.dedup();
        ids
    }

    #[test]
    fn every_message_has_a_row() {
        let mut missing = Vec::new();
        for code in crate::CODES {
            if TEXT.binary_search_by(|(id, _)| (*id).cmp(&format!("L{code}"))).is_err() {
                missing.push(format!("L{code}"));
            }
        }
        for id in phrase_ids() {
            if TEXT.binary_search_by(|(other, _)| (*other).cmp(&id)).is_err() {
                missing.push(id);
            }
        }
        assert!(missing.is_empty(), "no Validator.* row in localization/NebulaText.tsv for: {}", missing.join(", "));
    }
}
