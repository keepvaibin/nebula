use super::*;

/// Two game lines, the first with a node.
pub(crate) fn game() -> (Vec<u8>, Vec<u8>) {
    let mut nmsg = Nmsg::default();
    for (i, words) in ["First", "Second"].iter().enumerate() {
        nmsg.messages.push(Message { text: nmsg.text.len() as u32, camera_set: 7, bytes: [3, 0, 4, 5, 1, i as u8] });
        nmsg.text.extend(words.encode_utf16());
        nmsg.text.push(0);
    }
    nmsg.nodes.push(Node { kind: 1, group: 0, index: 0, next: NONE, next_group: 0 });
    let table = TableIr::from_json(br#"{"columns":{"MessageId":"string","Index":"int"},"rows":[{"MessageId":"Game_A","Index":0},{"MessageId":"Game_B","Index":1}]}"#).unwrap();
    (nmsg.to_bytes(), table.to_ntbl())
}

pub(crate) fn words(nmsg: &Nmsg, index: usize) -> String {
    decode_text(units_at(&nmsg.text, nmsg.messages[index].text as usize))
}

#[test]
fn lines_change_and_join_the_game_with_their_talk() {
    let (file, table) = game();
    let json = br#"{"lines":{"Game_B":"Other [color 1]words[color 0]","Mod_New":{"default":"Hi [player]!","fr":"Salut [player] !","like":"Game_A","balloon":9},"AAA_First":"x"}}"#;
    let (file, table) = merge(&file, &table, json, "UsFrench").unwrap();
    let nmsg = Nmsg::from_nmsg(&file).unwrap();
    let ids = Ids::read(&table).unwrap();
    assert_eq!(nmsg.messages.len(), 4);
    assert_eq!(words(&nmsg, 1), "Other [color 1]words[color 0]");
    assert_eq!(nmsg.messages[1].camera_set, 7, "a game line keeps its talk");
    let new = ids.by_id["Mod_New"] as usize;
    assert_eq!(words(&nmsg, new), "Salut [player] !");
    assert_eq!(nmsg.messages[new].bytes, [3, 0, 4, 9, 1, 0], "like Game_A, its balloon set");
    let order: Vec<_> = ids.table.rows.iter().map(|r| if let Cell::Text(t) = &r[ids.id_at] { String::from_utf8_lossy(t).into_owned() } else { String::new() }).collect();
    assert_eq!(order, ["AAA_First", "Game_A", "Game_B", "Mod_New"], "in the byte order the game searches");
    assert_eq!(nmsg.messages[ids.by_id["AAA_First"] as usize].bytes, ordinary_talk().bytes);
}

#[test]
fn flows_walk_as_the_game_walks_them() {
    let (file, table) = game();
    let json = br#"{"lines":{"Ask":"Want one?","Yes":"Here!","No":"Fine."},
            "flows":{"Game_A":"Ask","Ask":{"ask":"select","choices":{"yes":"Sure","no":{"default":"Nope","fr":"Non"}},
                "yes":{"event":"func","value":2,"next":"Yes"},"no":"No"},"No":null}}"#;
    let (file, table) = merge(&file, &table, json, "UsEnglish").unwrap();
    let nmsg = Nmsg::from_nmsg(&file).unwrap();
    let ids = Ids::read(&table).unwrap();
    let node_of = |id: &str| nmsg.nodes.iter().position(|n| n.kind == 1 && n.index == ids.by_id[id]).unwrap();
    // Game_A's own node now goes on to Ask; Ask to a select branch.
    assert_eq!(nmsg.nodes[0].next as usize, node_of("Ask"));
    let branch_at = nmsg.nodes[node_of("Ask")].next;
    let branch = nmsg.nodes[branch_at as usize];
    assert_eq!((branch.kind, branch.group, branch.index, branch.next), (2, BRANCH_GROUP, 0, SELECT_OWN_CHOICES));
    // Its choices: lines the game's selector reads by the branch's node.
    assert_eq!(words(&nmsg, ids.by_id[&format!("Select_Nebula{branch_at}_Yes")] as usize), "Sure");
    assert_eq!(words(&nmsg, ids.by_id[&format!("Select_Nebula{branch_at}_No")] as usize), "Nope");
    // Yes: an event whose next (its branch slot) is the Yes line; no: the No line, which ends the talk.
    let event = nmsg.nodes[nmsg.branches[branch.next_group as usize] as usize];
    assert_eq!((event.kind, event.group, event.next), (3, 0, 2));
    assert_eq!(nmsg.branches[event.index as usize] as usize, node_of("Yes"));
    assert_eq!(nmsg.branches[branch.next_group as usize + 1] as usize, node_of("No"));
    assert_eq!(nmsg.nodes[node_of("No")].next, NONE);
}

