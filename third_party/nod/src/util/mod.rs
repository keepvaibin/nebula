use std::ops::{Div, Rem};

pub(crate) mod compress;
pub(crate) mod lfg;
pub(crate) mod read;
pub(crate) mod take_seek;

#[inline(always)]
pub(crate) fn div_rem<T>(x: T, y: T) -> (T, T)
where T: Div<Output = T> + Rem<Output = T> + Copy {
    let quot = x / y;
    let rem = x % y;
    (quot, rem)
}

/// Creates a fixed-size array reference from a slice.
#[macro_export]
macro_rules! array_ref {
    ($slice:expr, $offset:expr, $size:expr) => {{
        #[inline(always)]
        fn to_array<T>(slice: &[T]) -> &[T; $size] {
            unsafe { &*(slice.as_ptr() as *const [_; $size]) }
        }
        to_array(&$slice[$offset..$offset + $size])
    }};
}

/// Creates a mutable fixed-size array reference from a slice.
#[macro_export]
macro_rules! array_ref_mut {
    ($slice:expr, $offset:expr, $size:expr) => {{
        #[inline(always)]
        fn to_array<T>(slice: &mut [T]) -> &mut [T; $size] {
            unsafe { &mut *(slice.as_ptr() as *mut [_; $size]) }
        }
        to_array(&mut $slice[$offset..$offset + $size])
    }};
}

/// Compile-time assertion.
#[macro_export]
macro_rules! static_assert {
    ($condition:expr) => {
        const _: () = core::assert!($condition);
    };
}
