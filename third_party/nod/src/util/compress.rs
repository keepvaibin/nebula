/// Decodes the LZMA Properties byte (lc/lp/pb).
/// See `lzma_lzma_lclppb_decode` in `liblzma/lzma/lzma_decoder.c`.
#[cfg(feature = "compress-lzma")]
pub fn lzma_lclppb_decode(
    options: &mut liblzma::stream::LzmaOptions,
    byte: u8,
) -> std::io::Result<()> {
    let mut d = byte as u32;
    if d >= (9 * 5 * 5) {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidData,
            format!("Invalid LZMA props byte: {}", d),
        ));
    }
    options.literal_context_bits(d % 9);
    d /= 9;
    options.position_bits(d / 5);
    options.literal_position_bits(d % 5);
    Ok(())
}

/// Decodes LZMA properties.
/// See `lzma_lzma_props_decode` in `liblzma/lzma/lzma_decoder.c`.
#[cfg(feature = "compress-lzma")]
pub fn lzma_props_decode(props: &[u8]) -> std::io::Result<liblzma::stream::LzmaOptions> {
    use crate::array_ref;
    if props.len() != 5 {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidData,
            format!("Invalid LZMA props length: {}", props.len()),
        ));
    }
    let mut options = liblzma::stream::LzmaOptions::new();
    lzma_lclppb_decode(&mut options, props[0])?;
    options.dict_size(u32::from_le_bytes(*array_ref!(props, 1, 4)));
    Ok(options)
}

/// Decodes LZMA2 properties.
/// See `lzma_lzma2_props_decode` in `liblzma/lzma/lzma2_decoder.c`.
#[cfg(feature = "compress-lzma")]
pub fn lzma2_props_decode(props: &[u8]) -> std::io::Result<liblzma::stream::LzmaOptions> {
    use std::cmp::Ordering;
    if props.len() != 1 {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidData,
            format!("Invalid LZMA2 props length: {}", props.len()),
        ));
    }
    let d = props[0] as u32;
    let mut options = liblzma::stream::LzmaOptions::new();
    options.dict_size(match d.cmp(&40) {
        Ordering::Greater => {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                format!("Invalid LZMA2 props byte: {}", d),
            ));
        }
        Ordering::Equal => u32::MAX,
        Ordering::Less => (2 | (d & 1)) << (d / 2 + 11),
    });
    Ok(options)
}

/// Creates a new raw LZMA decoder with the given options.
#[cfg(feature = "compress-lzma")]
pub fn new_lzma_decoder<R>(
    reader: R,
    options: &liblzma::stream::LzmaOptions,
) -> std::io::Result<liblzma::read::XzDecoder<R>>
where
    R: std::io::Read,
{
    let mut filters = liblzma::stream::Filters::new();
    filters.lzma1(options);
    let stream =
        liblzma::stream::Stream::new_raw_decoder(&filters).map_err(std::io::Error::from)?;
    Ok(liblzma::read::XzDecoder::new_stream(reader, stream))
}

/// Creates a new raw LZMA2 decoder with the given options.
#[cfg(feature = "compress-lzma")]
pub fn new_lzma2_decoder<R>(
    reader: R,
    options: &liblzma::stream::LzmaOptions,
) -> std::io::Result<liblzma::read::XzDecoder<R>>
where
    R: std::io::Read,
{
    let mut filters = liblzma::stream::Filters::new();
    filters.lzma2(options);
    let stream =
        liblzma::stream::Stream::new_raw_decoder(&filters).map_err(std::io::Error::from)?;
    Ok(liblzma::read::XzDecoder::new_stream(reader, stream))
}