#[test]
fn what_cannot_be_is_said() {
    let (file, table) = game();
    for (json, says) in [
        (r#"{"lines":{"New":{"fr":"x","UsFrench":"y"}}}"#, "no words for UsEnglish"),
        (r#"{"lines":{"New":{"default":"x","like":"Nope"}}}"#, "which is no line"),
        (r#"{"flows":{"Game_A":"Nope"}}"#, "which is no line"),
        (r#"{"flows":{"Game_A":{"ask":"maybe"}}}"#, "not one of"),
        (r#"{"flows":{"Game_A":{"ask":"select","param":19}}}"#, "0 to 18"),
        (r#"{"flows":{"Game_A":{"ask":"select","param":1,"choices":{"yes":"a","no":"b"}}}}"#, "not both"),
        (r#"{"flows":{"Game_A":{"ask":"near_player","choices":{"yes":"a","no":"b"}}}}"#, "only a select"),
        (r#"{"flows":{"Game_A":{"ask":"select","choices":{"yes":"a"}}}}"#, "choices need no"),
        (r#"{"lines":{"New":{"default":"x","talk":"loud"}}}"#, "not one of"),
        (r#"{"rows":[]}"#, "lines and flows"),
    ] {
        let error = format!("{:#}", merge(&file, &table, json.as_bytes(), "UsEnglish").unwrap_err());
        assert!(error.contains(says), "{json}: {error}");
    }
}

#[test]
fn words_go_by_language_as_text_rows_do() {
    let words = serde_json::json!({"default": "d", "fr": "f", "fr-CA": "q", "EuGerman": "g", "es": "s"});
    for (folder, expected) in [("UsFrench", "q"), ("EuFrench", "f"), ("EuGerman", "g"), ("UsSpanish", "s"), ("JpJapanese", "d")] {
        assert_eq!(words_in(&words, folder).unwrap(), Some(expected), "{folder}");
    }
}

#[test]
fn a_merge_is_checked_without_the_game() {
    let good = br#"{"lines":{"New":{"default":"Hi","like":"Game_X"},"Game_Y":{"talk":"short"}},
            "flows":{"Game_Z":"New","New":{"ask":"select","choices":{"yes":"a","no":"b"},"yes":"Game_W","no":null}}}"#;
    check_shape(good, "UsEnglish").unwrap();
    for (json, says) in [
        (r#"{"flows":{"A":{"ask":"maybe"}}}"#, "not one of"),
        (r#"{"lines":{"A":{"default":"x","balloon":300}}}"#, "outside"),
        (r#"{"lines":{"A":{"default":"x","camera":"wide"}}}"#, "not one of"),
        (r#"{"flows":{"A":{"ask":"select","choices":{"yes":{"fr":"x"},"no":"b"}}}}"#, "no words for UsEnglish"),
        (r#"{"rows":[]}"#, "lines and flows"),
    ] {
        let error = format!("{:#}", check_shape(json.as_bytes(), "UsEnglish").unwrap_err());
        assert!(error.contains(says), "{json}: {error}");
    }
}

#[test]
fn mod_text_reads_as_nebula_reads_it() {
    assert_eq!(encode_text("A[wait 30]B"), [u16::from(b'A'), 0x1A, 8 | (1 << 8), 0, 30, u16::from(b'B')]);
    assert_eq!(encode_text("[color 1]x[color 0]"), [0x1A, 8 | (255 << 8), 0, 0x100, u16::from(b'x'), 0x1A, 8 | (255 << 8), 0, 0]);
    assert_eq!(encode_text("[icon 7][LARGE]"), [0x1A, 6 | (3 << 8), 7, 0x1A, 6 | (4 << 8), 2]);
    assert_eq!(encode_text("[tag 6 5 0 0x1 2]"), [0x1A, 12 | (6 << 8), 5, 0, 1, 2]);
    let plain = "[a] [nope 1] [wait] [wait x] [color 256]";
    assert_eq!(encode_text(&format!("[{plain}")), plain.encode_utf16().collect::<Vec<_>>());
    let text = "Hi [player]! [[sic] [color 3]Star[color 0][wait 20][page][icon 12][tag 6 5 0 1 0 0][sound SE_SV_TICO]";
    assert_eq!(decode_text(&encode_text(text)), text);
    // A message ends at its first 0 outside a tag: tags hold 0s.
    let mut file = encode_text("[color 1]A[color 0]");
    file.push(0);
    file.extend(encode_text("B"));
    assert_eq!(decode_text(units_at(&file, 0)), "[color 1]A[color 0]");
    assert_eq!(units_at(&file, file.len()), &[] as &[u16]);
}

/// The game's own messages, every language on the disc, read out as words and cooked back: the same units, so
/// the same file; and every tag they hold has a word of the table. NEBULA_DISC_FILES: an extracted disc's files
/// folder. cargo test -p nebula-import --lib disc_messages_read_out_and_cook_back -- --ignored --nocapture
#[test]
#[ignore]
fn disc_messages_read_out_and_cook_back() {
    use crate::cook::archive;
    let files = std::env::var("NEBULA_DISC_FILES").expect("NEBULA_DISC_FILES");
    let mut languages = 0;
    let mut entries: Vec<_> = std::fs::read_dir(&files).unwrap().map(|e| e.unwrap().path()).collect();
    entries.sort();
    for dir in entries {
        let arc = dir.join("MessageData/Message.arc");
        let Ok(raw) = std::fs::read(&arc) else { continue };
        let bytes = if archive::is_compressed(&raw) { archive::expand(&raw).unwrap() } else { raw };
        let member = archive::members(&bytes).unwrap().into_iter().find(|m| m.path.ends_with("message.bmg")).unwrap();
        let nmsg = Nmsg::from_disc(&member.bytes).unwrap();
        let mut text = Vec::with_capacity(nmsg.text.len());
        let (mut tags, mut unnamed) = (0, Vec::new());
        for (index, message) in nmsg.messages.iter().enumerate() {
            let units = units_at(&nmsg.text, message.text as usize);
            let words = decode_text(units);
            tags += units.iter().filter(|u| **u == TAG).count();
            unnamed.extend(words.match_indices("[tag ").map(|(at, _)| words[at..].split(']').next().unwrap_or("").to_string()));
            let again = encode_text(&words);
            assert_eq!(again, units, "{} message {index}: {words}", dir.display());
            if text.len() == message.text as usize {
                text.extend(again);
                text.push(0);
            }
        }
        assert_eq!(text, nmsg.text[..text.len()], "{}", dir.display());
        unnamed.sort();
        unnamed.dedup();
        assert!(unnamed.is_empty(), "{}: tags of no word: {unnamed:?}", dir.display());
        println!("{}: {} messages, {} tags, the same units", dir.display(), nmsg.messages.len(), tags);
        languages += 1;
    }
    assert!(languages > 0, "no MessageData/Message.arc under {files}");
}

/// A line's brackets that are no tag are refused, each with its cause, the line and the column; `[[` writes a
/// bracket of the text.
#[test]
fn a_wrong_tag_is_named() {
    let said = |text: &str| -> Option<Vec<String>> {
        let error = check_words("Mod_Line (UsEnglish)", text).err()?;
        let refusal = error.downcast_ref::<super::super::patch::Refusal>().expect("a named refusal");
        Some(
            refusal
                .0
                .args
                .iter()
                .map(|(name, value)| match value {
                    luma_validate::Arg::Phrase(p) => format!("{name}={p}"),
                    luma_validate::Arg::Text(t) => format!("{name}={t}"),
                })
                .collect(),
        )
    };
    assert_eq!(said("Hi [player]! [[sic] [color 3]Star[color 0][number 5 1][tag 6 5 0 1 2]"), None);
    assert_eq!(said("Hi [player"), Some(vec!["why=Cook.Text.Unclosed".into(), "key=Mod_Line (UsEnglish)".into(), "column=4".into()]));
    assert_eq!(said("é [wait [page]").unwrap()[2], "column=3");
    assert_eq!(said("[plyer]").unwrap(), ["why=Cook.Text.UnknownTag", "key=Mod_Line (UsEnglish)", "column=1", "word=plyer"]);
    assert_eq!(said("a []").unwrap()[0], "why=Cook.Text.UnknownTag");
    assert_eq!(said("[wait]").unwrap()[4], "syntax=[wait n]");
    assert_eq!(said("[player 1]").unwrap()[4], "syntax=[player]");
    assert_eq!(said("[number 5]").unwrap()[4], "syntax=[number t i]");
    assert_eq!(said("[sound A[B]").unwrap()[0], "why=Cook.Text.Unclosed");
    assert_eq!(said("[tag 300 1]").unwrap()[4], "syntax=[tag G T u...]");
    assert_eq!(said("[color 256]").unwrap()[3..], ["word=color".to_string(), "value=256".into(), "most=255".into()]);
    assert_eq!(said("[wait x]").unwrap()[4], "value=x");
    assert_eq!(said("[WAIT 0x10000]").unwrap()[4], "value=0x10000");
    // In a merge the line's id and language are named.
    let (file, ids) = game();
    let error = merge(&file, &ids, br#"{"lines":{"Mod_New":"Hi [plyer]"}}"#, "UsEnglish").unwrap_err();
    let refusal = error.downcast_ref::<super::super::patch::Refusal>().expect("a named refusal");
    assert!(refusal.0.args.iter().any(|(n, v)| *n == "key" && *v == luma_validate::Arg::Text("Mod_New (UsEnglish)".into())));
}

/// The cook's words are the SDK's: native/include/luma_text_tags.h's table, which the game's mod API and
/// LumaScript's runtime use, row for row (word, group, tag, argument, largest value, words); and each row's word
/// cooks to its tag, reads back as written, and past its largest value stays as written.
#[test]
fn tag_words_are_the_sdk_table() {
    let header = std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/../../native/include/luma_text_tags.h")).unwrap();
    let body = header.split("luma_text_tags[] = {").nth(1).unwrap().split("};").next().unwrap();
    let rows: Vec<&str> = body.lines().map(str::trim).filter(|l| l.starts_with("{\"")).collect();
    assert_eq!(rows.len(), TAG_WORDS.len(), "the header has {} rows, the cook {}", rows.len(), TAG_WORDS.len());
    for (line, t) in rows.iter().zip(TAG_WORDS.iter()) {
        let inner = line.trim_start_matches('{').trim_end_matches("},");
        let (fields, means) = inner.rsplit_once(", \"").unwrap();
        let f: Vec<&str> = fields.split(", ").collect();
        let arg = match f[3] {
            "LUMA_TEXT_ARG_NONE" => Arg::None,
            "LUMA_TEXT_ARG_UNIT" => Arg::Unit,
            "LUMA_TEXT_ARG_TAG" => Arg::Tag,
            "LUMA_TEXT_ARG_NAME" => Arg::Name,
            "LUMA_TEXT_ARG_HIGH" => Arg::High,
            "LUMA_TEXT_ARG_ZERO" => Arg::Zero,
            "LUMA_TEXT_ARG_INDEX" => Arg::Index,
            other => panic!("{other} is no argument kind"),
        };
        let most = match f[4] {
            "LUMA_TEXT_TAG_UNITS_MOST" => TAG_UNITS_MAX as u16,
            hex if hex.starts_with("0x") => u16::from_str_radix(&hex[2..], 16).unwrap(),
            n => n.parse().unwrap(),
        };
        let want = (f[0].trim_matches('"'), f[1].parse::<u16>().unwrap(), f[2].parse::<u16>().unwrap(), arg, most, means.trim_end_matches('"'));
        assert_eq!(want, (t.word, t.group, t.tag, t.arg, t.most, t.means), "row {line}");

        let unit = |count: u16| (6 + 2 * count) | (t.group << 8);
        let (text, units): (String, Vec<u16>) = match t.arg {
            Arg::None => (format!("[{}]", t.word), vec![0x1A, unit(0), t.tag]),
            Arg::Zero => (format!("[{}]", t.word), vec![0x1A, unit(1), t.tag, 0]),
            Arg::Unit => (format!("[{} 7]", t.word), vec![0x1A, unit(1), t.tag, 7]),
            Arg::High => (format!("[{} 3]", t.word), vec![0x1A, unit(1), t.tag, 3 << 8]),
            Arg::Tag => (format!("[{} 7]", t.word), vec![0x1A, unit(0), 7]),
            Arg::Name => (format!("[{} AB_1]", t.word), vec![0x1A, unit(4), t.tag, 0x41, 0x42, 0x5F, 0x31]),
            Arg::Index => (format!("[{} 5 2]", t.word), vec![0x1A, unit(4), 5, 0, 0, 0, 2]),
        };
        assert_eq!(encode_text(&text), units, "{text}");
        assert_eq!(decode_text(&units), text, "{text}");
        if matches!(t.arg, Arg::Unit | Arg::High | Arg::Tag) && t.most < 0xFFFF {
            let past = format!("[{} {}]", t.word, u32::from(t.most) + 1);
            assert_eq!(encode_text(&past), past.encode_utf16().collect::<Vec<_>>(), "{past}");
        }
        if !matches!(t.arg, Arg::None | Arg::Zero) {
            let bare = format!("[{}]", t.word);
            assert_eq!(encode_text(&bare), bare.encode_utf16().collect::<Vec<_>>(), "{bare}");
        }
    }
}
