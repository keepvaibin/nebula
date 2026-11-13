use super::*;
use crate::cook::refuse::said;
use crate::cook::ui_layout::{read_layout, write_layout, UiPayload};

fn none(_: &str) -> Option<String> {
    None
}

const HUD: &str = r#"{
  "panes": [
    { "name": "Hud", "type": "pane", "position": [276, -156], "widen": true, "children": [
      { "name": "Icon", "type": "picture", "texture": "Star.png", "size": [32, 40], "anchor": "left",
        "corners": ["ffffffff", "ffffffff", "808080ff", "808080ff"] },
      { "name": "Count", "type": "text", "size": [200, 40], "position": [-3, 3], "text": "×07 ルイージ", "length": 16,
        "font": "NumberFont", "font_size": 32, "place": "right", "colour": "ffd700ff", "char_space": -1 },
      { "name": "Hit", "type": "bounding", "size": [30, 40], "visible": false }
    ]},
    { "name": "Plain", "type": "picture", "size": [8, 8], "colour": "00000080", "alpha": 128, "rotation": 45 }
  ],
  "groups": [ { "name": "Counter", "panes": ["Icon", "count"] } ]
}"#;

#[test]
fn a_text_names_a_mods_font_by_its_path_in_the_store() {
    let source = |font: &str| {
        format!(r#"{{"panes":[{{"name":"A","type":"text","size":[10,10],"font":"{font}"}},{{"name":"B","type":"text","size":[10,10]}}]}}"#)
    };
    for written in ["Luma/my.mod/fonts/Title", "Luma/my.mod/fonts/Title.font.json", "luma/my.mod/fonts/Title.brfnt"] {
        let (brlyt, _) = to_brlyt(source(written).as_bytes(), &none).unwrap();
        let ir = read_layout(&brlyt).unwrap();
        assert_eq!(ir.fonts, ["Luma/my.mod/fonts/Title.brfnt", "MessageFont26.brfnt"], "{written}");
    }
}

#[test]
fn a_layout_reads_as_the_games_layouts_read() {
    let (brlyt, facts) = to_brlyt(HUD.as_bytes(), &none).unwrap();
    assert_eq!(facts, LayoutFacts { panes: 5, pictures: 2, texts: 1, textures: 1, groups: 1, windows: 0, animations: 0 });
    // The game's own reader takes it, and the store's writer writes it.
    let ir = read_layout(&brlyt).unwrap();
    write_layout(&ir).unwrap();
    assert!(ir.centered);
    assert_eq!(ir.size, SCREEN);
    assert_eq!(ir.textures, ["Star.tpl"]);
    assert_eq!(ir.fonts, ["NumberFont.brfnt"]);
    let names: Vec<(&str, Option<usize>)> = ir.panes.iter().map(|p| (p.name.as_str(), p.parent)).collect();
    assert_eq!(names, [("RootPane", None), ("Hud", Some(0)), ("Icon", Some(1)), ("Count", Some(1)), ("Hit", Some(1)), ("Plain", Some(0))]);
    let hud = &ir.panes[1];
    assert_eq!((hud.visible, hud.influenced_alpha, hud.location_adjust, hud.base_position), (true, true, true, 4));
    assert_eq!(hud.translate, [276.0, -156.0, 0.0]);
    let UiPayload::Picture(icon) = &ir.panes[2].payload else { panic!("Icon: {:?}", ir.panes[2].payload) };
    assert_eq!((ir.panes[2].base_position, ir.panes[2].size), (3, [32.0, 40.0]));
    assert_eq!(icon.vertex_colors[2], [0x80, 0x80, 0x80, 0xFF]);
    assert_eq!(icon.coords, [[[0.0, 0.0], [1.0, 0.0], [0.0, 1.0], [1.0, 1.0]]]);
    assert_eq!(ir.materials[icon.material as usize].maps.len(), 1);
    assert_eq!(ir.materials[icon.material as usize].name, "Icon");
    let UiPayload::TextBox(count) = &ir.panes[3].payload else { panic!("Count") };
    let words: Vec<u16> = "×07 ルイージ".encode_utf16().chain([0]).collect();
    assert_eq!(count.text, words);
    assert_eq!((count.buffer_bytes, count.text_bytes, count.font, count.position, count.alignment), (34, words.len() as u16 * 2, 0, 5, 0));
    assert_eq!((count.colors, count.font_size, count.char_space), ([[0xFF, 0xD7, 0, 0xFF]; 2], [32.0, 32.0], -1.0));
    assert!(ir.materials[count.material as usize].maps.is_empty());
    assert!(matches!(ir.panes[4].payload, UiPayload::Bounding) && !ir.panes[4].visible);
    let plain = &ir.panes[5];
    assert_eq!((plain.alpha, plain.rotate), (128, [0.0, 0.0, 45.0]));
    let UiPayload::Picture(p) = &plain.payload else { panic!("Plain") };
    assert!(p.coords.is_empty() && ir.materials[p.material as usize].maps.is_empty());
    assert_eq!(p.vertex_colors, [[0, 0, 0, 0x80]; 4]);
    // A group names its panes as the layout does (any case in the source).
    assert!(ir.has_group_container);
    assert_eq!((ir.groups[0].name.as_str(), ir.groups[0].panes.clone()), ("Counter", vec![2, 3]));
}

#[test]
fn a_picture_material_is_the_one_the_games_layouts_carry() {
    // LayoutData/CoinCounter.arc blyt/coincounter.brlyt, material PicX (texture 1), after its name: black to white,
    // white constants, one map, one transform, one generator, no stages.
    let game = "000000000000000000ff00ff00ff00ff00ff00ff00ff00ff\
                ffffffffffffffffffffffffffffffff00000111\
                00010000000000000000000000000000\
                3f8000003f80000001041e00";
    let made = material("PicX", Some(1));
    let hex: String = made[20..].iter().map(|b| format!("{b:02x}")).collect();
    assert_eq!(hex, game.replace(' ', ""));
    let bits = &material("TxtNumber", None)[60..];
    assert_eq!(bits, [0, 0, 0, 0]);
}

#[test]
fn an_empty_layout_is_its_root_alone() {
    let (brlyt, facts) = to_brlyt(br#"{"size": [640, 480], "panes": []}"#, &none).unwrap();
    assert_eq!(facts, LayoutFacts::default());
    let ir = read_layout(&brlyt).unwrap();
    assert_eq!((ir.panes.len(), ir.size, ir.textures.len(), ir.materials.len()), (1, [640.0, 480.0], 0, 0));
}

#[test]
fn a_layout_says_what_it_cannot_make() {
    let refused = |source: &str| said(&to_brlyt(source.as_bytes(), &none).unwrap_err());
    let pane = |extra: &str| format!(r#"{{"panes":[{{"name":"A","type":"text","size":[1,1]{extra}}}]}}"#);
    let cases: &[(String, &str, &str)] = &[
        ("{".into(), "Cook.Model.Json", ""),
        (r#"{"panes":[],"pane":[]}"#.into(), "Cook.Effect.UnknownPart", "/"),
        (r#"{"size":[0,4],"panes":[]}"#.into(), "Cook.Effect.Range", "/size"),
        ("{}".into(), "Cook.Effect.Missing", "/"),
        (r#"{"panes":[{"name":"A","type":"window","size":[1,1]}]}"#.into(), "Cook.Effect.Missing", "/panes/0"),
        (r#"{"panes":[{"name":"A","type":"button","size":[1,1]}]}"#.into(), "Cook.Effect.Choice", "/panes/0/type"),
        (r#"{"patch":1,"panes":{}}"#.into(), "Cook.Layout.PatchPlace", "/patch"),
        (r#"{"panes":[{"type":"pane"}]}"#.into(), "Cook.Effect.Missing", "/panes/0"),
        (r#"{"panes":[{"name":"A very long pane name","type":"pane"}]}"#.into(), "Cook.Layout.Name", "/panes/0/name"),
        (r#"{"panes":[{"name":"Bad name","type":"pane"}]}"#.into(), "Cook.Layout.Name", "/panes/0/name"),
        (r#"{"panes":[{"name":"rootpane","type":"pane"}]}"#.into(), "Cook.Layout.Twice", "/panes/0/name"),
        (r#"{"panes":[{"name":"A","type":"pane","children":[{"name":"a","type":"pane"}]}]}"#.into(), "Cook.Layout.Twice", "/panes/0/children/0/name"),
        (r#"{"panes":[{"name":"A","type":"picture"}]}"#.into(), "Cook.Effect.Missing", "/panes/0"),
        (r#"{"panes":[{"name":"A","type":"pane","size":[-1,1]}]}"#.into(), "Cook.Effect.Range", "/panes/0/size"),
        (r#"{"panes":[{"name":"A","type":"pane","text":"x"}]}"#.into(), "Cook.Effect.UnknownPart", "/panes/0"),
        (r#"{"panes":[{"name":"A","type":"pane","anchor":"middle"}]}"#.into(), "Cook.Effect.Choice", "/panes/0/anchor"),
        (r#"{"panes":[{"name":"A","type":"pane","alpha":256}]}"#.into(), "Cook.Effect.Range", "/panes/0/alpha"),
        (r#"{"panes":[{"name":"A","type":"pane","visible":1}]}"#.into(), "Cook.Effect.Kind", "/panes/0/visible"),
        (r#"{"panes":[{"name":"A","type":"pane","position":[1]}]}"#.into(), "Cook.Effect.Length", "/panes/0/position"),
        (r#"{"panes":[{"name":"A","type":"pane","position":[1e39,0]}]}"#.into(), "Cook.Effect.Range", "/panes/0/position/0"),
        (r#"{"panes":[{"name":"A","type":"picture","size":[1,1],"texture":"../x"}]}"#.into(), "Cook.Effect.TextureName", "/panes/0/texture"),
        (r#"{"panes":[{"name":"A","type":"picture","size":[1,1],"colour":"fff","corners":[]}]}"#.into(), "Cook.Layout.Both", "/panes/0"),
        (r#"{"panes":[{"name":"A","type":"picture","size":[1,1],"corners":["ffffffff"]}]}"#.into(), "Cook.Effect.Length", "/panes/0/corners"),
        (r#"{"panes":[{"name":"A","type":"picture","size":[1,1],"colour":"red"}]}"#.into(), "Cook.Effect.Color", "/panes/0/colour"),
        (pane(r#","font":"Arial""#), "Cook.Effect.Choice", "/panes/0/font"),
        (pane(r#","font":"Luma/my.mod""#), "Cook.Effect.Choice", "/panes/0/font"),
        (pane(r#","font":"Luma/my.mod/../x""#), "Cook.Effect.Choice", "/panes/0/font"),
        (pane(r#","font":"Luma/<id>/<path>""#), "Cook.Effect.Choice", "/panes/0/font"),
        (pane(r#","text":"abc","length":2"#), "Cook.Layout.TextLong", "/panes/0/text"),
        (pane(&format!(r#","text":"{}""#, "x".repeat(256))), "Cook.Layout.TextLong", "/panes/0/text"),
        (pane(r#","length":0"#), "Cook.Effect.Range", "/panes/0/length"),
        (pane(r#","text":"a\u001ab""#), "Cook.Layout.Control", "/panes/0/text"),
        (pane(r#","align":"justify""#), "Cook.Effect.Choice", "/panes/0/align"),
        (pane(r#","text":"a\nb","font_size":1"#), "Cook.Layout.TextTall", "/panes/0/text"),
        (pane(r#","text":"abcdefgh","font_size":1"#), "Cook.Layout.TextWide", "/panes/0/text"),
        (r#"{"panes":[],"groups":[{"name":"G","panes":["Nope"]}]}"#.into(), "Cook.Layout.NoPane", "/groups/0/panes/0"),
        (r#"{"panes":[],"groups":[{"name":"G","panes":[]},{"name":"g","panes":[]}]}"#.into(), "Cook.Layout.Twice", "/groups/1/name"),
        (r#"{"panes":[],"groups":[{"name":"RootGroup","panes":[]}]}"#.into(), "Cook.Layout.Twice", "/groups/0/name"),
    ];
    for (source, why, at) in cases {
        let (got, args) = refused(source);
        assert_eq!(got, *why, "{source}: {args:?}");
        if !at.is_empty() {
            assert_eq!(args.iter().find(|(n, _)| *n == "where").map(|(_, v)| v.as_str()), Some(*at), "{source}: {args:?}");
        }
    }
    // A texture the mod does not have, named with the file it would be.
    let missing = |name: &str| Some(format!("LayoutData/Hud.arc/timg/{name}.png"));
    let (why, args) = said(&to_brlyt(br#"{"panes":[{"name":"A","type":"picture","size":[1,1],"texture":"Star"}]}"#, &missing).unwrap_err());
    assert_eq!(why, "Cook.Layout.NoTexture");
    assert!(args.contains(&("uri", "LayoutData/Hud.arc/timg/Star.png".to_string())), "{args:?}");
}

#[test]
fn too_many_panes_are_refused_before_the_file_is_written() {
    let panes: Vec<String> = (0..=PANES_MAX).map(|i| format!(r#"{{"name":"P{i}","type":"pane"}}"#)).collect();
    let source = format!(r#"{{"panes":[{}]}}"#, panes.join(","));
    let (why, args) = said(&to_brlyt(source.as_bytes(), &none).unwrap_err());
    assert_eq!(why, "Cook.Layout.TooMany");
    assert!(args.contains(&("max", PANES_MAX.to_string())), "{args:?}");
    // The most it takes still reads (a pane each, no children: one block each).
    let source = format!(r#"{{"panes":[{}]}}"#, panes[..PANES_MAX].join(","));
    let ir = read_layout(&to_brlyt(source.as_bytes(), &none).unwrap().0).unwrap();
    assert_eq!(ir.panes.len(), PANES_MAX + 1);
}

#[test]
fn a_window_and_an_animation_are_the_games_files() {
    use super::super::ui_layout::{read_animation, read_layout, UiPayload};
    let source = br#"{"panes":[{"name":"Box","type":"window","size":[200,100],"frame":"Corner","inflation":[2,2,3,3],"colour":"102030ff",
        "children":[{"name":"Label","type":"text","size":[180,40],"text":"Hi"}]}],
        "animations":[{"name":"Open","frames":20,"tracks":[{"pane":"Box","what":"scale_y","keys":[[0,0],[20,1]]},{"pane":"Label","what":"alpha","keys":[[0,0],[20,255]]}]}]}"#;
    let (brlyt, anims, facts) = to_files(source, "Menu", &none, &|_| None).unwrap();
    assert_eq!((facts.windows, facts.animations), (1, 1));
    let ir = read_layout(&brlyt).unwrap();
    let window = ir.panes.iter().find(|p| p.name == "Box").unwrap();
    let UiPayload::Window(w) = &window.payload else { panic!("a window") };
    assert_eq!(w.inflation, [2.0, 2.0, 3.0, 3.0]);
    assert_eq!(w.content.vertex_colors, [[0x10, 0x20, 0x30, 0xFF]; 4]);
    // The frame's material is the corner texture's, named after the pane.
    assert_eq!(w.frames.len(), 1);
    assert_eq!(ir.materials[w.frames[0].0 as usize].name, "Box_F");
    assert_eq!(anims[0].0, "anim/Menu_Open.brlan");
    let a = read_animation(&anims[0].1).unwrap();
    assert_eq!((a.frame_size, a.contents.len()), (20, 2));
}
