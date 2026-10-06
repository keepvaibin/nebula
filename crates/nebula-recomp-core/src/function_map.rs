use serde::Serialize;

const RMGE01_FUNCTIONS: &str = include_str!("../../../metadata/rmge01/functions.csv");

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
pub struct FunctionRange {
    pub address: u32,
    pub size: u32,
}

pub fn rmge01_function_map() -> Vec<FunctionRange> {
    RMGE01_FUNCTIONS
        .lines()
        .skip(1)
        .filter(|line| !line.trim().is_empty())
        .map(|line| {
            let (address, size) = line
                .split_once(',')
                .expect("embedded RMGE01 function metadata must be valid CSV");
            FunctionRange {
                address: u32::from_str_radix(address, 16)
                    .expect("embedded function address must be hexadecimal"),
                size: u32::from_str_radix(size, 16)
                    .expect("embedded function size must be hexadecimal"),
            }
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn embedded_map_is_sorted_aligned_and_non_overlapping() {
        let functions = rmge01_function_map();
        assert_eq!(functions.len(), 41_941);
        for function in &functions {
            assert_eq!(function.address % 4, 0);
            assert_eq!(function.size % 4, 0);
            assert_ne!(function.size, 0);
        }
        for pair in functions.windows(2) {
            assert!(pair[0].address < pair[1].address);
            assert!(pair[0].address + pair[0].size <= pair[1].address);
        }
    }
}
