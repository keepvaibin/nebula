//! Message files (NMSG): a disc's BMG as the plain arrays MessageData reads.
//!
//! Layout, little-endian:
//! - header (48 bytes): "NMSG", then the counts and offsets of the messages, the text (16-bit units), the flow nodes
//!   and the branches, and 2 reserved words;
//! - messages (12 bytes): the text's first unit, the camera set (u16), then bytes: _6, camera type, talk type, balloon
//!   type, _A, _B;
//! - text: UTF-16 units, each message ending in a 0. A 0x1A unit starts a tag: the next unit holds the tag's length in
//!   bytes (from the 0x1A) and its group as two bytes, then the tag's id and parameters in units;
//! - nodes (16 bytes): type (1 message, 2 branch, 3 event), group (an event's kind), reserved (u16, 0), index (a message's
//!   number, a branch's condition, an event's first branch), next (a message's next node, NONE at the end; a branch's
//!   parameter; an event's value), next group (a message's group; a branch's first branch);
//! - branches: node numbers, NONE for none.

use anyhow::{bail, Context, Result};

pub const MAGIC: &[u8; 4] = b"NMSG";
/// A node's reserved u16 (TalkNode::mReserved).
const NODE_RESERVED: [u8; 2] = [0, 0];
pub const NONE: u32 = u32::MAX;
const HEADER: usize = 48;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Message {
    pub text: u32,
    pub camera_set: u16,
    pub bytes: [u8; 6],
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Node {
    pub kind: u8,
    pub group: u8,
    pub index: u32,
    pub next: u32,
    pub next_group: u32,
}

#[derive(Debug, Default, PartialEq)]
pub struct Nmsg {
    pub messages: Vec<Message>,
    pub text: Vec<u16>,
    pub nodes: Vec<Node>,
    pub branches: Vec<u32>,
}

fn be16(bytes: &[u8], at: usize) -> Result<u16> {
    let b = bytes.get(at..at + 2).context("past the message file's end")?;
    Ok(u16::from_be_bytes([b[0], b[1]]))
}

fn be32(bytes: &[u8], at: usize) -> Result<u32> {
    let b = bytes.get(at..at + 4).context("past the message file's end")?;
    Ok(u32::from_be_bytes([b[0], b[1], b[2], b[3]]))
}

fn le16(bytes: &[u8], at: usize) -> Result<u16> {
    let b = bytes.get(at..at + 2).context("past the message file's end")?;
    Ok(u16::from_le_bytes([b[0], b[1]]))
}

fn le32(bytes: &[u8], at: usize) -> Result<u32> {
    let b = bytes.get(at..at + 4).context("past the message file's end")?;
    Ok(u32::from_le_bytes([b[0], b[1], b[2], b[3]]))
}

/// A disc BMG's blocks: (magic, start, end), the last block's end clipped to the file (its size may count padding the
/// archive does not store).
fn blocks(bytes: &[u8]) -> Result<Vec<([u8; 4], usize, usize)>> {
    if bytes.len() < 0x20 || !bytes.starts_with(b"MESGbmg1") {
        bail!("not a message file");
    }
    if bytes[0x10] != 2 {
        bail!("a message file in encoding {} (only UTF-16 is read)", bytes[0x10]);
    }
    let count = be32(bytes, 0xC)?;
    let mut at = 0x20usize;
    let mut out = Vec::new();
    for block in 0..count {
        let magic: [u8; 4] = bytes.get(at..at + 4).context("a block past the file's end")?.try_into().unwrap();
        let size = be32(bytes, at + 4)? as usize;
        if size < 8 || (at + size > bytes.len() && block + 1 != count) {
            bail!("block {} at {at:#x}: size {size:#x} outside the file", String::from_utf8_lossy(&magic));
        }
        let end = (at + size).min(bytes.len());
        out.push((magic, at, end));
        at = end;
    }
    Ok(out)
}

impl Nmsg {
    pub fn from_disc(bytes: &[u8]) -> Result<Nmsg> {
        let mut nmsg = Nmsg::default();
        for (magic, at, end) in blocks(bytes)? {
            match &magic {
                b"INF1" => {
                    let count = be16(bytes, at + 8)? as usize;
                    let item = be16(bytes, at + 10)? as usize;
                    if item < 12 || at + 16 + count * item > end {
                        bail!("INF1: {count} items of {item} bytes");
                    }
                    for i in 0..count {
                        let entry = at + 16 + i * item;
                        let offset = be32(bytes, entry)?;
                        if offset % 2 != 0 {
                            bail!("INF1: message {i}'s text at odd byte {offset}");
                        }
                        let mut message = Message { text: offset / 2, camera_set: be16(bytes, entry + 4)?, bytes: [0; 6] };
                        message.bytes.copy_from_slice(&bytes[entry + 6..entry + 12]);
                        nmsg.messages.push(message);
                    }
                }
                b"DAT1" => {
                    // Every unit big-endian, except the unit after a 0x1A, which holds two bytes.
                    let mut p = at + 8;
                    while p + 2 <= end {
                        let unit = be16(bytes, p)?;
                        nmsg.text.push(unit);
                        p += 2;
                        if unit != 0x1A || p + 2 > end {
                            continue;
                        }
                        let tag_end = (p - 2 + bytes[p] as usize).min(end);
                        if tag_end < p + 2 || bytes[p] % 2 != 0 {
                            bail!("DAT1: a tag at {:#x} of {} bytes", p - 2, bytes[p]);
                        }
                        nmsg.text.push(u16::from_le_bytes([bytes[p], bytes[p + 1]]));
                        p += 2;
                        while p + 2 <= tag_end {
                            nmsg.text.push(be16(bytes, p)?);
                            p += 2;
                        }
                        p = tag_end;
                    }
                }
                b"FLW1" => {
                    let nodes = be16(bytes, at + 8)? as usize;
                    let branches = be16(bytes, at + 10)? as usize;
                    if at + 16 + nodes * 8 + branches * 2 > end {
                        bail!("FLW1: {nodes} nodes and {branches} branches");
                    }
                    for i in 0..nodes {
                        let n = at + 16 + i * 8;
                        let kind = bytes[n];
                        let (next, next_group) = if kind == 3 {
                            (be32(bytes, n + 4)?, 0)
                        } else {
                            let next = be16(bytes, n + 4)?;
                            let next = if kind == 1 && next == 0xFFFF { NONE } else { u32::from(next) };
                            (next, u32::from(be16(bytes, n + 6)?))
                        };
                        nmsg.nodes.push(Node { kind, group: bytes[n + 1], index: u32::from(be16(bytes, n + 2)?), next, next_group });
                    }
                    for b in 0..branches {
                        let node = be16(bytes, at + 16 + nodes * 8 + b * 2)?;
                        nmsg.branches.push(if node == 0xFFFF { NONE } else { u32::from(node) });
                    }
                }
                b"FLI1" => {} // the game reads nothing in it
                _ => bail!("unknown block {} at {at:#x}", String::from_utf8_lossy(&magic)),
            }
        }
        for (i, message) in nmsg.messages.iter().enumerate() {
            if message.text as usize >= nmsg.text.len() && !nmsg.text.is_empty() {
                bail!("message {i}'s text at unit {} past the {} units", message.text, nmsg.text.len());
            }
        }
        nmsg.check_flow()?;
        Ok(nmsg)
    }

    /// The flow's references as TalkNodeCtrl follows them, so the game never reads past its nodes or branches: a
    /// message's next node (NONE ends the flow), a branch's two choices (its first branch and the one after), an event's
    /// first branch, and every branch's node (NONE: none). A mod may give a whole message file; the console followed
    /// whatever it held.
    pub fn check_flow(&self) -> Result<()> {
        let nodes = self.nodes.len();
        let branches = self.branches.len();
        for (i, node) in self.nodes.iter().enumerate() {
            match node.kind {
                1 if node.next != NONE && node.next as usize >= nodes => {
                    bail!("FLW1: message node {i} goes on to node {} of {nodes}", node.next)
                }
                2 if node.next_group as usize + 1 >= branches => {
                    bail!("FLW1: branch node {i}'s choices are branches {} and {} of {branches}", node.next_group, node.next_group + 1)
                }
                3 if node.index as usize >= branches => bail!("FLW1: event node {i}'s branch {} of {branches}", node.index),
                _ => {}
            }
        }
        for (b, &node) in self.branches.iter().enumerate() {
            if node != NONE && node as usize >= nodes {
                bail!("FLW1: branch {b} goes to node {node} of {nodes}");
            }
        }
        // The talk's loop (TalkMessageCtrl::rootNodePre) goes on, in one step, from a branch to one of its choices (any
        // branch but a select, which waits for the player) and from an event that goes on by itself (func, none, anime)
        // to its branch, until it reaches another node: a ring of those never ends.
        let goes_on = |i: usize| -> Vec<usize> {
            let node = &self.nodes[i];
            let to = |b: usize| self.branches.get(b).copied().filter(|&n| n != NONE).map(|n| n as usize);
            match node.kind {
                2 if node.index != 0 => [to(node.next_group as usize), to(node.next_group as usize + 1)].into_iter().flatten().collect(),
                3 if matches!(node.group, 0 | 2 | 4) => to(node.index as usize).into_iter().collect(),
                _ => Vec::new(),
            }
        };
        // 0: not seen, 1: on the walk, 2: ends.
        let mut seen = vec![0u8; nodes];
        for first in 0..nodes {
            if seen[first] != 0 {
                continue;
            }
            seen[first] = 1;
            let mut walk = vec![(first, goes_on(first), 0usize)];
            while let Some(top) = walk.last_mut() {
                if top.2 == top.1.len() {
                    let done = top.0;
                    walk.pop();
                    seen[done] = 2;
                    continue;
                }
                let next = top.1[top.2];
                top.2 += 1;
                match seen[next] {
                    0 => {
                        seen[next] = 1;
                        walk.push((next, goes_on(next), 0));
                    }
                    1 => bail!("FLW1: node {next} comes back to itself through branches and events that go on by themselves (the talk would never stop)"),
                    _ => {}
                }
            }
        }
        Ok(())
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let messages_at = HEADER;
        let text_at = messages_at + 12 * self.messages.len();
        let nodes_at = (text_at + 2 * self.text.len() + 3) & !3;
        let branches_at = nodes_at + 16 * self.nodes.len();
        let mut out = Vec::with_capacity(branches_at + 4 * self.branches.len());
        out.extend_from_slice(MAGIC);
        for word in [
            self.messages.len(),
            messages_at,
            self.text.len(),
            text_at,
            self.nodes.len(),
            nodes_at,
            self.branches.len(),
            branches_at,
            0,
            0,
        ] {
            out.extend_from_slice(&(word as u32).to_le_bytes());
        }
        out.extend_from_slice(&[0; 4]);
        for m in &self.messages {
            out.extend_from_slice(&m.text.to_le_bytes());
            out.extend_from_slice(&m.camera_set.to_le_bytes());
            out.extend_from_slice(&m.bytes);
        }
        for unit in &self.text {
            out.extend_from_slice(&unit.to_le_bytes());
        }
        out.resize(nodes_at, 0);
        for n in &self.nodes {
            out.extend_from_slice(&[n.kind, n.group]);
            out.extend_from_slice(&NODE_RESERVED);
            for word in [n.index, n.next, n.next_group] {
                out.extend_from_slice(&word.to_le_bytes());
            }
        }
        for b in &self.branches {
            out.extend_from_slice(&b.to_le_bytes());
        }
        out
    }

    pub fn from_nmsg(bytes: &[u8]) -> Result<Nmsg> {
        if !bytes.starts_with(MAGIC) {
            bail!("not a cooked message file");
        }
        let w = |i: usize| -> Result<usize> { Ok(le32(bytes, 4 * i)? as usize) };
        let mut nmsg = Nmsg::default();
        for i in 0..w(1)? {
            let at = w(2)? + 12 * i;
            let mut m = Message { text: le32(bytes, at)?, camera_set: le16(bytes, at + 4)?, bytes: [0; 6] };
            m.bytes.copy_from_slice(bytes.get(at + 6..at + 12).context("past the end")?);
            nmsg.messages.push(m);
        }
        for i in 0..w(3)? {
            nmsg.text.push(le16(bytes, w(4)? + 2 * i)?);
        }
        for i in 0..w(5)? {
            let at = w(6)? + 16 * i;
            nmsg.nodes.push(Node {
                kind: bytes[at],
                group: bytes[at + 1],
                index: le32(bytes, at + 4)?,
                next: le32(bytes, at + 8)?,
                next_group: le32(bytes, at + 12)?,
            });
        }
        for i in 0..w(7)? {
            nmsg.branches.push(le32(bytes, w(8)? + 4 * i)?);
        }
        Ok(nmsg)
    }
}

/// A message file cooked: a disc's BMG to NMSG.
pub fn cook(bytes: &[u8]) -> Result<Vec<u8>> {
    let out = Nmsg::from_disc(bytes)?.to_bytes();
    #[cfg(feature = "proof-tools")]
    proof::reads_agree(bytes, &out)?;
    Ok(out)
}

/// What the game read from the disc file (MessageData and TalkNode on the BMG blocks) against NMSG: every message's
/// attributes and text to its 0, every node's fields as each node type reads them, every branch (proof-tools: the cook
/// stops on a difference).
#[cfg(feature = "proof-tools")]
pub mod proof {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    pub static FILES: AtomicU64 = AtomicU64::new(0);
    pub static READS: AtomicU64 = AtomicU64::new(0);

    pub fn reads_agree(bytes: &[u8], cooked: &[u8]) -> Result<()> {
        let nmsg = Nmsg::from_nmsg(cooked)?;
        if nmsg.to_bytes() != cooked {
            bail!("NMSG reads back other than it was written");
        }
        let blocks = blocks(bytes)?;
        let block = |magic: &[u8; 4]| blocks.iter().find(|b| &b.0 == magic).map(|b| b.1);
        let mut reads = 0u64;
        if let (Some(inf), Some(dat)) = (block(b"INF1"), block(b"DAT1")) {
            let item = be16(bytes, inf + 10)? as usize;
            for (i, m) in nmsg.messages.iter().enumerate() {
                let entry = inf + 16 + i * item;
                if be16(bytes, entry + 4)? != m.camera_set || bytes[entry + 6..entry + 12] != m.bytes {
                    bail!("message {i}: attributes differ");
                }
                // The text to its 0: the units as the game read them (a tag's second unit as bytes).
                let mut p = dat + 8 + be32(bytes, entry)? as usize;
                let mut u = m.text as usize;
                let mut tag_end = 0usize; // past a tag's last byte while in one
                loop {
                    let in_tag = p < tag_end;
                    let old = be16(bytes, p)?;
                    let new = *nmsg.text.get(u).context("text past its end")?;
                    if old != new {
                        bail!("message {i}: unit {u} reads {old:#x} on the disc, {new:#x} in NMSG");
                    }
                    reads += 1;
                    if !in_tag && old == 0 {
                        break;
                    }
                    p += 2;
                    u += 1;
                    if !in_tag && old == 0x1A {
                        // The tag's length and group: two bytes, read as bytes.
                        let unit = u16::from_le_bytes([bytes[p], bytes[p + 1]]);
                        if nmsg.text.get(u) != Some(&unit) {
                            bail!("message {i}: tag header at unit {u} differs");
                        }
                        reads += 1;
                        tag_end = p - 2 + bytes[p] as usize;
                        p += 2;
                        u += 1;
                    }
                }
            }
        }
        if let Some(flw) = block(b"FLW1") {
            let count = be16(bytes, flw + 8)? as usize;
            for (i, n) in nmsg.nodes.iter().enumerate() {
                let at = flw + 16 + i * 8;
                let index = u32::from(be16(bytes, at + 2)?);
                let ok = n.kind == bytes[at]
                    && n.group == bytes[at + 1]
                    && n.index == index
                    && match n.kind {
                        3 => n.next == be32(bytes, at + 4)?,
                        1 if be16(bytes, at + 4)? == 0xFFFF => n.next == NONE && n.next_group == u32::from(be16(bytes, at + 6)?),
                        _ => n.next == u32::from(be16(bytes, at + 4)?) && n.next_group == u32::from(be16(bytes, at + 6)?),
                    };
                if !ok {
                    bail!("node {i}: {n:?} against the disc's");
                }
                reads += 1;
            }
            for (b, node) in nmsg.branches.iter().enumerate() {
                let old = be16(bytes, flw + 16 + count * 8 + 2 * b)?;
                if (old == 0xFFFF && *node != NONE) || (old != 0xFFFF && *node != u32::from(old)) {
                    bail!("branch {b}: {old:#x} on the disc, {node:#x} in NMSG");
                }
                reads += 1;
            }
        }
        FILES.fetch_add(1, Ordering::Relaxed);
        READS.fetch_add(reads, Ordering::Relaxed);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn disc() -> Vec<u8> {
        let mut out = b"MESGbmg1".to_vec();
        out.extend_from_slice(&[0; 4]); // size
        out.extend_from_slice(&3u32.to_be_bytes());
        out.push(2);
        out.resize(0x20, 0);
        // INF1: two messages of 12 bytes.
        let inf: Vec<u8> = [b"INF1".to_vec(), 40u32.to_be_bytes().to_vec(), 2u16.to_be_bytes().to_vec(), 12u16.to_be_bytes().to_vec(), vec![0; 4]]
            .concat()
            .into_iter()
            .chain([0, 0, 0, 0, 0, 7, 1, 2, 3, 4, 5, 6])
            .chain([0, 0, 0, 4, 0, 0, 0, 0, 1, 0, 0, 0])
            .collect();
        out.extend_from_slice(&inf);
        // DAT1: "A" 0, then a tag (0x1A, length 6, group 1, id 0x0203) and 0.
        let dat: Vec<u8> = [b"DAT1".to_vec(), 24u32.to_be_bytes().to_vec(), vec![0, b'A', 0, 0, 0, 0x1A, 6, 1, 2, 3, 0, 0, 0, 0, 0, 0]].concat();
        out.extend_from_slice(&dat);
        // FLW1: a message node to the end, an event node, one branch to node 0 and one to none.
        let flw: Vec<u8> = [
            b"FLW1".to_vec(),
            36u32.to_be_bytes().to_vec(),
            2u16.to_be_bytes().to_vec(),
            2u16.to_be_bytes().to_vec(),
            vec![0; 4],
            vec![1, 0, 0, 1, 0xFF, 0xFF, 0, 0],
            vec![3, 5, 0, 1, 0x12, 0x34, 0x56, 0x78],
            vec![0, 0, 0xFF, 0xFF],
        ]
        .concat();
        out.extend_from_slice(&flw);
        out
    }

    #[test]
    fn a_disc_message_file_cooks_to_arrays() {
        let nmsg = Nmsg::from_disc(&disc()).unwrap();
        assert_eq!(nmsg.messages[0], Message { text: 0, camera_set: 7, bytes: [1, 2, 3, 4, 5, 6] });
        assert_eq!(nmsg.messages[1].text, 2);
        assert_eq!(nmsg.text, [0x41, 0, 0x1A, u16::from_le_bytes([6, 1]), 0x0203, 0, 0, 0]);
        assert_eq!(nmsg.nodes[0], Node { kind: 1, group: 0, index: 1, next: NONE, next_group: 0 });
        assert_eq!(nmsg.nodes[1], Node { kind: 3, group: 5, index: 1, next: 0x1234_5678, next_group: 0 });
        assert_eq!(nmsg.branches, [0, NONE]);
        let cooked = cook(&disc()).unwrap();
        assert_eq!(Nmsg::from_nmsg(&cooked).unwrap(), nmsg);
    }

    /// The disc file of disc() with its two FLW1 nodes and two branches given (the block's first node at FLW1 + 16).
    fn disc_with_flow(nodes: [[u8; 8]; 2], branches: [u16; 2]) -> Vec<u8> {
        let mut bytes = disc();
        let flw = bytes.windows(4).position(|w| w == b"FLW1").unwrap();
        bytes[flw + 16..flw + 32].copy_from_slice(&[nodes[0], nodes[1]].concat());
        bytes[flw + 32..flw + 34].copy_from_slice(&branches[0].to_be_bytes());
        bytes[flw + 34..flw + 36].copy_from_slice(&branches[1].to_be_bytes());
        bytes
    }

    #[test]
    fn a_flow_that_leads_outside_its_nodes_or_branches_is_refused() {
        let message_end = [1, 0, 0, 1, 0xFF, 0xFF, 0, 0];
        let event = [3, 5, 0, 1, 0x12, 0x34, 0x56, 0x78];
        // The game's own shapes: a message that ends, a message on to node 1, a branch whose choices are branches 0 and 1,
        // an event on a branch of none.
        assert!(Nmsg::from_disc(&disc_with_flow([message_end, event], [0, 0xFFFF])).is_ok());
        assert!(Nmsg::from_disc(&disc_with_flow([[1, 0, 0, 1, 0, 1, 0, 0], event], [0, 0xFFFF])).is_ok());
        assert!(Nmsg::from_disc(&disc_with_flow([[2, 0, 0, 0, 0, 0, 0, 0], event], [0, 1])).is_ok());
        // A message on to node 2 of 2.
        let next = Nmsg::from_disc(&disc_with_flow([[1, 0, 0, 1, 0, 2, 0, 0], event], [0, 0xFFFF]));
        assert!(format!("{:#}", next.unwrap_err()).contains("message node 0 goes on to node 2 of 2"));
        // A branch whose second choice is past the two branches.
        let choice = Nmsg::from_disc(&disc_with_flow([[2, 0, 0, 0, 0, 0, 0, 1], event], [0, 1]));
        assert!(format!("{:#}", choice.unwrap_err()).contains("branch node 0's choices are branches 1 and 2 of 2"));
        // An event on branch 2 of 2.
        let first = Nmsg::from_disc(&disc_with_flow([message_end, [3, 5, 0, 2, 0, 0, 0, 0]], [0, 0xFFFF]));
        assert!(format!("{:#}", first.unwrap_err()).contains("event node 1's branch 2 of 2"));
        // A branch to node 5 of 2.
        let branch = Nmsg::from_disc(&disc_with_flow([message_end, event], [5, 0xFFFF]));
        assert!(format!("{:#}", branch.unwrap_err()).contains("branch 0 goes to node 5 of 2"));
    }

    #[test]
    fn a_ring_of_branches_and_events_that_go_on_by_themselves_is_refused() {
        // Node 0: a func branch (kind 1) whose choices are branches 0 and 1; node 1: a func event on branch 0.
        let func_branch = [2, 0, 0, 1, 0, 0, 0, 0];
        let func_event = [3, 0, 0, 0, 0, 0, 0, 0];
        // Its choices lead to the event and to none: the event goes on to branch 0, node 1, itself.
        let ring = Nmsg::from_disc(&disc_with_flow([func_branch, func_event], [1, 0xFFFF]));
        assert!(format!("{:#}", ring.unwrap_err()).contains("node 1 comes back to itself"));
        // A branch whose choice is itself.
        let own = Nmsg::from_disc(&disc_with_flow([func_branch, func_event], [0, 0xFFFF]));
        assert!(format!("{:#}", own.unwrap_err()).contains("node 0 comes back to itself"));
        // The same ring through a select (the player chooses) or a func-stop event (it waits) stops: allowed.
        // A select (node 0) whose choices are the event and itself; the event goes on to branch 1, the select.
        let select = [2, 0, 0, 0, 0, 0, 0, 0];
        assert!(Nmsg::from_disc(&disc_with_flow([select, [3, 0, 0, 1, 0, 0, 0, 0]], [1, 0])).is_ok());
        // The same with a func branch is a ring.
        assert!(Nmsg::from_disc(&disc_with_flow([func_branch, [3, 0, 0, 1, 0, 0, 0, 0]], [1, 0])).is_err());
        let func_stop = [3, 1, 0, 0, 0, 0, 0, 0];
        assert!(Nmsg::from_disc(&disc_with_flow([func_branch, func_stop], [1, 0xFFFF])).is_ok());
    }
}
