//! luma_functions.json's reader (types TY-11): a file in its form reads, every fault is an error naming the row.

use luma_validate::names::{FunctionFacts, FunctionKind, TypesError};

fn row(name: &str, sig: &str, kind: &str, owner: &str, flags: (bool, bool, bool), overrides: &str) -> String {
    let (v, p, c) = flags;
    format!(r#"{{"name": "{name}", "signature": "{sig}", "kind": "{kind}", "owner": {owner}, "virtual": {v}, "pure": {p}, "const": {c}, "overrides": [{overrides}]}}"#)
}

fn file(rows: &[String]) -> String {
    format!(r#"{{"functions": [{}]}}"#, rows.join(", "))
}

fn refused(rows: &[String]) -> String {
    match FunctionFacts::from_json(file(rows).as_bytes()) {
        Err(TypesError::Row { at, why }) => format!("{at}: {why}"),
        other => panic!("not refused: {other:?}"),
    }
}

#[test]
fn a_file_in_its_form_reads() {
    let rows = [
        row("MR::isNear", "bool(const LiveActor *, f32)", "free", "null", (false, false, false), ""),
        row("Base::f", "void(int)", "method", "\"Base\"", (true, false, false), ""),
        row("Base::g", "int() const", "method", "\"Base\"", (true, true, true), ""),
        row("Derived::f", "void(int)", "method", "\"Derived\"", (true, false, false), r#"{"name": "Base::f", "signature": "void(int)", "row": true}"#),
        row("Base::s", "int()", "static", "\"Base\"", (false, false, false), ""),
        row("Base::Base", "()", "constructor", "\"Base\"", (false, false, false), ""),
        row("Base::~Base", "()", "destructor", "\"Base\"", (true, false, false), ""),
        row("a.cpp#Local::m", "void()", "method", "null", (false, false, false), ""),
    ];
    let facts = FunctionFacts::from_json(file(&rows).as_bytes()).unwrap();
    assert_eq!(facts.rows.len(), 8);
    let d = facts.get("Derived::f", "void(int)").unwrap();
    assert_eq!((d.kind, d.owner.as_deref(), d.is_virtual), (FunctionKind::Method, Some("Derived"), true));
    assert_eq!((d.overrides[0].name.as_str(), d.overrides[0].row), ("Base::f", true));
    assert!(facts.get("Base::g", "int() const").unwrap().konst);
    assert_eq!(facts.get("Base::s", "int()").unwrap().kind, FunctionKind::Static);
    assert_eq!(facts.get("a.cpp#Local::m", "void()").unwrap().owner, None);
    assert!(facts.get("Base::f", "void(long)").is_none());
}

#[test]
fn every_fault_is_named() {
    let free = row("f", "void()", "free", "null", (false, false, false), "");
    assert_eq!(refused(&[free.clone(), free]), "functions[1]: a row given twice");
    assert_eq!(refused(&[row("f", "void()", "free", "\"R\"", (false, false, false), "")]), "functions[0].owner: a free function has no record");
    let rule = "functions[0]: const only on a method, virtual only on a member, pure only on a virtual";
    assert_eq!(refused(&[row("R::s", "void()", "static", "\"R\"", (false, false, true), "")]), rule);
    assert_eq!(refused(&[row("R::s", "void()", "static", "\"R\"", (true, false, false), "")]), rule);
    assert_eq!(refused(&[row("R::m", "void()", "method", "\"R\"", (false, true, false), "")]), rule);
    let over = r#"{"name": "B::m", "signature": "void()", "row": true}"#;
    assert_eq!(refused(&[row("R::m", "void()", "method", "\"R\"", (false, false, false), over)]), "functions[0].overrides: only a virtual overrides");
    assert_eq!(refused(&[row("R::m", "void()", "member", "\"R\"", (false, false, false), "")]), "functions[0].kind: not free, method, static, constructor or destructor");
    let nameless = r#"{"name": null, "signature": "void()", "row": false}"#;
    assert_eq!(refused(&[row("R::m", "void()", "method", "\"R\"", (true, false, false), nameless)]), "functions[0].overrides[0].name: missing, or not text");
    assert_eq!(refused(&[r#"{"name": "f", "signature": "void()", "kind": "free", "owner": null, "pure": false, "const": false, "overrides": []}"#.to_string()]), "functions[0].virtual: missing, or not true or false");
    assert!(matches!(FunctionFacts::from_json(b"{\"rows\": []}"), Err(TypesError::Row { .. })));
    assert!(matches!(FunctionFacts::from_json(b"{\"functions\": ["), Err(TypesError::Json { .. })));
}

/// A kernel build's file (`LUMA_FUNCTIONS=<build>/luma_types/luma_functions.json cargo test -p luma-validate --test
/// function_facts -- --ignored --nocapture`): it reads, and its counts are printed.
#[test]
#[ignore]
fn a_kernel_builds_facts_read() {
    let path = std::env::var("LUMA_FUNCTIONS").expect("LUMA_FUNCTIONS names a luma_functions.json");
    let facts = FunctionFacts::from_json(&std::fs::read(&path).unwrap()).unwrap_or_else(|e| panic!("{path}: {e}"));
    let mut kinds = std::collections::BTreeMap::new();
    for f in facts.rows.values() {
        *kinds.entry(format!("{:?}", f.kind)).or_insert(0usize) += 1;
    }
    let overrides: usize = facts.rows.values().map(|f| f.overrides.len()).sum();
    eprintln!("{} rows {kinds:?}; {} virtual; {overrides} overrides", facts.rows.len(), facts.rows.values().filter(|f| f.is_virtual).count());
}
