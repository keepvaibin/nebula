//! The census and curve proofs (feature `proof-tools`, never in a release). Their sources sit with the proof tools and
//! read the cook's own modules, which this module passes through.
#[allow(unused_imports)]
use super::*;

pub mod model_census { include!(concat!(env!("NEBULA_PROOF_TOOLS"), "/model_census.rs")); }
pub mod clip_census { include!(concat!(env!("NEBULA_PROOF_TOOLS"), "/clip_census.rs")); }
