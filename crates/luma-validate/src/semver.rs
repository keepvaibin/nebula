//! Semantic versions (`MAJOR.MINOR.PATCH` with an optional `-pre` and `+build`) and the ranges `nebula` and `needs`
//! use: comparators `>=`, `>`, `<=`, `<`, `=`, a bare version (exactly it), `^` (same major; for 0.x the same minor),
//! `~` (same minor), `*` (any), joined by spaces (all must hold) and ` || ` (any group). Precedence as semver 2.0.0.

use std::cmp::Ordering;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Version {
    pub major: u64,
    pub minor: u64,
    pub patch: u64,
    pub pre: Vec<String>,
}

impl Version {
    pub fn parse(text: &str) -> Option<Version> {
        let text = text.split_once('+').map_or(text, |(v, build)| {
            if build.is_empty() || !build.split('.').all(|p| !p.is_empty() && p.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'-')) {
                "!"
            } else {
                v
            }
        });
        let (core, pre) = match text.split_once('-') {
            Some((core, pre)) => (core, Some(pre)),
            None => (text, None),
        };
        let mut parts = core.split('.');
        let mut number = || -> Option<u64> {
            let p = parts.next()?;
            if p.is_empty() || !p.bytes().all(|b| b.is_ascii_digit()) || (p.len() > 1 && p.starts_with('0')) {
                return None;
            }
            p.parse().ok()
        };
        let (major, minor, patch) = (number()?, number()?, number()?);
        if parts.next().is_some() {
            return None;
        }
        let pre = match pre {
            None => Vec::new(),
            Some(pre) => {
                let ids: Vec<String> = pre.split('.').map(str::to_string).collect();
                for id in &ids {
                    let numeric = id.bytes().all(|b| b.is_ascii_digit());
                    if id.is_empty()
                        || !id.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'-')
                        || (numeric && id.len() > 1 && id.starts_with('0'))
                    {
                        return None;
                    }
                }
                ids
            }
        };
        Some(Version { major, minor, patch, pre })
    }
}

impl std::fmt::Display for Version {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}.{}.{}", self.major, self.minor, self.patch)?;
        if !self.pre.is_empty() {
            write!(f, "-{}", self.pre.join("."))?;
        }
        Ok(())
    }
}

impl Ord for Version {
    fn cmp(&self, other: &Self) -> Ordering {
        (self.major, self.minor, self.patch).cmp(&(other.major, other.minor, other.patch)).then_with(|| {
            match (self.pre.is_empty(), other.pre.is_empty()) {
                (true, true) => Ordering::Equal,
                (true, false) => Ordering::Greater,
                (false, true) => Ordering::Less,
                (false, false) => {
                    for (a, b) in self.pre.iter().zip(&other.pre) {
                        let order = match (a.parse::<u64>(), b.parse::<u64>()) {
                            (Ok(x), Ok(y)) => x.cmp(&y),
                            (Ok(_), Err(_)) => Ordering::Less,
                            (Err(_), Ok(_)) => Ordering::Greater,
                            (Err(_), Err(_)) => a.cmp(b),
                        };
                        if order != Ordering::Equal {
                            return order;
                        }
                    }
                    self.pre.len().cmp(&other.pre.len())
                }
            }
        })
    }
}

impl PartialOrd for Version {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
enum Op {
    Ge,
    Gt,
    Le,
    Lt,
    Eq,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Range {
    /// Alternatives (`||`), each a list of comparators that must all hold.
    groups: Vec<Vec<(Op, Version)>>,
}

impl Range {
    pub fn parse(text: &str) -> Option<Range> {
        let mut groups = Vec::new();
        for group in text.split("||") {
            let mut comparators = Vec::new();
            let words: Vec<&str> = group.split_whitespace().collect();
            if words.is_empty() {
                return None;
            }
            for word in words {
                if word == "*" {
                    comparators.push((Op::Ge, Version { major: 0, minor: 0, patch: 0, pre: Vec::new() }));
                    continue;
                }
                let (prefix, rest) = [">=", "<=", ">", "<", "=", "^", "~"]
                    .iter()
                    .find_map(|p| word.strip_prefix(p).map(|rest| (*p, rest)))
                    .unwrap_or(("", word));
                let v = Version::parse(rest)?;
                // The lowest pre-release of a version: below every other build of it.
                let floor = |major, minor| Version { major, minor, patch: 0, pre: vec!["0".into()] };
                match prefix {
                    ">=" => comparators.push((Op::Ge, v)),
                    "<=" => comparators.push((Op::Le, v)),
                    ">" => comparators.push((Op::Gt, v)),
                    "<" => comparators.push((Op::Lt, v)),
                    "^" => {
                        let upper = if v.major > 0 { floor(v.major + 1, 0) } else { floor(0, v.minor + 1) };
                        comparators.push((Op::Ge, v));
                        comparators.push((Op::Lt, upper));
                    }
                    "~" => {
                        let upper = floor(v.major, v.minor + 1);
                        comparators.push((Op::Ge, v));
                        comparators.push((Op::Lt, upper));
                    }
                    _ => comparators.push((Op::Eq, v)),
                }
            }
            groups.push(comparators);
        }
        Some(Range { groups })
    }

    pub fn holds(&self, v: &Version) -> bool {
        self.groups.iter().any(|group| {
            group.iter().all(|(op, bound)| match op {
                Op::Ge => v >= bound,
                Op::Gt => v > bound,
                Op::Le => v <= bound,
                Op::Lt => v < bound,
                Op::Eq => v == bound,
            })
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn v(s: &str) -> Version {
        Version::parse(s).unwrap()
    }

    #[test]
    fn parse_and_order() {
        assert!(Version::parse("1.0").is_none());
        assert!(Version::parse("01.0.0").is_none());
        assert!(Version::parse("1.0.0-").is_none());
        assert!(Version::parse("1.0.0+").is_none());
        assert!(v("1.0.0-alpha") < v("1.0.0-alpha.1"));
        assert!(v("1.0.0-alpha.1") < v("1.0.0-beta"));
        assert!(v("1.0.0-beta.11") > v("1.0.0-beta.2"));
        assert!(v("1.0.0-rc.1") < v("1.0.0"));
        assert_eq!(v("1.2.3+abc"), v("1.2.3"));
    }

    #[test]
    fn ranges() {
        let r = Range::parse(">=2.0.0").unwrap();
        assert!(r.holds(&v("2.0.0")) && r.holds(&v("3.1.0")) && !r.holds(&v("1.9.9")));
        let r = Range::parse("^1.2.0").unwrap();
        assert!(r.holds(&v("1.9.0")) && !r.holds(&v("2.0.0")) && !r.holds(&v("2.0.0-pre")));
        let r = Range::parse("^0.2.1").unwrap();
        assert!(r.holds(&v("0.2.5")) && !r.holds(&v("0.3.0")));
        let r = Range::parse("~1.2.0").unwrap();
        assert!(r.holds(&v("1.2.9")) && !r.holds(&v("1.3.0")));
        let r = Range::parse(">=1.0.0 <2.0.0 || 3.0.0").unwrap();
        assert!(r.holds(&v("1.5.0")) && r.holds(&v("3.0.0")) && !r.holds(&v("2.5.0")));
        assert!(Range::parse("").is_none() && Range::parse(">= 1.0.0").is_none() && Range::parse("1.x").is_none());
        assert!(Range::parse("*").unwrap().holds(&v("0.0.1")));
    }
}
