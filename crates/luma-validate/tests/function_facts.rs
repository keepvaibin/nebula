//! luma_functions.json's reader (types TY-11): a file in its form reads, every fault is an error naming the row.

use luma_validate::names::{signature_params, FunctionFacts, FunctionKind, Mark, TypesError, Way};

fn row(name: &str, sig: &str, kind: &str, owner: &str, flags: (bool, bool, bool), overrides: &str) -> String {
    // No marks: one null per parameter (C-6).
    let marks = vec!["null"; signature_params(sig).unwrap()].join(", ");
    marked(name, sig, kind, owner, flags, overrides, &marks)
}

fn marked(name: &str, sig: &str, kind: &str, owner: &str, flags: (bool, bool, bool), overrides: &str, marks: &str) -> String {
    let (v, p, c) = flags;
    format!(r#"{{"name": "{name}", "signature": "{sig}", "kind": "{kind}", "owner": {owner}, "virtual": {v}, "pure": {p}, "const": {c}, "overrides": [{overrides}], "marks": [{marks}]}}"#)
}

/// A free function `f` with this signature and these marks.
fn free(sig: &str, marks: &str) -> String {
    marked("f", sig, "free", "null", (false, false, false), "", marks)
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

/// K's fault on l16 (G, 2026-11-11): a function returning a pointer to an array read `(*)` as its list, so its one
/// row refused the whole file.
#[test]
fn a_declarator_before_the_list_is_no_list() {
    let rows = [marked("Ui::detail::GetDrawMtx", "const f32 (*)[4]()", "free", "null", (false, false, false), "", "")];
    let facts = FunctionFacts::from_json(file(&rows).as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
    assert_eq!(facts.rows.len(), 1);
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
    let no_marks = r#"{"name": "f", "signature": "void()", "kind": "free", "owner": null, "virtual": false, "pure": false, "const": false, "overrides": []}"#;
    assert_eq!(refused(&[no_marks.to_string()]), "functions[0].marks: missing, or not a list");
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
    let marked: usize = facts.rows.values().map(|f| f.marks.iter().flatten().count()).sum();
    eprintln!("{} rows {kinds:?}; {} virtual; {overrides} overrides; {marked} marked parameters", facts.rows.len(), facts.rows.values().filter(|f| f.is_virtual).count());
}

fn mark(way: &str, count: &str, fixed: &str, nullable: bool) -> String {
    format!(r#"{{"way": "{way}", "count": {count}, "fixed": {fixed}, "nullable": {nullable}}}"#)
}

#[test]
fn marks_in_their_form_read() {
    // C-6's example: `void f(LUMA_IN LUMA_COUNT(n) const f32* p, s32 n, LUMA_OUT LUMA_NULLABLE Vec3f* out)`.
    let sig = "void(const f32 *, s32, Vec3f *)";
    let marks = [mark("in", "1", "null", false), "null".into(), mark("out", "null", "null", true)].join(", ");
    let m = marked("R::get", "void(Vec3f *, f32 *) const", "method", "\"R\"", (false, false, true), "", &[mark("inout", "null", "null", false), mark("kept", "null", "null", false)].join(", "));
    let fixed = marked("g", "void(f32 *)", "free", "null", (false, false, false), "", &mark("out", "null", "12", false));
    let facts = FunctionFacts::from_json(file(&[free(sig, &marks), m, fixed]).as_bytes()).unwrap();
    assert_eq!(facts.get("f", sig).unwrap().marks, vec![
        Some(Mark { way: Way::In, count: Some(1), fixed: None, nullable: false }),
        None,
        Some(Mark { way: Way::Out, count: None, fixed: None, nullable: true }),
    ]);
    // A method's `this` is not counted.
    let r = &facts.get("R::get", "void(Vec3f *, f32 *) const").unwrap().marks;
    assert_eq!((r.len(), r[0].unwrap().way, r[1].unwrap().way), (2, Way::InOut, Way::Kept));
    assert_eq!(facts.get("g", "void(f32 *)").unwrap().marks[0].unwrap().fixed, Some(12));
}

#[test]
fn every_marks_fault_is_named() {
    let out = mark("out", "null", "null", false);
    assert_eq!(refused(&[free("void(f32 *)", "")]), "functions[0].marks: 0 entries for 1 parameters");
    assert_eq!(refused(&[free("void()", "null")]), "functions[0].marks: 1 entries for 0 parameters");
    assert_eq!(refused(&[free("void(f32 *)", "1")]), "functions[0].marks[0]: not null, nor an object of exactly way, count, fixed and nullable");
    let three = r#"{"way": "out", "count": null, "nullable": false}"#;
    assert_eq!(refused(&[free("void(f32 *)", three)]), "functions[0].marks[0]: not null, nor an object of exactly way, count, fixed and nullable");
    let five = r#"{"way": "out", "count": null, "fixed": null, "nullable": false, "kept": true}"#;
    assert_eq!(refused(&[free("void(f32 *)", five)]), "functions[0].marks[0]: not null, nor an object of exactly way, count, fixed and nullable");
    assert_eq!(refused(&[free("void(f32 *)", &mark("write", "null", "null", false))]), "functions[0].marks[0].way: not in, out, inout or kept");
    assert_eq!(refused(&[free("void(f32 *, s32)", &format!("{}, null", mark("in", "2", "null", false)))]), "functions[0].marks[0].count: not null or a whole number from 0 to 1");
    assert_eq!(refused(&[free("void(f32 *, s32)", &format!("{}, null", mark("in", "1.0", "null", false)))]), "functions[0].marks[0].count: not null or a whole number from 0 to 1");
    assert_eq!(refused(&[free("void(f32 *)", &mark("in", "0", "null", false))]), "functions[0].marks[0].count: its own parameter");
    assert_eq!(refused(&[free("void(f32 *)", &mark("out", "null", "0", false))]), "functions[0].marks[0].fixed: not null or a whole number from 1 to 4096");
    assert_eq!(refused(&[free("void(f32 *)", &mark("out", "null", "4097", false))]), "functions[0].marks[0].fixed: not null or a whole number from 1 to 4096");
    assert_eq!(refused(&[free("void(f32 *, s32)", &format!("{}, null", mark("in", "1", "3", false)))]), "functions[0].marks[0]: count and fixed both set");
    assert_eq!(refused(&[free("void(f32 *)", &mark("kept", "null", "2", false))]), "functions[0].marks[0]: kept with a count or a fixed count");
    assert_eq!(refused(&[free("void(f32 *)", r#"{"way": "out", "count": null, "fixed": null, "nullable": 1}"#)]), "functions[0].marks[0].nullable: not true or false");
    assert_eq!(refused(&[free("void(f32 *, s32 *)", &format!("{}, {out}", mark("in", "1", "null", false)))]), "functions[0].marks[0].count: names a parameter that is itself marked");
    assert_eq!(refused(&[marked("f", "void", "free", "null", (false, false, false), "", "")]), "functions[0].marks: the row's signature has no parameter list");
}

#[test]
fn a_signatures_parameters_are_counted() {
    assert_eq!(signature_params("void()"), Some(0));
    assert_eq!(signature_params("void(void)"), Some(0));
    assert_eq!(signature_params("()"), Some(0));
    assert_eq!(signature_params("bool(const Vec3f &, s32) const"), Some(2));
    assert_eq!(signature_params("void(void (*)(int, int), JGeometry::TVec3<f32, int> *, int[3])"), Some(3));
    assert_eq!(signature_params("void"), None);
    // A declarator's parentheses before the list (Ui::detail::GetDrawMtx in l16's luma_functions.json, K's fault).
    assert_eq!(signature_params("const f32 (*)[4]()"), Some(0));
    assert_eq!(signature_params("const f32 (*)[4](int, char) const"), Some(2));
    assert_eq!(signature_params("void (*)(int)"), Some(1));
}
