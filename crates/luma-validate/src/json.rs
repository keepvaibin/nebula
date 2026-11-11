//! A strict JSON reader that keeps where every value is. `luma.json` and patches are read with it so that each
//! finding names its line and column (luma-format 7). Strict: UTF-8 only, no byte-order mark, no comments, no
//! trailing commas, no duplicate keys, nesting at most `MAX_DEPTH` deep. Never panics on any input.

/// Where a value starts: line and column, both from 1; the column counts characters, not bytes.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub struct Pos {
    pub line: u32,
    pub column: u32,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Kind {
    Null,
    Bool(bool),
    /// The number's value and its text as written (for integers past 2^53 and for messages).
    Number(f64, String),
    String(String),
    Array(Vec<Value>),
    /// Members in the order written; keys are unique (a duplicate is an error).
    Object(Vec<Member>),
}

#[derive(Clone, Debug, PartialEq)]
pub struct Member {
    pub key: String,
    pub key_pos: Pos,
    pub value: Value,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Value {
    pub kind: Kind,
    pub pos: Pos,
}

/// Why a text is not JSON. `what` is a phrase id (messages.rs), `detail` a literal for it (a key, a character).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Error {
    pub pos: Pos,
    pub what: &'static str,
    pub detail: String,
}

pub const MAX_DEPTH: usize = 64;

impl Value {
    pub fn get(&self, key: &str) -> Option<&Value> {
        match &self.kind {
            Kind::Object(members) => members.iter().find(|m| m.key == key).map(|m| &m.value),
            _ => None,
        }
    }
    pub fn member(&self, key: &str) -> Option<&Member> {
        match &self.kind {
            Kind::Object(members) => members.iter().find(|m| m.key == key),
            _ => None,
        }
    }
    pub fn members(&self) -> &[Member] {
        match &self.kind {
            Kind::Object(members) => members,
            _ => &[],
        }
    }
    pub fn items(&self) -> &[Value] {
        match &self.kind {
            Kind::Array(items) => items,
            _ => &[],
        }
    }
    pub fn as_str(&self) -> Option<&str> {
        match &self.kind {
            Kind::String(s) => Some(s),
            _ => None,
        }
    }
    pub fn as_bool(&self) -> Option<bool> {
        match self.kind {
            Kind::Bool(b) => Some(b),
            _ => None,
        }
    }
    pub fn as_f64(&self) -> Option<f64> {
        match self.kind {
            Kind::Number(n, _) => Some(n),
            _ => None,
        }
    }
    /// An integer written without a fraction or exponent, in i64's range.
    pub fn as_i64(&self) -> Option<i64> {
        match &self.kind {
            Kind::Number(_, text) if !text.contains(['.', 'e', 'E']) => text.parse().ok(),
            _ => None,
        }
    }
    pub fn is_object(&self) -> bool {
        matches!(self.kind, Kind::Object(_))
    }
    pub fn is_array(&self) -> bool {
        matches!(self.kind, Kind::Array(_))
    }
    /// The JSON type's name, as a phrase id for messages.
    pub fn type_phrase(&self) -> &'static str {
        match self.kind {
            Kind::Null => "Type.Null",
            Kind::Bool(_) => "Type.Bool",
            Kind::Number(..) => "Type.Number",
            Kind::String(_) => "Type.String",
            Kind::Array(_) => "Type.Array",
            Kind::Object(_) => "Type.Object",
        }
    }
}

pub fn parse(bytes: &[u8]) -> Result<Value, Error> {
    let text = match std::str::from_utf8(bytes) {
        Ok(text) => text,
        Err(e) => {
            let pos = position_of(bytes, e.valid_up_to());
            return Err(Error { pos, what: "Json.NotUtf8", detail: String::new() });
        }
    };
    if text.starts_with('\u{feff}') {
        return Err(Error { pos: Pos { line: 1, column: 1 }, what: "Json.Bom", detail: String::new() });
    }
    let mut p = Parser { chars: text.char_indices().peekable(), text, line: 1, column: 1 };
    p.skip_space()?;
    let value = p.value(0)?;
    p.skip_space()?;
    if let Some(c) = p.peek() {
        return Err(p.error_here("Json.AfterEnd", c.to_string()));
    }
    Ok(value)
}

/// Line and column of a byte offset in text that is UTF-8 up to it.
fn position_of(bytes: &[u8], offset: usize) -> Pos {
    let before = std::str::from_utf8(&bytes[..offset.min(bytes.len())]).unwrap_or("");
    let line = before.matches('\n').count() as u32 + 1;
    let column = before.rsplit('\n').next().map_or(0, |l| l.chars().count()) as u32 + 1;
    Pos { line, column }
}

struct Parser<'a> {
    chars: std::iter::Peekable<std::str::CharIndices<'a>>,
    #[allow(dead_code)]
    text: &'a str,
    line: u32,
    column: u32,
}

impl Parser<'_> {
    fn peek(&mut self) -> Option<char> {
        self.chars.peek().map(|&(_, c)| c)
    }
    fn pos(&self) -> Pos {
        Pos { line: self.line, column: self.column }
    }
    fn next(&mut self) -> Option<char> {
        let (_, c) = self.chars.next()?;
        if c == '\n' {
            self.line += 1;
            self.column = 1;
        } else {
            self.column += 1;
        }
        Some(c)
    }
    fn error_here(&self, what: &'static str, detail: String) -> Error {
        Error { pos: self.pos(), what, detail }
    }
    fn skip_space(&mut self) -> Result<(), Error> {
        while let Some(c) = self.peek() {
            match c {
                ' ' | '\t' | '\n' | '\r' => {
                    self.next();
                }
                '/' => return Err(self.error_here("Json.Comment", String::new())),
                '#' => return Err(self.error_here("Json.Comment", String::new())),
                _ => break,
            }
        }
        Ok(())
    }
    fn expect(&mut self, want: char) -> Result<(), Error> {
        match self.peek() {
            Some(c) if c == want => {
                self.next();
                Ok(())
            }
            Some(_) => Err(self.error_here("Json.Expected", want.to_string())),
            None => Err(self.error_here("Json.Truncated", String::new())),
        }
    }
    fn value(&mut self, depth: usize) -> Result<Value, Error> {
        if depth >= MAX_DEPTH {
            return Err(self.error_here("Json.TooDeep", MAX_DEPTH.to_string()));
        }
        let pos = self.pos();
        let kind = match self.peek() {
            None => return Err(self.error_here("Json.Truncated", String::new())),
            Some('{') => self.object(depth)?,
            Some('[') => self.array(depth)?,
            Some('"') => Kind::String(self.string()?),
            Some('t') => self.word("true", Kind::Bool(true))?,
            Some('f') => self.word("false", Kind::Bool(false))?,
            Some('n') => self.word("null", Kind::Null)?,
            Some(c) if c == '-' || c.is_ascii_digit() => self.number()?,
            Some(c) => return Err(self.error_here("Json.Unexpected", c.to_string())),
        };
        Ok(Value { kind, pos })
    }
    fn word(&mut self, word: &str, kind: Kind) -> Result<Kind, Error> {
        for want in word.chars() {
            match self.peek() {
                Some(c) if c == want => {
                    self.next();
                }
                Some(c) => return Err(self.error_here("Json.Unexpected", c.to_string())),
                None => return Err(self.error_here("Json.Truncated", String::new())),
            }
        }
        Ok(kind)
    }
    fn number(&mut self) -> Result<Kind, Error> {
        let mut text = String::new();
        if self.peek() == Some('-') {
            text.push('-');
            self.next();
        }
        // Integer part: 0, or a digit 1-9 and more digits.
        match self.peek() {
            Some('0') => {
                text.push('0');
                self.next();
                if self.peek().is_some_and(|c| c.is_ascii_digit()) {
                    return Err(self.error_here("Json.LeadingZero", String::new()));
                }
            }
            Some(c) if c.is_ascii_digit() => self.digits(&mut text),
            Some(c) => return Err(self.error_here("Json.Unexpected", c.to_string())),
            None => return Err(self.error_here("Json.Truncated", String::new())),
        }
        if self.peek() == Some('.') {
            text.push('.');
            self.next();
            if !self.peek().is_some_and(|c| c.is_ascii_digit()) {
                return Err(self.error_here("Json.BadNumber", text));
            }
            self.digits(&mut text);
        }
        if matches!(self.peek(), Some('e' | 'E')) {
            text.push('e');
            self.next();
            if let Some(sign @ ('+' | '-')) = self.peek() {
                text.push(sign);
                self.next();
            }
            if !self.peek().is_some_and(|c| c.is_ascii_digit()) {
                return Err(self.error_here("Json.BadNumber", text));
            }
            self.digits(&mut text);
        }
        let value: f64 = text.parse().unwrap_or(f64::NAN);
        if !value.is_finite() {
            return Err(self.error_here("Json.BadNumber", text));
        }
        Ok(Kind::Number(value, text))
    }
    fn digits(&mut self, text: &mut String) {
        while let Some(c) = self.peek().filter(|c| c.is_ascii_digit()) {
            text.push(c);
            self.next();
        }
    }
    fn string(&mut self) -> Result<String, Error> {
        self.expect('"')?;
        let mut out = String::new();
        loop {
            let at = self.pos();
            match self.next() {
                None => return Err(Error { pos: at, what: "Json.Truncated", detail: String::new() }),
                Some('"') => return Ok(out),
                Some('\\') => match self.next() {
                    Some('"') => out.push('"'),
                    Some('\\') => out.push('\\'),
                    Some('/') => out.push('/'),
                    Some('b') => out.push('\u{8}'),
                    Some('f') => out.push('\u{c}'),
                    Some('n') => out.push('\n'),
                    Some('r') => out.push('\r'),
                    Some('t') => out.push('\t'),
                    Some('u') => {
                        let first = self.hex4(at)?;
                        let c = if (0xd800..0xdc00).contains(&first) {
                            // A high surrogate must be followed by \u and a low one.
                            if self.next() != Some('\\') || self.next() != Some('u') {
                                return Err(Error { pos: at, what: "Json.BadEscape", detail: String::new() });
                            }
                            let second = self.hex4(at)?;
                            if !(0xdc00..0xe000).contains(&second) {
                                return Err(Error { pos: at, what: "Json.BadEscape", detail: String::new() });
                            }
                            char::from_u32(0x10000 + ((first - 0xd800) << 10) + (second - 0xdc00))
                        } else {
                            char::from_u32(first)
                        };
                        match c {
                            Some(c) => out.push(c),
                            None => return Err(Error { pos: at, what: "Json.BadEscape", detail: String::new() }),
                        }
                    }
                    Some(_) | None => return Err(Error { pos: at, what: "Json.BadEscape", detail: String::new() }),
                },
                Some(c) if (c as u32) < 0x20 => {
                    return Err(Error { pos: at, what: "Json.ControlInString", detail: format!("U+{:04X}", c as u32) })
                }
                Some(c) => out.push(c),
            }
        }
    }
    fn hex4(&mut self, at: Pos) -> Result<u32, Error> {
        let mut value = 0u32;
        for _ in 0..4 {
            let digit = self.next().and_then(|c| c.to_digit(16));
            match digit {
                Some(d) => value = value * 16 + d,
                None => return Err(Error { pos: at, what: "Json.BadEscape", detail: String::new() }),
            }
        }
        Ok(value)
    }
    fn array(&mut self, depth: usize) -> Result<Kind, Error> {
        self.expect('[')?;
        self.skip_space()?;
        let mut items = Vec::new();
        if self.peek() == Some(']') {
            self.next();
            return Ok(Kind::Array(items));
        }
        loop {
            self.skip_space()?;
            items.push(self.value(depth + 1)?);
            self.skip_space()?;
            match self.peek() {
                Some(',') => {
                    self.next();
                    self.skip_space()?;
                    if self.peek() == Some(']') {
                        return Err(self.error_here("Json.TrailingComma", String::new()));
                    }
                }
                Some(']') => {
                    self.next();
                    return Ok(Kind::Array(items));
                }
                Some(_) => return Err(self.error_here("Json.Expected", "]".to_string())),
                None => return Err(self.error_here("Json.Truncated", String::new())),
            }
        }
    }
    fn object(&mut self, depth: usize) -> Result<Kind, Error> {
        self.expect('{')?;
        self.skip_space()?;
        let mut members: Vec<Member> = Vec::new();
        if self.peek() == Some('}') {
            self.next();
            return Ok(Kind::Object(members));
        }
        loop {
            self.skip_space()?;
            let key_pos = self.pos();
            if self.peek() != Some('"') {
                return Err(match self.peek() {
                    Some(c) => self.error_here("Json.KeyNotString", c.to_string()),
                    None => self.error_here("Json.Truncated", String::new()),
                });
            }
            let key = self.string()?;
            if members.iter().any(|m| m.key == key) {
                return Err(Error { pos: key_pos, what: "Json.DuplicateKey", detail: key });
            }
            self.skip_space()?;
            self.expect(':')?;
            self.skip_space()?;
            let value = self.value(depth + 1)?;
            members.push(Member { key, key_pos, value });
            self.skip_space()?;
            match self.peek() {
                Some(',') => {
                    self.next();
                    self.skip_space()?;
                    if self.peek() == Some('}') {
                        return Err(self.error_here("Json.TrailingComma", String::new()));
                    }
                }
                Some('}') => {
                    self.next();
                    return Ok(Kind::Object(members));
                }
                Some(_) => return Err(self.error_here("Json.Expected", "}".to_string())),
                None => return Err(self.error_here("Json.Truncated", String::new())),
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn positions_are_kept() {
        let v = parse(b"{\n  \"a\": [1, 2],\n  \"b\": \"x\"\n}").unwrap();
        assert_eq!(v.member("b").unwrap().key_pos, Pos { line: 3, column: 3 });
        assert_eq!(v.get("a").unwrap().items()[1].pos, Pos { line: 2, column: 12 });
    }

    #[test]
    fn strictness() {
        let cases: &[(&[u8], &str, u32, u32)] = &[
            (b"{\"a\": 1, \"a\": 2}", "Json.DuplicateKey", 1, 10),
            (b"{\"a\": 1 // no\n}", "Json.Comment", 1, 9),
            (b"[1,]", "Json.TrailingComma", 1, 4),
            (b"\xef\xbb\xbf{}", "Json.Bom", 1, 1),
            (b"{\"a\": \xff}", "Json.NotUtf8", 1, 7),
            (b"{\"a\": 01}", "Json.LeadingZero", 1, 8),
            (b"{\"a\": \"\\x\"}", "Json.BadEscape", 1, 8),
            (b"{} x", "Json.AfterEnd", 1, 4),
            (b"{\"a\":", "Json.Truncated", 1, 6),
            (b"{\"a\": 1e999}", "Json.BadNumber", 1, 12),
        ];
        for &(text, what, line, column) in cases {
            let e = parse(text).unwrap_err();
            assert_eq!((e.what, e.pos.line, e.pos.column), (what, line, column), "{:?}", String::from_utf8_lossy(text));
        }
    }

    #[test]
    fn deep_nesting_is_an_error_not_a_crash() {
        let text = "[".repeat(100_000);
        assert_eq!(parse(text.as_bytes()).unwrap_err().what, "Json.TooDeep");
    }

    #[test]
    fn surrogate_pairs() {
        let v = parse(b"\"\\ud83d\\ude00\"").unwrap();
        assert_eq!(v.as_str(), Some("\u{1f600}"));
        assert_eq!(parse(b"\"\\ud83d\"").unwrap_err().what, "Json.BadEscape");
    }
}
